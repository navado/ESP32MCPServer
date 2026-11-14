#include "McpTlsServer.h"

#if MCP_TLS && !defined(NATIVE_TEST)

#include "TlsCore.h"
#include <Arduino.h>
#include <esp_https_server.h>
#include <cstring>

namespace mcp {

namespace {

const char* const kSessionHeader = "Mcp-Session-Id";
constexpr size_t kMaxBody = 16 * 1024;   // as McpHttpTransport::MAX_BODY_BYTES
// The MCP task can be busy for a while (ble/services/results waits up to
// ~13 s); HTTP clients get this long before 504.
constexpr uint32_t REQUEST_TIMEOUT_MS = 30000;

McpTlsServer* self(httpd_req_t* req) { return static_cast<McpTlsServer*>(req->user_ctx); }

std::string header(httpd_req_t* req, const char* name) {
    size_t n = httpd_req_get_hdr_value_len(req, name);
    if (n == 0) return std::string();
    std::string v(n + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, &v[0], n + 1) != ESP_OK) return std::string();
    v.resize(n);
    return v;
}

const char* statusLine(int status) {
    switch (status) {
        case 200: return "200 OK";
        case 202: return "202 Accepted";
        case 204: return "204 No Content";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 413: return "413 Payload Too Large";
        case 503: return "503 Service Unavailable";
        case 504: return "504 Gateway Timeout";
        default:  return "500 Internal Server Error";
    }
}

esp_err_t sendError(httpd_req_t* req, int status, const char* message) {
    httpd_resp_set_status(req, statusLine(status));
    httpd_resp_set_type(req, "application/json");
    if (status == 401) httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer realm=\"mcp\"");
    std::string body = mcpJsonError(message);
    return httpd_resp_send(req, body.c_str(), body.size());
}

// --- POST / DELETE / GET /mcp ---------------------------------------------

esp_err_t mcpPost(httpd_req_t* req) {
    McpTlsServer* s = self(req);
    McpPostRequest in;
    std::unique_ptr<char[]> body;
    in.contentLength = req->content_len;
    if (req->content_len > 0 && req->content_len <= kMaxBody) {
        body.reset(new char[req->content_len + 1]);
        // A client that stalls mid-body must not hold the (single) server
        // task: give up after a few receive timeouts (~5 s each).
        size_t got = 0;
        int timeouts = 0;
        while (got < req->content_len) {
            int r = httpd_req_recv(req, body.get() + got, req->content_len - got);
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                if (++timeouts >= 3) {
                    httpd_resp_set_status(req, "408 Request Timeout");
                    httpd_resp_send(req, nullptr, 0);
                    return ESP_FAIL;
                }
                continue;
            }
            if (r <= 0) return ESP_FAIL;
            got += static_cast<size_t>(r);
        }
        body[got] = '\0';
        in.body = body.get();
    }
    in.origin        = header(req, "Origin");
    in.host          = header(req, "Host");
    in.authorization = header(req, "Authorization");
    in.sessionId     = header(req, kSessionHeader);
    McpPostOutcome out = s->http_.beginPost(in);
    if (out.status != 200) return sendError(req, out.status, out.error.c_str());
    if (!out.newSessionId.empty()) httpd_resp_set_hdr(req, kSessionHeader, out.newSessionId.c_str());
    if (!out.expectsReply) {
        httpd_resp_set_status(req, statusLine(202));
        return httpd_resp_send(req, nullptr, 0);
    }
    // This task may block: the server has its own task and few sockets.
    uint32_t waited = 0;
    while (!out.job->done.load(std::memory_order_acquire) && waited < REQUEST_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(5));
        waited += 5;
    }
    if (!out.job->done.load(std::memory_order_acquire)) return sendError(req, 504, "Timed out");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, out.job->response.c_str(), out.job->response.size());
}

esp_err_t mcpDelete(httpd_req_t* req) {
    McpTlsServer* s = self(req);
    int status = s->http_.endSession(header(req, "Authorization"), header(req, kSessionHeader));
    if (status == 204) {
        httpd_resp_set_status(req, statusLine(204));
        return httpd_resp_send(req, nullptr, 0);
    }
    return sendError(req, status, status == 401 ? "Unauthorized" : "Unknown or expired session");
}

esp_err_t mcpGet(httpd_req_t* req) {
    // No server-initiated SSE stream (spec: 405), as on port 80.
    httpd_resp_set_status(req, statusLine(405));
    httpd_resp_set_hdr(req, "Allow", "POST, DELETE");
    return httpd_resp_send(req, "Method Not Allowed", HTTPD_RESP_USE_STRLEN);
}

// --- wss:///ws ---------------------------------------------------------------

struct SessionCtx {
    McpTlsServer* server;
    uint8_t       clientId;
};

