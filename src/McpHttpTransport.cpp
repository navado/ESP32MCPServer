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

void McpHttpTransport::sendJsonError(AsyncWebServerRequest* req, int status,
                                     const char* message) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"]      = nullptr;
    doc["error"]["code"]    = -32600;
    doc["error"]["message"] = message;
    String out;
    serializeJson(doc, out);
    AsyncWebServerResponse* r = req->beginResponse(status, "application/json", out);
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

void McpHttpTransport::handlePost(AsyncWebServerRequest* req) {
    const char* body = static_cast<const char*>(req->_tempObject);
    if (!body) {
        sendJsonError(req, req->contentLength() > MAX_BODY_BYTES ? 413 : 400,
                      req->contentLength() > MAX_BODY_BYTES ? "Request too large"
                                                             : "Missing JSON-RPC body");
        return;
    }

    // DNS-rebinding protection: a browser page from another host must not
    // reach the device.
    if (!http::originAllowed(headerValue(req, "Origin").c_str(), req->host().c_str())) {
        sendJsonError(req, 403, "Forbidden origin");
        return;
    }

    // Bearer token on every request (same tokens as the WebSocket).
    const String authz = headerValue(req, "Authorization");
    const auth::Access access = auth::authenticate(creds_, authz.c_str(), "");
    if (access == auth::Access::None) {
        sendJsonError(req, 401, "Unauthorized");
        return;
    }

    // Peek at the message: initialize starts a session; notifications and
    // responses get 202 with no body.
    JsonDocument filter;
    filter["method"] = true;
    filter["id"]     = true;
    JsonDocument peek;
    if (deserializeJson(peek, body, DeserializationOption::Filter(filter))) {
        sendJsonError(req, 400, "Parse error");
        return;
    }
    const bool isInitialize = peek["method"].is<const char*>() &&
                              strcmp(peek["method"].as<const char*>(), "initialize") == 0;
    const bool expectsReply = peek["method"].is<const char*>() && !peek["id"].isNull();

    auto job = std::make_shared<Job>();
    job->body = body;
    String newSessionId;
    const String sid = headerValue(req, kSessionHeader);

    if (isInitialize && sid.isEmpty()) {
        std::lock_guard<std::mutex> lock(mu_);
        const http::Session* s =
            sessions_.create(access, millis(), []() { return esp_random(); });
        if (!s) {
            sendJsonError(req, 503, "Too many MCP sessions");
            return;
        }
        newSessionId       = s->id.c_str();
        job->kind          = Job::Kind::Admit;
        job->clientId      = s->clientId;
        job->authorization = authz.c_str();
        job->sessionId     = s->id;
    } else {
        if (sid.isEmpty()) {
            sendJsonError(req, 400, "Missing Mcp-Session-Id");
            return;
        }
        std::lock_guard<std::mutex> lock(mu_);
        const http::Session* s = sessions_.find(sid.c_str(), millis());
        if (!s) {
            // Spec: 404 tells the client to start a new session.
            sendJsonError(req, 404, "Unknown or expired session");
            return;
        }
        if (s->access != access) {
            sendJsonError(req, 401, "Token does not match the session");
            return;
        }
        job->clientId = s->clientId;
    }

    if (!enqueue(job)) {
        sendJsonError(req, 503, "Server busy");
        return;
    }

    if (!expectsReply) {
        req->send(202);
        return;
    }

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
    if (newSessionId.length()) resp->addHeader(kSessionHeader, newSessionId);
    req->send(resp);
}

void McpHttpTransport::handleDelete(AsyncWebServerRequest* req) {
    const String authz = headerValue(req, "Authorization");
    if (auth::authenticate(creds_, authz.c_str(), "") == auth::Access::None) {
        sendJsonError(req, 401, "Unauthorized");
        return;
    }
    const String sid = headerValue(req, kSessionHeader);
    int clientId;
    {
        std::lock_guard<std::mutex> lock(mu_);
        clientId = sessions_.remove(sid.c_str());
    }
    if (clientId < 0) {
        sendJsonError(req, 404, "Unknown or expired session");
        return;
    }
    auto job = std::make_shared<Job>();
    job->kind     = Job::Kind::Release;
    job->clientId = static_cast<uint8_t>(clientId);
    enqueue(job);  // always accepted for Release
    req->send(204);
}

void McpHttpTransport::pump() {
    // Release server state of sessions that went idle.
    std::vector<uint8_t> expired;
    {
        std::lock_guard<std::mutex> lock(mu_);
        expired = sessions_.expire(millis());
    }
    for (uint8_t cid : expired) server_.removeClient(cid);

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
            default:
                job->response = server_.processMessage(job->clientId, job->body);
                break;
        }
        job->body.clear();
        job->body.shrink_to_fit();
        job->done.store(true, std::memory_order_release);
    }
}

} // namespace mcp

#endif
