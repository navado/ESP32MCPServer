#include "McpHttpSession.h"
#include <cctype>

namespace mcp {
namespace http {

SessionTable::SessionTable(size_t capacity, uint32_t idleMs)
    : slots_(capacity ? capacity : 1), idleMs_(idleMs) {}

const Session* SessionTable::create(auth::Access access, uint32_t nowMs,
                                    const std::function<uint32_t()>& rnd) {
    expire(nowMs);
    for (size_t i = 0; i < slots_.size(); ++i) {
        Slot& sl = slots_[i];
        if (sl.used) continue;
        sl.used         = true;
        sl.s.id         = auth::generateToken(rnd, 16);
        sl.s.clientId   = static_cast<uint8_t>(MCP_HTTP_CLIENT_BASE + i);
        sl.s.access     = access;
        sl.s.lastUsedMs = nowMs;
        return &sl.s;
    }
    return nullptr;
}

const Session* SessionTable::find(const std::string& id, uint32_t nowMs) {
    if (id.empty()) return nullptr;
    for (auto& sl : slots_) {
        if (!sl.used || !auth::constantTimeEquals(sl.s.id, id)) continue;
        if ((uint32_t)(nowMs - sl.s.lastUsedMs) > idleMs_) return nullptr;  // expired
        sl.s.lastUsedMs = nowMs;
        return &sl.s;
    }
    return nullptr;
}

int SessionTable::remove(const std::string& id) {
    for (auto& sl : slots_) {
        if (sl.used && auth::constantTimeEquals(sl.s.id, id)) {
            sl.used = false;
            return sl.s.clientId;
        }
    }
    return -1;
}

std::vector<uint8_t> SessionTable::expire(uint32_t nowMs) {
    std::vector<uint8_t> gone;
    for (auto& sl : slots_) {
        if (sl.used && (uint32_t)(nowMs - sl.s.lastUsedMs) > idleMs_) {
            sl.used = false;
            gone.push_back(sl.s.clientId);
        }
    }
    return gone;
}

size_t SessionTable::size() const {
    size_t n = 0;
    for (const auto& sl : slots_) n += sl.used ? 1 : 0;
    return n;
}

std::string hostOnly(const std::string& in) {
    std::string s = in;
    size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    size_t slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);
    // Strip userinfo.
    size_t at = s.rfind('@');
    if (at != std::string::npos) s = s.substr(at + 1);
    if (!s.empty() && s[0] == '[') {
        size_t close = s.find(']');
        if (close != std::string::npos) s = s.substr(0, close + 1);
    } else {
        size_t colon = s.find(':');
        if (colon != std::string::npos) s = s.substr(0, colon);
    }
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool originAllowed(const std::string& origin, const std::string& host) {
    if (origin.empty()) return true;           // non-browser client
    if (origin == "null") return false;        // sandboxed / file:// pages
    std::string o = hostOnly(origin);
    std::string h = hostOnly(host);
    return !o.empty() && o == h;
}

} // namespace http
} // namespace mcp
