#pragma once

// Streamable HTTP transport — pure session/origin logic (issue #13).
//
// Kept free of platform code so it is unit tested natively
// (test_mcp_http_session); the AsyncWebServer glue lives in
// McpHttpTransport.cpp.  Not thread-safe: the transport serialises access.

#include "AuthPolicy.h"
#include "MCPServer.h"   // MCP_HTTP_CLIENT_BASE
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mcp {
namespace http {

struct Session {
    std::string  id;               // Mcp-Session-Id (32 hex chars)
    uint8_t      clientId = 0;     // MCPServer client id (>= MCP_HTTP_CLIENT_BASE)
    auth::Access access = auth::Access::None;
    uint32_t     lastUsedMs = 0;
};

class SessionTable {
public:
    // `capacity` concurrent sessions; idle sessions expire after `idleMs`.
    SessionTable(size_t capacity = 4, uint32_t idleMs = 10u * 60u * 1000u);

    // Create a session.  Returns nullptr when every slot is taken by a live
    // session.  `rnd` supplies randomness for the id.
    const Session* create(auth::Access access, uint32_t nowMs,
                          const std::function<uint32_t()>& rnd);

    // Live session by id (touches lastUsedMs), or nullptr.
    const Session* find(const std::string& id, uint32_t nowMs);

    // End a session; returns its client id, or -1 if unknown.
    int remove(const std::string& id);

    // Drop idle sessions; returns their client ids (to release server state).
    std::vector<uint8_t> expire(uint32_t nowMs);

    size_t size() const;

private:
    struct Slot {
        bool    used = false;
        Session s;
    };
    std::vector<Slot> slots_;
    uint32_t idleMs_;
};

// Origin validation against DNS rebinding (MCP spec: servers MUST validate
// Origin).  No Origin header (non-browser clients) is allowed; otherwise the
// Origin's host must equal the Host header's host (ports ignored).
bool originAllowed(const std::string& origin, const std::string& host);

// Host part of "http://host:port/..." or "host:port" (lower-case, no port,
// IPv6 brackets kept).
std::string hostOnly(const std::string& hostOrOrigin);

} // namespace http
} // namespace mcp
