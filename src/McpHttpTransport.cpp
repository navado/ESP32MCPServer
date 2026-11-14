#include "McpHttpTransport.h"

#if BOARD_HAS_WIFI && !defined(NATIVE_TEST)

#include <ArduinoJson.h>
#include <esp_random.h>

namespace mcp {

namespace {

const char* const kSessionHeader = "Mcp-Session-Id";

String headerValue(AsyncWebServerRequest* req, const char* name) {
    const AsyncWebHeader* h = req->getHeader(name);
    return h ? h->value() : String();
}

// AsyncCallbackWebHandler (server.on) refuses any request whose Accept
// header mentions text/event-stream — it classifies it as an SSE
// subscription — and the MCP spec REQUIRES clients to send
// "Accept: application/json, text/event-stream" on every POST.  This handler
// matches /mcp regardless of that classification.
class McpHandler : public AsyncWebHandler {
public:
    using Fn = std::function<void(AsyncWebServerRequest*)>;
    McpHandler(Fn post, Fn del) : post_(std::move(post)), del_(std::move(del)) {}

    bool canHandle(AsyncWebServerRequest* req) const override {
        return req->url() == "/mcp" &&
               (req->method() & (HTTP_POST | HTTP_GET | HTTP_DELETE | HTTP_OPTIONS));
    }
    bool isRequestHandlerTrivial() const override { return false; }

    void handleBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index,
                    size_t total) override {
        if (total > McpHttpTransport::MAX_BODY_BYTES) return;  // handleRequest answers 413
        if (index == 0) {
            // Freed with free() by AsyncWebServerRequest's destructor.
            req->_tempObject = malloc(total + 1);
        }
        if (!req->_tempObject || index + len > total) return;
        memcpy(static_cast<char*>(req->_tempObject) + index, data, len);
        if (index + len == total) static_cast<char*>(req->_tempObject)[total] = '\0';
    }

    void handleRequest(AsyncWebServerRequest* req) override {
        if (req->method() == HTTP_POST) { post_(req); return; }
        if (req->method() == HTTP_DELETE) { del_(req); return; }
        // No server-initiated SSE stream: GET is not supported (spec: 405).
        AsyncWebServerResponse* r = req->beginResponse(405, "text/plain", "Method Not Allowed");
        r->addHeader("Allow", "POST, DELETE");
        req->send(r);
    }

private:
    Fn post_, del_;
};

}  // namespace

void McpHttpTransport::registerRoutes(AsyncWebServer& web) {
    web.addHandler(new McpHandler([this](AsyncWebServerRequest* r) { handlePost(r); },
                                  [this](AsyncWebServerRequest* r) { handleDelete(r); }));
}

std::string mcpJsonError(const char* message) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"]      = nullptr;
    doc["error"]["code"]    = -32600;
    doc["error"]["message"] = message;
    std::string out;
    serializeJson(doc, out);
    return out;
}

void McpHttpTransport::sendJsonError(AsyncWebServerRequest* req, int status,
                                     const char* message) {
    AsyncWebServerResponse* r =
        req->beginResponse(status, "application/json", jsonError(message).c_str());
    if (status == 401) r->addHeader("WWW-Authenticate", "Bearer realm=\"mcp\"");
    req->send(r);
}

bool McpHttpTransport::enqueue(const std::shared_ptr<Job>& job) {
    std::lock_guard<std::mutex> lock(mu_);
    // Release jobs are never refused: the session is already gone from the
    // table, so dropping the job would leak the server-side client slot.
    if (job->kind != Job::Kind::Release && queue_.size() >= MAX_QUEUED) return false;
    queue_.push_back(job);
    return true;
}

McpHttpTransport::PostOutcome McpHttpTransport::beginPost(const PostRequest& in) {
    PostOutcome out;
    auto fail = [&out](int status, const char* msg) {
        out.status = status;
        out.error = msg;
        return out;
    };
    if (!in.body) {
        return in.contentLength > MAX_BODY_BYTES ? fail(413, "Request too large")
                                                 : fail(400, "Missing JSON-RPC body");
    }
    // DNS-rebinding protection: a browser page from another host must not
    // reach the device.
    if (!http::originAllowed(in.origin, in.host)) return fail(403, "Forbidden origin");

    // Bearer token on every request (same tokens as the WebSocket).
    const auth::Access access = auth::authenticate(creds_, in.authorization, "");
    if (access == auth::Access::None) return fail(401, "Unauthorized");

    // Peek at the message: initialize starts a session; notifications and
    // responses get 202 with no body.
    JsonDocument filter;
    filter["method"] = true;
    filter["id"]     = true;
    JsonDocument peek;
    if (deserializeJson(peek, in.body, DeserializationOption::Filter(filter)))
        return fail(400, "Parse error");
    const bool isInitialize = peek["method"].is<const char*>() &&
                              strcmp(peek["method"].as<const char*>(), "initialize") == 0;
    out.expectsReply = peek["method"].is<const char*>() && !peek["id"].isNull();

    auto job = std::make_shared<Job>();
    job->body = in.body;

    if (isInitialize && in.sessionId.empty()) {
        std::lock_guard<std::mutex> lock(mu_);
        int evicted = -1;
        const http::Session* s =
            sessions_.create(access, millis(), []() { return esp_random(); }, &evicted,
                             [this](uint8_t cid) {
                                 const uint32_t bit = cid - MCP_HTTP_CLIENT_BASE;
                                 return bit >= 32 || !(pinnedMask_.load() & (1u << bit));
                             });
        if (!s) return fail(503, "Too many MCP sessions");
        if (evicted >= 0) {
            // Release the evicted session's server state before the new
            // session's Admit job runs (same queue, so in order).
            auto rel = std::make_shared<Job>();
            rel->kind     = Job::Kind::Release;
            rel->clientId = static_cast<uint8_t>(evicted);
            queue_.push_back(rel);
        }
        out.newSessionId   = s->id;
        job->kind          = Job::Kind::Admit;
        job->clientId      = s->clientId;
        job->authorization = in.authorization;
        job->sessionId     = s->id;
    } else {
        if (in.sessionId.empty()) return fail(400, "Missing Mcp-Session-Id");
        std::lock_guard<std::mutex> lock(mu_);
        const http::Session* s = sessions_.find(in.sessionId, millis());
        // Spec: 404 tells the client to start a new session.
        if (!s) return fail(404, "Unknown or expired session");
        if (s->access != access) return fail(401, "Token does not match the session");
        job->clientId = s->clientId;
    }

    if (!enqueue(job)) return fail(503, "Server busy");
    out.job = job;
    return out;
}