void releaseSession(void* ctx) {
    // Called by esp_http_server when the socket closes (any reason).
    auto* c = static_cast<SessionCtx*>(ctx);
    McpTlsServer* s = c->server;
    {
        std::lock_guard<std::mutex> lock(s->mu_);
        for (auto& sl : s->slots_)
            if (sl.used && sl.clientId == c->clientId) sl = McpTlsServer::Slot();
    }
    auto job = std::make_shared<McpTlsServer::Job>();
    job->kind = McpTlsServer::Job::Kind::Release;
    job->clientId = c->clientId;
    s->submit(job);
    delete c;
}

void closeWith(httpd_req_t* req, uint16_t code, const char* reason) {
    std::string payload = tls::wsClosePayload(code, reason);
    httpd_ws_frame_t f = {};
    f.final = true;
    f.type = HTTPD_WS_TYPE_CLOSE;
    f.payload = reinterpret_cast<uint8_t*>(&payload[0]);
    f.len = payload.size();
    httpd_ws_send_frame(req, &f);
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
}

esp_err_t wsHandler(httpd_req_t* req) {
    McpTlsServer* s = self(req);
    const int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) {
        // Handshake done (esp_http_server answered the upgrade): admit or
        // close with the same codes as ws://:9000 (4401 / 4429).
        int slot = -1;
        {
            std::lock_guard<std::mutex> lock(s->mu_);
            for (int i = 0; i < MCP_WSS_MAX_CLIENTS; ++i)
                if (!s->slots_[i].used) { slot = i; break; }
            if (slot >= 0) {
                s->slots_[slot].used = true;
                s->slots_[slot].fd = fd;
                s->slots_[slot].clientId = static_cast<uint8_t>(MCP_WSS_CLIENT_BASE + slot);
                s->slots_[slot].gen = ++s->nextGen_;
            }
        }
        if (slot < 0) { closeWith(req, 4429, "Too many clients"); return ESP_OK; }
        const uint8_t cid = static_cast<uint8_t>(MCP_WSS_CLIENT_BASE + slot);
        auto job = std::make_shared<McpTlsServer::Job>();
        job->kind = McpTlsServer::Job::Kind::Admit;
        job->clientId = cid;
        job->authorization = header(req, "Authorization");
        job->url = req->uri;   // carries ?token=
        s->submit(job);
        const bool finished = McpTlsServer::wait(job, 5000);
        if (!finished || job->admission != MCPServer::Admission::Accepted) {
            {
                std::lock_guard<std::mutex> lock(s->mu_);
                s->slots_[slot] = McpTlsServer::Slot();
            }
            if (!finished) {
                // The Admit may still run later: undo it in queue order.
                auto rel = std::make_shared<McpTlsServer::Job>();
                rel->kind = McpTlsServer::Job::Kind::Release;
                rel->clientId = cid;
                s->submit(rel);
            }
            const bool full = job->admission == MCPServer::Admission::TooManyClients;
            closeWith(req, full ? 4429 : 4401, full ? "Too many clients" : "Unauthorized");
            return ESP_OK;
        }
        httpd_sess_set_ctx(req->handle, fd, new SessionCtx{s, cid}, releaseSession);
        return ESP_OK;
    }

    httpd_ws_frame_t f = {};
    if (httpd_ws_recv_frame(req, &f, 0) != ESP_OK) return ESP_FAIL;
    if (f.len > McpTlsServer::MAX_FRAME_BYTES) {
        closeWith(req, 1009, "Message too big");
        return ESP_OK;
    }
    std::string body(f.len, '\0');
    if (f.len) {
        f.payload = reinterpret_cast<uint8_t*>(&body[0]);
        if (httpd_ws_recv_frame(req, &f, f.len) != ESP_OK) return ESP_FAIL;
    }
    if (f.type != HTTPD_WS_TYPE_TEXT || body.empty()) return ESP_OK;
    auto* ctx = static_cast<SessionCtx*>(httpd_sess_get_ctx(req->handle, fd));
    if (!ctx) return ESP_FAIL;   // not admitted
    auto job = std::make_shared<McpTlsServer::Job>();
    job->kind = McpTlsServer::Job::Kind::Message;
    job->clientId = ctx->clientId;
    job->body = std::move(body);
    s->submit(job);
    if (!McpTlsServer::wait(job, REQUEST_TIMEOUT_MS) || job->response.empty()) return ESP_OK;
    httpd_ws_frame_t out = {};
    out.final = true;
    out.type = HTTPD_WS_TYPE_TEXT;
    out.payload = reinterpret_cast<uint8_t*>(&job->response[0]);
    out.len = job->response.size();
    return httpd_ws_send_frame(req, &out);
}

struct PushWork {
    McpTlsServer*  server;
    httpd_handle_t handle;
    int            fd;
    uint8_t        clientId;
    uint32_t       gen;
    std::string    msg;
};

