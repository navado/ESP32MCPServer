#pragma once

// Pure BLE helper functions — no BLE-stack dependencies, so they can be unit
// tested natively.  Shared by BLEScanner.cpp (device builds) and the native
// test_ble_scanner suite.

#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace mcp {

// One captured BLE advertising report (raw broadcast payload).
struct BLEAdvReport {
    std::string mac;
    std::string name;          // advertised name (may be empty)
    int         rssi;          // dBm (latest)
    // Distinct payloads seen for this MAC, oldest -> newest.  Each entry is the
    // complete raw advertising payload as lowercase hex.  Payload changes are
    // the key signal when reverse-engineering a broadcast format.
    std::vector<std::string> payloadHistory;
    std::string servicesHex;   // advertised 128/32/16-bit service UUIDs, comma-separated
    std::string manufacturer;  // manufacturer data, hex (company id first 2 bytes)
    bool        isConnectable = false;
    uint64_t    firstSeen = 0; // millis() of first report
    uint64_t    lastSeen = 0;  // millis() of latest report (eviction is LRU on this)
    // Number of DISTINCT payloads captured for this MAC.
    uint32_t    count = 0;
    // Advertiser address type, numerically identical to esp_ble_addr_type_t:
    // 0 public, 1 random, 2 RPA-public, 3 RPA-random.  Needed to connect:
    // opening a random-address peer with the public type never succeeds.
    uint8_t     addrType = 0;
};

// One entry of the MAC-independent payload registry: a distinct payload as
// seen across ALL broadcasters during a scan, in first-seen order, with the
// number of times it was observed and its first/last observation times.
// The phone rotating its MAC every ~1s means per-device history never
// accumulates; the registry is the artifact that captures the command surface.
struct BLEPayloadEntry {
    std::string payload;   // truncated hex, dedupe key
    uint32_t    count;     // broadcasts observed (across all MACs)
    uint64_t    firstSeen; // millis() of first observation
    uint64_t    lastSeen;  // millis() of last observation
};

// One GATT characteristic discovered on a connected device.
struct BLECharInfo {
    uint16_t    handle = 0;   // attribute handle (for ble/write, ble/subscribe)
    std::string uuid;
    std::string properties;   // read/write/notify/indicate/...
    std::string valueHex;     // last read / notified value (may be empty)
    std::string valueText;    // printable ASCII of valueHex (may be empty)
};

// One GATT service discovered on a connected device.
struct BLEServiceInfo {
    std::string uuid;
    std::vector<BLECharInfo> characteristics;
};