int McpHttpTransport::endSession(const std::string& authorization, const std::string& sessionId) {
    if (auth::authenticate(creds_, authorization, "") == auth::Access::None) return 401;
    int clientId;
    {
        std::lock_guard<std::mutex> lock(mu_);
        clientId = sessions_.remove(sessionId);
    }
    if (clientId < 0) return 404;
    auto job = std::make_shared<Job>();
    job->kind     = Job::Kind::Release;
    job->clientId = static_cast<uint8_t>(clientId);
    enqueue(job);  // always accepted for Release
    return 204;
}

void McpHttpTransport::handlePost(AsyncWebServerRequest* req) {
    PostRequest in;
    in.body          = static_cast<const char*>(req->_tempObject);
    in.contentLength = req->contentLength();
    in.origin        = headerValue(req, "Origin").c_str();
    in.host          = req->host().c_str();
    in.authorization = headerValue(req, "Authorization").c_str();
    in.sessionId     = headerValue(req, kSessionHeader).c_str();
    PostOutcome out = beginPost(in);
    if (out.status != 200) {
        sendJsonError(req, out.status, out.error.c_str());
        return;
    }
    if (!out.expectsReply) {
        req->send(202);
        return;
    }
    auto job = out.job;

    // Most requests complete within a few ms on the MCP task; wait briefly so
    // the answer goes out in one plain response.  Slow ones (GATT results,
    // blocking captures) fall back to a chunked response that the server
    // re-polls until the job is done.
    for (int i = 0; i < 15 && !job->done.load(std::memory_order_acquire); ++i) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    AsyncWebServerResponse* resp;
    if (job->done.load(std::memory_order_acquire)) {
        resp = req->beginResponse(200, "application/json", job->response.c_str());
    } else {
        resp = req->beginChunkedResponse(
            "application/json",
            [job](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                if (!job->done.load(std::memory_order_acquire)) return RESPONSE_TRY_AGAIN;
                const std::string& r = job->response;
                if (index >= r.size()) return 0;
                size_t n = r.size() - index;
                if (n > maxLen) n = maxLen;
                memcpy(buf, r.data() + index, n);
                return n;
            });
    }
    if (!out.newSessionId.empty()) resp->addHeader(kSessionHeader, out.newSessionId.c_str());
    req->send(resp);
}

void McpHttpTransport::handleDelete(AsyncWebServerRequest* req) {
    const int status = endSession(headerValue(req, "Authorization").c_str(),
                                  headerValue(req, kSessionHeader).c_str());
    if (status == 204) {
        req->send(204);
        return;
    }
    sendJsonError(req, status, status == 401 ? "Unauthorized" : "Unknown or expired session");
}

void McpHttpTransport::pump() {
    // Release server state of sessions that went idle.
    std::vector<uint8_t> expired;
    {
        std::lock_guard<std::mutex> lock(mu_);
        expired = sessions_.expire(millis());
    }
    for (uint8_t cid : expired) server_.removeClient(cid);

    if (pinned_) {
        uint32_t mask = 0;
        for (uint32_t i = 0; i < 32; ++i)
            if (pinned_(static_cast<uint8_t>(MCP_HTTP_CLIENT_BASE + i))) mask |= 1u << i;
        pinnedMask_.store(mask);
    }

    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (queue_.empty()) break;
            job = queue_.front();
            queue_.pop_front();
        }
        switch (job->kind) {
            case Job::Kind::Release:
                server_.removeClient(job->clientId);
                break;
            case Job::Kind::Admit: {
                auto admission = server_.admitClient(job->clientId, job->authorization, "");
                if (admission != MCPServer::Admission::Accepted) {
                    {
                        std::lock_guard<std::mutex> lock(mu_);
                        sessions_.remove(job->sessionId);
                    }
                    JsonDocument doc;
                    doc["jsonrpc"] = "2.0";
                    doc["id"]      = nullptr;
                    doc["error"]["code"] = auth::ERR_UNAUTHORIZED;
                    doc["error"]["message"] =
                        admission == MCPServer::Admission::TooManyClients
                            ? "Too many MCP clients" : "Unauthorized";
                    serializeJson(doc, job->response);
                    break;
                }
                job->response = server_.processMessage(job->clientId, job->body);
                break;
            }
            default: {
                job->response = server_.processMessage(job->clientId, job->body);
                std::lock_guard<std::mutex> lock(mu_);
                sessions_.touch(job->clientId, millis());
                break;
            }
        }
        job->body.clear();
        job->body.shrink_to_fit();
        job->done.store(true, std::memory_order_release);
    }
}

} // namespace mcp

#endif
