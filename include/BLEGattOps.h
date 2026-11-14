#pragma once

// Pure helpers for GATT client operations exposed over MCP (issue #14):
// write values, subscribe on demand, and queue notified values for clients.
// No platform dependencies; unit tested natively (test_gatt_ops).

#include "BLEScannerCore.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace mcp {
namespace gattops {

// ATT attribute values are at most 512 bytes (Core spec Vol 3, Part F 3.2.9).
constexpr size_t MAX_WRITE_BYTES = 512;
// Notified values queued for clients between polls / pushes.
constexpr size_t MAX_QUEUED_NOTIFICATIONS = 32;
// Characteristics subscribed on demand at the same time.
constexpr size_t MAX_LIVE_SUBSCRIPTIONS = 8;

// Parse "0a1B", "0a:1b", "0a 1b" or "0x0a1b" into bytes.  Rejects odd digit
// counts, non-hex characters, empty input and values over MAX_WRITE_BYTES.
bool parseHexBytes(const std::string& in, std::vector<uint8_t>& out, std::string& err);

// Does a properties string from gattcore::propertiesString() contain `name`
// as a whole entry ("write" must not match "write-no-response")?
bool hasProperty(const std::string& properties, const char* name);

struct CharRef {
    uint16_t    handle = 0;
    std::string uuid;
    std::string properties;
    std::string serviceUuid;
};

enum class Resolve { Ok, NotFound, Ambiguous };

// Find a characteristic by handle (when handle != 0) or by UUID
// (case-insensitive).  A UUID present in several services is Ambiguous and
// `candidates` lists the handles to choose from.
Resolve resolveCharacteristic(const std::vector<BLEServiceInfo>& services, uint16_t handle,
                              const std::string& uuid, CharRef& out,
                              std::vector<uint16_t>& candidates);

// CCCD value to enable notifications (0x0001) or indications (0x0002); 0 off.
inline uint16_t cccdValue(bool enable, bool indicate) {
    if (!enable) return 0x0000;
    return indicate ? 0x0002 : 0x0001;
}

struct NotifyEvent {
    uint16_t    handle = 0;
    std::string uuid;
    std::string valueHex;
    uint64_t    timestampMs = 0;
};

// Bounded FIFO of notified values; the oldest are dropped (and counted)
// when full so a chatty peripheral cannot grow memory.
class NotifyQueue {
public:
    explicit NotifyQueue(size_t capacity = MAX_QUEUED_NOTIFICATIONS)
        : capacity_(capacity ? capacity : 1) {}
    void push(NotifyEvent e);
    // Remove and return everything queued; `dropped` gets the number of
    // events discarded since the previous drain.
    std::vector<NotifyEvent> drain(uint32_t& dropped);
    size_t size() const { return q_.size(); }
    void clear() { q_.clear(); dropped_ = 0; }

private:
    size_t                  capacity_;
    std::deque<NotifyEvent> q_;
    uint32_t                dropped_ = 0;
};

} // namespace gattops
} // namespace mcp
