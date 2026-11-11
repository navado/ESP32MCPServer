#pragma once

// Pure BLE helper functions — no BLE-stack dependencies, so they can be unit
// tested natively.  Shared by BLEScanner.cpp (device builds) and the native
// test_ble_scanner suite.

#include <cstdint>
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
    bool        isConnectable;
    uint64_t    firstSeen;     // millis() of first report
    // Number of DISTINCT payloads captured for this MAC.
    uint32_t    count;
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
// MAX_PAYLOAD_HEX, so 64 x 128 chars x 12 devices is the pathological worst
// case (~98 KB) and real captures (payloads are <= 62 bytes on air, ambient
// devices rarely broadcast more than a couple of distinct payloads) land at
// a few KB.
//
// The ble/scan/results endpoint serialises ONLY the newest MAX_PAYLOADS_VIEW
// payloads per device (count reports the true distinct total), keeping the
// unfiltered response the proven-safe size even under a flooded radio; the
// full history is available per-MAC via ble/scan/results?mac=.
constexpr size_t MAX_DEVICES       = 12;   // tracked MACs per scan
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

// Record a payload into the MAC-independent registry.  The registry is
// insertion-ordered (first-seen order), deduped on the truncated hex form,
// and bounded to MAX_REGISTRY_PAYLOADS entries (when full, new distinct
// payloads are ignored).  Unlike the per-MAC history it is NOT subject to
// device eviction, so it survives a flood of rotating MACs.  `now` is the
// millis() at observation time; the caller is responsible for locking.
inline void recordRegistryPayload(std::vector<BLEPayloadEntry>& registry,
                                  const std::string& payloadHex, uint64_t now) {
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
    if (registry.size() >= MAX_REGISTRY_PAYLOADS) return;  // registry full
    BLEPayloadEntry e;
    e.payload = stored;
    e.count = 1;
    e.firstSeen = now;
    e.lastSeen = now;
    registry.push_back(e);
}

} // namespace blecore
} // namespace mcp
