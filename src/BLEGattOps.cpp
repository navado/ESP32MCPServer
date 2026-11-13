#include "BLEGattOps.h"
#include <cctype>

namespace mcp {
namespace gattops {

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseHexBytes(const std::string& in, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    size_t i = 0;
    if (in.size() >= 2 && in[0] == '0' && (in[1] == 'x' || in[1] == 'X')) i = 2;
    int hi = -1;
    for (; i < in.size(); ++i) {
        char c = in[i];
        if (c == ':' || c == ' ' || c == '-') {
            if (hi >= 0) { err = "separator inside a byte"; return false; }
            continue;
        }
        int d = hexDigit(c);
        if (d < 0) { err = "value must be hex"; return false; }
        if (hi < 0) {
            hi = d;
        } else {
            if (out.size() >= MAX_WRITE_BYTES) { err = "value longer than 512 bytes"; return false; }
            out.push_back(static_cast<uint8_t>((hi << 4) | d));
            hi = -1;
        }
    }
    if (hi >= 0) { err = "odd number of hex digits"; return false; }
    if (out.empty()) { err = "value is empty"; return false; }
    return true;
}

bool hasProperty(const std::string& props, const char* name) {
    const std::string n(name);
    size_t pos = 0;
    while (pos <= props.size()) {
        size_t comma = props.find(',', pos);
        if (comma == std::string::npos) comma = props.size();
        if (props.compare(pos, comma - pos, n) == 0 && comma - pos == n.size()) return true;
        pos = comma + 1;
    }
    return false;
}

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Resolve resolveCharacteristic(const std::vector<BLEServiceInfo>& services, uint16_t handle,
                              const std::string& uuid, CharRef& out,
                              std::vector<uint16_t>& candidates) {
    candidates.clear();
    const std::string want = lower(uuid);
    for (const auto& svc : services) {
        for (const auto& chr : svc.characteristics) {
            bool match = handle != 0 ? chr.handle == handle
                                     : (!want.empty() && lower(chr.uuid) == want);
            if (!match) continue;
            if (candidates.empty()) {
                out.handle      = chr.handle;
                out.uuid        = chr.uuid;
                out.properties  = chr.properties;
                out.serviceUuid = svc.uuid;
            }
            candidates.push_back(chr.handle);
        }
    }
    if (candidates.empty()) return Resolve::NotFound;
    if (candidates.size() > 1) return Resolve::Ambiguous;
    return Resolve::Ok;
}

void NotifyQueue::push(NotifyEvent e) {
    if (q_.size() >= capacity_) {
        q_.pop_front();
        ++dropped_;
    }
    q_.push_back(std::move(e));
}

std::vector<NotifyEvent> NotifyQueue::drain(uint32_t& dropped) {
    std::vector<NotifyEvent> out(q_.begin(), q_.end());
    q_.clear();
    dropped  = dropped_;
    dropped_ = 0;
    return out;
}

} // namespace gattops
} // namespace mcp