namespace blecore {

// Bounds for the per-device payload history (tunable constants).
//
// MAX_PAYLOADS (the deep cap) is deliberately generous — it is what makes
// protocol reverse-engineering possible in a single scan: 64 distinct
// payloads covers a full command set (e.g. 10 levels x 2 channels + heater +
// patterns) with margin.  The per-device storage cost is bounded by
// MAX_PAYLOAD_HEX per entry, and the WHOLE capture is bounded by
// MAX_CAPTURE_BYTES (see captureBytes()): when a new device or payload would
// exceed it, the least-recently-heard device is evicted.  That total budget,
// not the per-device caps, is what keeps a flooded radio from exhausting the
// heap, so MAX_DEVICES can be generous.
//
// The ble/scan/results endpoint serialises ONLY the newest MAX_PAYLOADS_VIEW
// payloads per device (count reports the true distinct total), keeping the
// unfiltered response the proven-safe size even under a flooded radio; the
// full history is available per-MAC via ble/scan/results?mac=.
constexpr size_t MAX_DEVICES       = 32;   // default tracked MACs per scan
constexpr size_t MAX_DEVICES_LIMIT = 64;   // upper bound for runtime limits (MemoryPlan)
constexpr size_t MAX_PAYLOADS      = 64;   // distinct payloads per MAC (deep)
constexpr size_t MAX_PAYLOADS_VIEW = 4;    // payloads serialised by the unfiltered endpoint
// Cap on the length of a single stored payload.  Payloads longer than this are
// truncated to MAX_PAYLOAD_BYTES*2 hex chars so the scan history (and the
// JSON serialised by ble/scan/results) stays small even when the radio is
// flooded by chatty broadcasters.
constexpr size_t MAX_PAYLOAD_BYTES = 64;
constexpr size_t MAX_PAYLOAD_HEX   = MAX_PAYLOAD_BYTES * 2;
// Cap on the MAC-independent payload registry: distinct payloads across ALL
// broadcasters in a scan, in first-seen order.  This is the artifact that
// captures a full command surface in one scan despite per-MAC MAC-rotation
// churn (e.g. the phone rotating its MAC every ~1s).  64 covers the whole
// LoveSpouse command vocabulary (~30 commands) with margin.
constexpr size_t MAX_REGISTRY_PAYLOADS = 64;

// Total heap budget for captured device reports (estimated by captureBytes()).
// Sized so the largest unfiltered ble/scan/results response (MAX_DEVICES x
// MAX_PAYLOADS_VIEW payloads) plus its JsonDocument fits comfortably on an
// ESP32-S3 alongside the BLE stack and WebSocket transport.
constexpr size_t MAX_CAPTURE_BYTES = 32 * 1024;
// Approximate per-std::string cost on the ESP32 heap (object + allocation
// header + rounding) used by the capture-size estimate.
constexpr size_t STRING_OVERHEAD = 32;

// Encode raw bytes as lowercase hex ("" for empty input).
inline std::string hexEncode(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

// Render a hex string as printable ASCII (non-printables become '.').
inline std::string hexToAscii(const std::string& hex) {
    std::string out;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        char c = static_cast<char>(strtol(hex.substr(i, 2).c_str(), nullptr, 16));
        if (c >= 0x20 && c <= 0x7E) out.push_back(c);
        else out.push_back('.');
    }
    return out;
}

// Decode a hex string into raw bytes.  Returns false on odd-length input or
// non-hex characters (out is unchanged then).
inline bool hexDecode(const std::string& hex, std::string& out) {
    if (hex.length() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.length() / 2);
    for (size_t i = 0; i < hex.length(); i += 2) {
        int hi = strtol(hex.substr(i, 1).c_str(), nullptr, 16);
        int lo = strtol(hex.substr(i + 1, 1).c_str(), nullptr, 16);
        if ((hex[i] != '0' && hi == 0) || (hex[i + 1] != '0' && lo == 0)) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

// BLE flags AD type (0x01): bit1 LE General Discoverable, bit2 LE Limited
// Discoverable, bit3 BR/EDR Not Supported, bit4 LE+BR/EDR Simultaneous.
// Heuristic: a broadcast is "connectable" when the BR/EDR-Not-Supported bit is
// clear.  A set bit indicates a beacon-style device that advertises BR/EDR
// support only (typically non-connectable over LE).  When the flags AD is
// absent or malformed we optimistically assume connectable.
inline bool payloadIsConnectable(const uint8_t* payload, size_t len) {
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t adLen = payload[i];
        if (adLen == 0) break;
        if (i + 1 + adLen > len) break;
        uint8_t adType = payload[i + 1];
        if (adType == 0x01 && adLen >= 2) {
            return (payload[i + 2] & 0x04) == 0;
        }
        i += adLen + 1;
    }
    return true;
}

// Record a raw payload into a report's per-device payload history.  Identical
// payloads are skipped; distinct payloads are appended oldest -> newest,
// bounded to MAX_PAYLOADS entries (oldest dropped when full), and the report's
// count tracks the number of distinct payloads currently retained.  The seen
// set stays consistent with the history (keyed by the stored form) so an
// evicted payload can be recorded again if it re-broadcasts later.  Payloads
// longer than MAX_PAYLOAD_HEX are truncated before they are stored — and
// deduplicated after truncation — so a chatty broadcaster cannot bloat the
// history or the serialised results.
inline void recordPayload(std::vector<std::string>& history, uint32_t& count,
                          std::map<std::string, bool>& seen,
                          const std::string& payloadHex) {
    if (payloadHex.empty()) return;
    std::string stored = payloadHex;
    if (stored.length() > MAX_PAYLOAD_HEX) {
        stored.resize(MAX_PAYLOAD_HEX);
    }
    if (seen.count(stored)) return;  // duplicate (after truncation) — skip
    seen[stored] = true;
    history.push_back(stored);
    while (history.size() > MAX_PAYLOADS) {  // drop the oldest entry when full
        seen.erase(history.front());
        history.erase(history.begin());
    }
    count = static_cast<uint32_t>(history.size());
}

// ---- Address types --------------------------------------------------------

constexpr uint8_t ADDR_TYPE_PUBLIC     = 0;
constexpr uint8_t ADDR_TYPE_RANDOM     = 1;
constexpr uint8_t ADDR_TYPE_RPA_PUBLIC = 2;
constexpr uint8_t ADDR_TYPE_RPA_RANDOM = 3;

inline const char* addrTypeName(uint8_t t) {
    switch (t) {
        case ADDR_TYPE_PUBLIC:     return "public";
        case ADDR_TYPE_RANDOM:     return "random";
        case ADDR_TYPE_RPA_PUBLIC: return "rpa-public";
        case ADDR_TYPE_RPA_RANDOM: return "rpa-random";
        default:                   return "unknown";
    }
}

// Parse a client-supplied address type ("public", "random", "rpa-public",
// "rpa-random"; case-insensitive).  Returns false for anything else.
inline bool parseAddrType(const std::string& s, uint8_t& out) {
    std::string l = s;
    for (char& c : l) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    for (uint8_t t = ADDR_TYPE_PUBLIC; t <= ADDR_TYPE_RPA_RANDOM; ++t) {
        if (l == addrTypeName(t)) { out = t; return true; }
    }
    return false;
}

// Case-insensitive MAC comparison ("AA:BB:.." == "aa:bb:..").
inline bool macEquals(const std::string& a, const std::string& b) {
    if (a.length() != b.length()) return false;
    for (size_t i = 0; i < a.length(); ++i) {
        if (tolower(static_cast<unsigned char>(a[i])) !=
            tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// Look up the address type a scan recorded for `mac`.  Returns false when the
// MAC was not seen (callers then fall back to public).
inline bool lookupAddrType(const std::vector<BLEAdvReport>& reports,
                           const std::string& mac, uint8_t& out) {
    for (const auto& r : reports) {
        if (macEquals(r.mac, mac)) { out = r.addrType; return true; }
    }
    return false;
}

// Merge a later report (typically the scan response) into a known device:
// fields that were empty in the first advertisement are filled in.  Names,
// service lists and manufacturer data very often arrive ONLY in the scan
// response, so without this they are never captured.
inline void mergeReportFields(BLEAdvReport& known, const std::string& name,
                              const std::string& servicesHex,
                              const std::string& manufacturer) {
    if (known.name.empty() && !name.empty()) known.name = name;
    if (known.servicesHex.empty() && !servicesHex.empty()) known.servicesHex = servicesHex;
    if (known.manufacturer.empty() && !manufacturer.empty()) known.manufacturer = manufacturer;
}

// ---- Capture budget and eviction -------------------------------------------

// Estimated heap footprint of one report.
inline size_t reportBytes(const BLEAdvReport& r) {
    size_t n = sizeof(BLEAdvReport);
    n += r.mac.capacity() + r.name.capacity() + r.servicesHex.capacity() +
         r.manufacturer.capacity() + 4 * STRING_OVERHEAD;
    for (const auto& p : r.payloadHistory) n += p.capacity() + STRING_OVERHEAD;
    // The scanner's per-MAC dedupe set mirrors the history (map node + key).
    n += r.payloadHistory.size() * (48 + STRING_OVERHEAD) + r.mac.capacity();
    return n;
}

inline size_t captureBytes(const std::vector<BLEAdvReport>& reports) {
    size_t n = 0;
    for (const auto& r : reports) n += reportBytes(r);
    return n;
}

// Index of the device to evict: the one heard least recently.  (The previous
// policy evicted the device FIRST seen, which kept dropping devices that were
// still actively advertising while rotating MACs churned through the list.)
// Ties go to the earliest entry.  Returns reports.size() when empty.
// `exclude` (a MAC) is never chosen — used to protect the device that was just
// added or updated while enforcing the budget.
inline size_t lruIndex(const std::vector<BLEAdvReport>& reports,
                       const std::string& exclude = std::string()) {
    size_t idx = reports.size();
    for (size_t i = 0; i < reports.size(); ++i) {
        if (!exclude.empty() && reports[i].mac == exclude) continue;
        if (idx == reports.size() || reports[i].lastSeen < reports[idx].lastSeen) idx = i;
    }
    return idx;
}

// ---- Capture filter (applied when a report is RECORDED) ---------------------

// Unlike the ble/scan/results response filters, these decide what is stored at
// all, so a busy RF environment cannot crowd the device of interest out of the
// capture.  Empty / default fields match everything.
struct CaptureFilter {
    int         minRssi = -128;     // dBm; reports weaker than this are ignored
    std::string manufacturerPrefix; // lowercase hex prefix of manufacturer data
                                    // (company id little-endian, e.g. 0xDEAD -> "adde")
    std::string serviceUuid;        // substring of the advertised service list
    std::string macPrefix;          // lowercase MAC prefix, e.g. "d7:2f"

    bool active() const {
        return minRssi > -128 || !manufacturerPrefix.empty() ||
               !serviceUuid.empty() || !macPrefix.empty();
    }
};

inline std::string toLowerCopy(const std::string& s) {
    std::string l = s;
    for (char& c : l) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return l;
}

// True when a report for a NOT-yet-captured device should be recorded.  Known
// devices are always updated (their scan responses must still merge in).
inline bool captureMatches(const CaptureFilter& f, const std::string& mac, int rssi,
                           const std::string& servicesHex,
                           const std::string& manufacturer) {
    if (rssi < f.minRssi) return false;
    if (!f.macPrefix.empty() &&
        toLowerCopy(mac).compare(0, f.macPrefix.length(), f.macPrefix) != 0) return false;
    if (!f.manufacturerPrefix.empty() &&
        toLowerCopy(manufacturer).compare(0, f.manufacturerPrefix.length(),
                                          f.manufacturerPrefix) != 0) return false;
    if (!f.serviceUuid.empty() &&
        toLowerCopy(servicesHex).find(f.serviceUuid) == std::string::npos) return false;
    return true;
}

// Record a payload into the MAC-independent registry.  The registry is
// insertion-ordered (first-seen order), deduped on the truncated hex form,
// and bounded to MAX_REGISTRY_PAYLOADS entries (when full, new distinct
// payloads are ignored).  Unlike the per-MAC history it is NOT subject to
// device eviction, so it survives a flood of rotating MACs.  `now` is the
// millis() at observation time; the caller is responsible for locking.
inline void recordRegistryPayload(std::vector<BLEPayloadEntry>& registry,
                                  const std::string& payloadHex, uint64_t now,
                                  uint32_t* dropped = nullptr) {
    if (payloadHex.empty()) return;
    std::string stored = payloadHex;
    if (stored.length() > MAX_PAYLOAD_HEX) {
        stored.resize(MAX_PAYLOAD_HEX);
    }
    for (auto& e : registry) {
        if (e.payload == stored) {
            e.count++;
            e.lastSeen = now;
            return;
        }
    }
    if (registry.size() >= MAX_REGISTRY_PAYLOADS) {  // registry full
        // Count what was lost so clients can tell the capture is incomplete
        // instead of the overflow being silent.
        if (dropped != nullptr) ++*dropped;
        return;
    }
    BLEPayloadEntry e;
    e.payload = stored;
    e.count = 1;
    e.firstSeen = now;
    e.lastSeen = now;
    registry.push_back(e);
}

} // namespace blecore
} // namespace mcp
