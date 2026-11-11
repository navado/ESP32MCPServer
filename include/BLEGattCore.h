#pragma once

// Pure GATT state-machine helpers — no BLE-stack dependencies, so they can be
// unit tested natively.  The raw esp_gattc calls live in BLEScanner.cpp; this
// header holds the decision logic (deadlines, transitions, formatting).

#include <cstdint>
#include <cstring>
#include <string>

namespace mcp {
namespace gattcore {

// Lifecycle of the raw esp_gattc client connection.
enum class GattState {
    Idle,         // no connection
    Connecting,   // app registration / open in flight
    Connected,    // open; idle or discovery pending
    Discovering,  // search_service issued, awaiting SEARCH_CMPL_EVT
    Reading,      // one or more characteristic reads in flight
    NotifyWait,   // reads done, collecting notified values
    Closing,      // close issued, awaiting CLOSE_EVT / DISCONNECT_EVT
};

// States in which a usable GATT connection exists.
inline bool gattStateConnected(GattState s) {
    return s == GattState::Connected || s == GattState::Discovering ||
           s == GattState::Reading || s == GattState::NotifyWait;
}

// A deadline measured from a start millis value.  nowMillis() must be
// monotonic (e.g. millis() on ESP32).
inline bool deadlineExpired(uint32_t startMillis, uint32_t timeoutMs,
                            uint32_t nowMillis) {
    return static_cast<uint32_t>(nowMillis - startMillis) >= timeoutMs;
}

// Bounds for a single GATT operation (tunable constants).
constexpr uint32_t READ_OP_TIMEOUT_MS        = 5000;   // per characteristic read
constexpr uint32_t DISCOVERY_TIMEOUT_MS      = 10000;  // whole service discovery
constexpr uint32_t NOTIFY_CAPTURE_TIMEOUT_MS = 3000;   // capture window
constexpr uint32_t CLOSE_TIMEOUT_MS          = 2000;   // close handshake

// A UUID in the byte layout Bluedroid uses: 16-bit and 32-bit UUIDs are
// scalar values (host-endian, e.g. 0x1800); 128-bit UUIDs are little-endian
// bytes with u128[0] as the LEAST significant byte (matching the Arduino BLE
// library's uuid128 convention).
struct RawUuid {
    uint16_t len = 0;        // 2, 4, or 16
    uint16_t u16 = 0;        // valid when len == 2
    uint32_t u32 = 0;        // valid when len == 4
    uint8_t  u128[16] = {0}; // valid when len == 16
};

// Render a UUID as a canonical string, e.g. "00001800-0000-1000-8000-00805f9b34fb".
inline std::string uuidToString(const RawUuid& u) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    auto hexByte = [&](uint8_t b) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0F]);
    };
    auto hex32 = [&](uint32_t v) {
        hexByte(static_cast<uint8_t>(v >> 24));
        hexByte(static_cast<uint8_t>(v >> 16));
        hexByte(static_cast<uint8_t>(v >> 8));
        hexByte(static_cast<uint8_t>(v));
    };
    if (u.len == 2) {
        hex32(u.u16);
        out += "-0000-1000-8000-00805f9b34fb";
    } else if (u.len == 4) {
        hex32(u.u32);
        out += "-0000-1000-8000-00805f9b34fb";
    } else if (u.len == 16) {
        for (int i = 15; i >= 12; --i) hexByte(u.u128[i]);
        out.push_back('-');
        for (int i = 11; i >= 10; --i) hexByte(u.u128[i]);
        out.push_back('-');
        for (int i = 9; i >= 8; --i) hexByte(u.u128[i]);
        out.push_back('-');
        for (int i = 7; i >= 6; --i) hexByte(u.u128[i]);
        out.push_back('-');
        for (int i = 5; i >= 0; --i) hexByte(u.u128[i]);
    }
    return out;
}

// Render a GATT characteristic properties bitmask as a comma-separated list,
// matching the format documented in the README (read/write/notify/indicate).
inline std::string propertiesString(uint8_t props) {
    std::string out;
    if (props & 0x02) out += "read,";
    if (props & 0x04) out += "write-no-response,";
    if (props & 0x08) out += "write,";
    if (props & 0x10) out += "notify,";
    if (props & 0x20) out += "indicate,";
    if (props & 0x01) out += "broadcast,";
    if (props & 0x80) out += "extended-properties,";
    if (!out.empty()) out.pop_back();
    return out;
}

} // namespace gattcore
} // namespace mcp
