#include "AuthPolicy.h"
#include <cctype>

namespace mcp {
namespace auth {

const char* accessName(Access a) {
    switch (a) {
        case Access::Full:     return "full";
        case Access::ReadOnly: return "read-only";
        default:               return "none";
    }
}

static std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string extractBearer(const std::string& header) {
    std::string h = trim(header);
    static const char kScheme[] = "bearer";
    const size_t n = sizeof(kScheme) - 1;
    if (h.size() <= n) return std::string();
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(h[i])) != kScheme[i]) return std::string();
    }
    if (!std::isspace(static_cast<unsigned char>(h[n]))) return std::string();
    return trim(h.substr(n));
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string percentDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hexVal(s[i + 1]);
            int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        // '+' is kept literally: tokens are not form-encoded text.
        out.push_back(s[i]);
    }
    return out;
}

std::string extractQueryToken(const std::string& url) {
    size_t q = url.find('?');
    if (q == std::string::npos) return std::string();
    size_t pos = q + 1;
    while (pos <= url.size()) {
        size_t amp = url.find('&', pos);
        if (amp == std::string::npos) amp = url.size();
        std::string pair = url.substr(pos, amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.compare(0, eq, "token") == 0) {
            return percentDecode(pair.substr(eq + 1));
        }
        pos = amp + 1;
    }
    return std::string();
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
    const size_t n = a.size() > b.size() ? a.size() : b.size();
    unsigned diff = static_cast<unsigned>(a.size() ^ b.size());
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned>(ca ^ cb);
    }
    return diff == 0;
}

Access authenticate(const Credentials& creds, const std::string& header,
                    const std::string& url) {
    if (creds.fullToken.empty()) {
        return creds.allowUnauthenticated ? Access::Full : Access::None;
    }
    std::string token = extractBearer(header);
    if (token.empty()) token = extractQueryToken(url);
    if (token.empty()) return Access::None;
    if (constantTimeEquals(token, creds.fullToken)) return Access::Full;
    if (!creds.readToken.empty() && constantTimeEquals(token, creds.readToken))
        return Access::ReadOnly;
    return Access::None;
}

// Methods a read-only token may call.  Anything not listed (state-changing
// methods and every method added later) requires full control.
static const char* const kReadOnlyMethods[] = {
    "initialize", "ping", "notifications/initialized",
    "resources/list", "resources/read", "resources/subscribe", "resources/unsubscribe",
    "resources/templates/list",
    "system/features",
    // sensors/i2c/scan is NOT here: re-probing writes init commands to the
    // devices on the bus.
    "sensors/read",
    "metrics/list", "metrics/get", "metrics/history",
    "logs/query",
    "bus/history/read", "bus/history/config/get",
    // Only BLE calls that leave shared state alone.  ble/scan starts the
    // shared scanner, ble/scan/stop, ble/scan/results and ble/scan/payloads
    // stop it, and ble/connect, ble/services/*, ble/disconnect talk to
    // third-party devices: all need full control.
    "ble/scan/poll", "ble/connect/results",
};

