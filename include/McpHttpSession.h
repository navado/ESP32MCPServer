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
    // When full, create() may evict the least recently used session if it
    // has been idle for at least `evictIdleMs`.
    SessionTable(size_t capacity = 4, uint32_t idleMs = 10u * 60u * 1000u,
                 uint32_t evictIdleMs = 30u * 1000u);

    // Create a session.  `rnd` supplies randomness for the id.
    //
    // When every slot is in use and `evictedClientId` is given, the least
    // recently used session that is idle for >= evictIdleMs, has no more
    // access than the new one (a read-only token never displaces a
    // full-control session) and passes `mayEvict` (the firmware refuses
    // sessions that own a BLE link, scan or subscription) is replaced, and its
    // client id is stored in *evictedClientId (else -1) so the caller can
    // release its server state first; the evicted client gets 404 on its
    // next request and starts a new session, as the spec requires.  Many
    // clients (the MCP Inspector CLI, a restarted Claude Code) never send
    // DELETE, so without eviction their sessions would hold every slot until
    // the idle timeout.
    //
    // Returns nullptr when no slot can be had.  Slots are never reclaimed
    // silently: an expired or evicted session's client id always comes back
    // to the caller (here or from expire()).
    const Session* create(auth::Access access, uint32_t nowMs,
                          const std::function<uint32_t()>& rnd,
                          int* evictedClientId = nullptr,
                          const std::function<bool(uint8_t clientId)>& mayEvict = nullptr);

    // Mark a session as used now (a request on it just finished), so a
    // long-running request does not leave it looking idle.
    void touch(uint8_t clientId, uint32_t nowMs);

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
    uint32_t evictIdleMs_;
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
