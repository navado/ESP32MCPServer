#pragma once

// MCP Streamable HTTP transport (issue #13) on the existing AsyncWebServer.
//
//   POST   /mcp   one JSON-RPC message -> application/json response
//                 (202 Accepted for notifications)
//   GET    /mcp   405 — no server-initiated SSE stream is offered
//   DELETE /mcp   end the session named by Mcp-Session-Id
//
// Every request needs `Authorization: Bearer <token>` (same tokens as the
// WebSocket, issue #12) and, from browsers, a same-host Origin.  initialize
// creates a session (Mcp-Session-Id response header) that later requests
// must send.  Sessions count against the MCP client limit and expire after
// 10 minutes idle.
//
// Threading: AsyncWebServer callbacks run on the AsyncTCP task, but
// MCPServer and every method handler belong to the MCP task.  Requests are
// therefore queued and executed by pump() on the MCP task; the HTTP
// response is completed when the result is ready.

#include "board_config.h"

#if BOARD_HAS_WIFI && !defined(NATIVE_TEST)

#include "AuthPolicy.h"
#include "MCPServer.h"
#include "McpHttpSession.h"
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace mcp {

class McpHttpTransport {
public:
    explicit McpHttpTransport(MCPServer& server) : server_(server) {}

    void setCredentials(const auth::Credentials& creds) { creds_ = creds; }
    void registerRoutes(AsyncWebServer& web);

    // Run queued requests and expire idle sessions.  MCP task only.
    void pump();

    static constexpr size_t   MAX_BODY_BYTES = 16 * 1024;
    static constexpr size_t   MAX_QUEUED     = 8;

private:
    struct Job {
        enum class Kind { Message, Admit, Release };
        Kind         kind = Kind::Message;
        uint8_t      clientId = 0;
        std::string  body;
        std::string  authorization;   // for Admit
        std::string  sessionId;       // for Admit failure cleanup
        std::string  response;
        std::atomic<bool> done{false};
    };

    void handlePost(AsyncWebServerRequest* req);
    void handleDelete(AsyncWebServerRequest* req);
    bool enqueue(const std::shared_ptr<Job>& job);
    void sendJsonError(AsyncWebServerRequest* req, int status, const char* message);

    MCPServer&        server_;
    auth::Credentials creds_;
    std::mutex        mu_;          // guards queue_ and sessions_
    std::deque<std::shared_ptr<Job>> queue_;
    http::SessionTable sessions_;
};

} // namespace mcp

#endif