bool methodAllowed(Access access, const std::string& method) {
    if (access == Access::Full) return true;
    if (access == Access::None) return false;
    for (const char* m : kReadOnlyMethods) {
        if (method == m) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// RateLimiter
// ---------------------------------------------------------------------------

RateLimiter::RateLimiter(uint32_t capacity, uint32_t refillPerSec)
    : capacity_(capacity ? capacity : 1),
      refillPerSec_(refillPerSec ? refillPerSec : 1),
      milliTokens_(capacity_ * 1000u) {}

void RateLimiter::refill(uint32_t nowMs) {
    if (!started_) { started_ = true; lastMs_ = nowMs; return; }
    uint32_t elapsed = nowMs - lastMs_;  // wrap-safe
    lastMs_ = nowMs;
    uint64_t add = static_cast<uint64_t>(elapsed) * refillPerSec_;  // milli-tokens
    uint64_t cap = static_cast<uint64_t>(capacity_) * 1000u;
    uint64_t now = milliTokens_ + add;
    milliTokens_ = static_cast<uint32_t>(now > cap ? cap : now);
}

bool RateLimiter::allow(uint32_t nowMs) {
    refill(nowMs);
    if (milliTokens_ >= 1000u) {
        milliTokens_ -= 1000u;
        retryAfterMs_ = 0;
        return true;
    }
    uint32_t missing = 1000u - milliTokens_;
    retryAfterMs_ = (missing + refillPerSec_ - 1) / refillPerSec_;
    return false;
}

// ---------------------------------------------------------------------------
// FailureLockout
// ---------------------------------------------------------------------------

FailureLockout::FailureLockout(uint32_t threshold, uint32_t baseMs, uint32_t maxMs)
    : threshold_(threshold ? threshold : 1), baseMs_(baseMs), maxMs_(maxMs) {}

bool FailureLockout::locked(uint32_t nowMs) const {
    if (!lockActive_) return false;
    return static_cast<int32_t>(lockedUntil_ - nowMs) > 0;
}

uint32_t FailureLockout::retryAfterMs(uint32_t nowMs) const {
    return locked(nowMs) ? lockedUntil_ - nowMs : 0;
}

void FailureLockout::recordFailure(uint32_t nowMs) {
    ++failures_;
    if (failures_ < threshold_) return;
    uint32_t extra = failures_ - threshold_;
    uint64_t dur = baseMs_;
    for (uint32_t i = 0; i < extra && dur < maxMs_; ++i) dur *= 2;
    if (dur > maxMs_) dur = maxMs_;
    lockedUntil_ = nowMs + static_cast<uint32_t>(dur);
    lockActive_  = true;
}

void FailureLockout::recordSuccess() {
    failures_   = 0;
    lockActive_ = false;
}

// ---------------------------------------------------------------------------
// LockoutTable
// ---------------------------------------------------------------------------

LockoutTable::LockoutTable(size_t capacity, uint32_t threshold, uint32_t baseMs,
                           uint32_t maxMs)
    : capacity_(capacity ? capacity : 1), threshold_(threshold), baseMs_(baseMs),
      maxMs_(maxMs) {}

LockoutTable::Entry* LockoutTable::find(uint32_t key) {
    for (auto& e : entries_) if (e.key == key) return &e;
    return nullptr;
}

bool LockoutTable::locked(uint32_t key, uint32_t nowMs) {
    Entry* e = find(key);
    return e && e->lockout.locked(nowMs);
}

uint32_t LockoutTable::retryAfterMs(uint32_t key, uint32_t nowMs) {
    Entry* e = find(key);
    return e ? e->lockout.retryAfterMs(nowMs) : 0;
}

void LockoutTable::recordFailure(uint32_t key, uint32_t nowMs) {
    Entry* e = find(key);
    if (!e) {
        if (entries_.size() >= capacity_) {
            // Evict the least recently used entry, preferring unlocked ones so
            // a flood of new sources cannot cheaply free a locked-out one.
            size_t victim = entries_.size();
            for (size_t i = 0; i < entries_.size(); ++i) {
                if (entries_[i].lockout.locked(nowMs)) continue;
                if (victim == entries_.size() || entries_[i].lastMs < entries_[victim].lastMs)
                    victim = i;
            }
            if (victim == entries_.size()) {
                victim = 0;
                for (size_t i = 1; i < entries_.size(); ++i)
                    if (entries_[i].lastMs < entries_[victim].lastMs) victim = i;
            }
            entries_.erase(entries_.begin() + victim);
        }
        entries_.push_back(Entry{key, FailureLockout(threshold_, baseMs_, maxMs_), nowMs});
        e = &entries_.back();
    }
    e->lastMs = nowMs;
    e->lockout.recordFailure(nowMs);
}

void LockoutTable::recordSuccess(uint32_t key) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->key == key) { entries_.erase(it); return; }
    }
}

// ---------------------------------------------------------------------------

std::string generateToken(const std::function<uint32_t()>& rnd, size_t bytes) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    uint32_t word = 0;
    for (size_t i = 0; i < bytes; ++i) {
        if (i % 4 == 0) word = rnd();
        uint8_t b = static_cast<uint8_t>(word & 0xFF);
        word >>= 8;
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

} // namespace auth
} // namespace mcp
