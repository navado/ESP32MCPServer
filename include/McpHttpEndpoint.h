#pragma once

// Transport-neutral side of the MCP Streamable HTTP endpoint (issue #13,
// shared with the TLS server of issue #23).  Kept free of any web server
// header: ESPAsyncWebServer's HTTP_GET/HTTP_POST enum clashes with the one
// esp_http_server uses, so the two servers must never meet in one file.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace mcp {

// One queued request, executed on the MCP task.
struct McpHttpJob {
    enum class Kind { Message, Admit, Release };
    Kind         kind = Kind::Message;
    uint8_t      clientId = 0;
    std::string  body;
    std::string  authorization;   // for Admit
    std::string  sessionId;       // for Admit failure cleanup
    std::string  response;
    std::atomic<bool> done{false};
};

struct McpPostRequest {
    const char* body = nullptr;     // NUL-terminated, or nullptr
    size_t      contentLength = 0;
    std::string origin, host, authorization, sessionId;
};

struct McpPostOutcome {
    int         status = 200;       // != 200: answer with mcpJsonError(error)
    std::string error;
    bool        expectsReply = false;   // false: 202, no body
    std::string newSessionId;           // Mcp-Session-Id to return
    std::shared_ptr<McpHttpJob> job;    // wait for job->done, send job->response
};

class McpHttpEndpoint {
public:
    virtual ~McpHttpEndpoint() = default;
    virtual McpPostOutcome beginPost(const McpPostRequest& req) = 0;
    // DELETE: 204 ended, 401, or 404 unknown session.
    virtual int endSession(const std::string& authorization, const std::string& sessionId) = 0;
};

// {"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":...}}
std::string mcpJsonError(const char* message);

} // namespace mcp
