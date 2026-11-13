#pragma once

// MCP transport security policy (issue #12).
//
// Pure logic, no platform dependencies, unit tested natively (test_auth_policy):
//
//   * token extraction from `Authorization: Bearer <t>` or `?token=<t>` (for
//     browser WebSocket clients, which cannot set headers);
//   * constant-time token comparison;
//   * access levels: a FULL-control token and an optional READ-ONLY token;
//   * per-method authorization (read-only clients get a whitelist, everything
//     else — including methods added later — needs full control);
//   * per-source failed-login lockout (LockoutTable);
//   * a per-client token-bucket request limiter;
//   * a failed-login lockout with exponential backoff (OTA Basic Auth);
//   * random token generation.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mcp {
namespace auth {

// JSON-RPC application error codes (next to the -3201x feature codes).
constexpr int ERR_UNAUTHORIZED = -32020;  // no/invalid credentials, or not allowed for this token
constexpr int ERR_RATE_LIMITED = -32021;  // per-client request rate exceeded

// WebSocket close codes (application range 4000-4999).
constexpr uint16_t WS_CLOSE_UNAUTHORIZED     = 4401;
constexpr uint16_t WS_CLOSE_TOO_MANY_CLIENTS = 4429;

enum class Access { None, ReadOnly, Full };
const char* accessName(Access a);

struct Credentials {
    std::string fullToken;              // empty = not configured
    std::string readToken;              // empty = no read-only token
    bool        allowUnauthenticated = false;  // MCP_ALLOW_UNAUTHENTICATED lab builds
};

// "Bearer abc" (scheme case-insensitive, surrounding spaces ignored) -> "abc";
// anything else -> "".
std::string extractBearer(const std::string& authorizationHeader);

// "/?token=abc&x=1" -> "abc" (percent-decoded); "" when absent.
std::string extractQueryToken(const std::string& url);

// Length-independent-time comparison of secrets.  Compares every byte of the
// longer input so timing does not reveal the matching prefix.
bool constantTimeEquals(const std::string& a, const std::string& b);

// Decide the access level of a new connection.  Header wins over query.
//   no full token configured: allowUnauthenticated ? Full : None
//   token == fullToken -> Full; token == readToken -> ReadOnly; else None
Access authenticate(const Credentials& creds, const std::string& authorizationHeader,
                    const std::string& url);

// Is `method` callable with `access`?  Full: everything.  ReadOnly: the
// observation whitelist (protocol, resources, sensors, metrics, logs/query,
// bus history reads, BLE scanning and status).  None: nothing.
bool methodAllowed(Access access, const std::string& method);

// Token bucket: `capacity` burst, refilled at `refillPerSec`.
class RateLimiter {
public:
    RateLimiter(uint32_t capacity = 20, uint32_t refillPerSec = 10);
    // Consume one token at `nowMs`.  On refusal, retryAfterMs() says when the
    // next token is available.
    bool allow(uint32_t nowMs);
    uint32_t retryAfterMs() const { return retryAfterMs_; }

private:
    void refill(uint32_t nowMs);
    uint32_t capacity_;
    uint32_t refillPerSec_;
    uint32_t milliTokens_;   // tokens * 1000, for sub-token refill precision
    uint32_t lastMs_ = 0;
    bool     started_ = false;
    uint32_t retryAfterMs_ = 0;
};

// Lock out authentication after `threshold` consecutive failures for
// baseMs, doubling per further failure, capped at maxMs.  A success resets.
class FailureLockout {
public:
    FailureLockout(uint32_t threshold = 5, uint32_t baseMs = 30000,
                   uint32_t maxMs = 300000);
    bool     locked(uint32_t nowMs) const;
    uint32_t retryAfterMs(uint32_t nowMs) const;
    void     recordFailure(uint32_t nowMs);
    void     recordSuccess();
    uint32_t failures() const { return failures_; }

private:
    uint32_t threshold_, baseMs_, maxMs_;
    uint32_t failures_ = 0;
    uint32_t lockedUntil_ = 0;
    bool     lockActive_ = false;
};

// One FailureLockout per source (e.g. client IPv4 address), so a host that
// keeps failing locks out only itself.  Bounded: at most `capacity` sources
// are tracked.  Entries are never evicted while they could matter (only
// once unlocked and quiet for longer than maxMs); when the table is full,
// all untracked sources share one overflow lockout, so an attacker cannot
// gain guesses by rotating through more addresses than the table holds.
class LockoutTable {
public:
    LockoutTable(size_t capacity = 8, uint32_t threshold = 5, uint32_t baseMs = 30000,
                 uint32_t maxMs = 300000);
    bool     locked(uint32_t key, uint32_t nowMs);
    uint32_t retryAfterMs(uint32_t key, uint32_t nowMs);
    void     recordFailure(uint32_t key, uint32_t nowMs);
    void     recordSuccess(uint32_t key);
    size_t   size() const { return entries_.size(); }

private:
    struct Entry {
        uint32_t       key;
        FailureLockout lockout;
        uint32_t       lastMs;
    };
    Entry* find(uint32_t key);
    void   purgeStale(uint32_t nowMs);
    size_t   capacity_;
    uint32_t threshold_, baseMs_, maxMs_;
    std::vector<Entry> entries_;
    FailureLockout     overflow_;
};

// `bytes` random bytes from `rnd` rendered as lower-case hex (2*bytes chars).
std::string generateToken(const std::function<uint32_t()>& rnd, size_t bytes = 16);

} // namespace auth
} // namespace mcp