void pushOnServerTask(void* arg) {
    std::unique_ptr<PushWork> w(static_cast<PushWork*>(arg));
    // The socket may have closed and its number been reused since the push
    // was queued: send only if it is still this client's WebSocket.
    if (!w->server->pushTargetValid(w->fd, w->clientId, w->gen)) return;
    httpd_ws_frame_t f = {};
    f.final = true;
    f.type = HTTPD_WS_TYPE_TEXT;
    f.payload = reinterpret_cast<uint8_t*>(&w->msg[0]);
    f.len = w->msg.size();
    httpd_ws_send_frame_async(w->handle, w->fd, &f);
}

}  // namespace

bool McpTlsServer::begin(const std::string& certPem, const std::string& keyPem, uint16_t port) {
    certPem_ = certPem;
    keyPem_ = keyPem;
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.cacert_pem   = reinterpret_cast<const uint8_t*>(certPem_.c_str());
    conf.cacert_len   = certPem_.size() + 1;
    conf.prvtkey_pem  = reinterpret_cast<const uint8_t*>(keyPem_.c_str());
    conf.prvtkey_len  = keyPem_.size() + 1;
    conf.port_secure  = port;
    conf.httpd.max_open_sockets = MCP_WSS_MAX_CLIENTS + 2;   // ~40 KB PSRAM each
    conf.httpd.lru_purge_enable = true;
    conf.httpd.stack_size       = 10240;                     // ECDSA handshakes
    conf.httpd.max_uri_handlers = 6;
    conf.httpd.ctrl_port        = 32769;
    httpd_handle_t h = nullptr;
    if (httpd_ssl_start(&h, &conf) != ESP_OK) return false;
    handle_ = h;

    httpd_uri_t post = {};
    post.uri = "/mcp"; post.method = HTTP_POST; post.handler = mcpPost; post.user_ctx = this;
    httpd_uri_t del = post;
    del.method = HTTP_DELETE; del.handler = mcpDelete;
    httpd_uri_t get = post;
    get.method = HTTP_GET; get.handler = mcpGet;
    httpd_uri_t ws = {};
    ws.uri = "/ws"; ws.method = HTTP_GET; ws.handler = wsHandler; ws.user_ctx = this;
    ws.is_websocket = true;
    httpd_register_uri_handler(h, &post);
    httpd_register_uri_handler(h, &del);
    httpd_register_uri_handler(h, &get);
    httpd_register_uri_handler(h, &ws);

    server_.setClientSender(MCP_WSS_CLIENT_BASE, MCP_WSS_CLIENT_BASE + MCP_WSS_MAX_CLIENTS - 1,
                            [this](uint8_t cid, const std::string& msg) { send(cid, msg); });
    return true;
}

std::shared_ptr<McpTlsServer::Job> McpTlsServer::submit(std::shared_ptr<Job> job) {
    std::lock_guard<std::mutex> lock(mu_);
    queue_.push_back(job);
    return job;
}

bool McpTlsServer::wait(const std::shared_ptr<Job>& job, uint32_t timeoutMs) {
    for (uint32_t t = 0; t < timeoutMs; t += 5) {
        if (job->done.load(std::memory_order_acquire)) return true;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return job->done.load(std::memory_order_acquire);
}

void McpTlsServer::pump() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (queue_.empty()) return;
            job = queue_.front();
            queue_.pop_front();
        }
        switch (job->kind) {
            case Job::Kind::Admit:
                job->admission = server_.admitClient(job->clientId, job->authorization, job->url);
                break;
            case Job::Kind::Message:
                job->response = server_.processMessage(job->clientId, job->body);
                break;
            case Job::Kind::Release:
                server_.removeClient(job->clientId);
                break;
        }
        job->done.store(true, std::memory_order_release);
    }
}

bool McpTlsServer::pushTargetValid(int fd, uint8_t clientId, uint32_t gen) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        bool match = false;
        for (const auto& sl : slots_)
            match |= sl.used && sl.fd == fd && sl.clientId == clientId && sl.gen == gen;
        if (!match) return false;
    }
    if (httpd_ws_get_fd_info(static_cast<httpd_handle_t>(handle_), fd) != HTTPD_WS_CLIENT_WEBSOCKET)
        return false;
    auto* ctx = static_cast<SessionCtx*>(httpd_sess_get_ctx(static_cast<httpd_handle_t>(handle_), fd));
    return ctx && ctx->clientId == clientId;
}

void McpTlsServer::send(uint8_t clientId, const std::string& msg) {
    int fd = -1;
    uint32_t gen = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& sl : slots_)
            if (sl.used && sl.clientId == clientId) { fd = sl.fd; gen = sl.gen; }
    }
    if (fd < 0 || !handle_) return;
    auto* w = new PushWork{this, static_cast<httpd_handle_t>(handle_), fd, clientId, gen, msg};
    if (httpd_queue_work(static_cast<httpd_handle_t>(handle_), pushOnServerTask, w) != ESP_OK)
        delete w;
}

} // namespace mcp

#endif // MCP_TLS
