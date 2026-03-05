#pragma once

#include "BusReader.h"   // BusReadResult, CANFrame, ParsedNMEA, NMEA2000Data, OBDIIData
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace mcp {

// ---------------------------------------------------------------------------
// BusHistoryConfig
//
// User-facing configuration.  Any per-type count of 0 means "auto-compute
// from ramBudgetBytes".  ramBudgetBytes = 0 means "use (freeHeap - margin)".
// The struct is persisted to LittleFS as JSON so settings survive reboots.
// ---------------------------------------------------------------------------
struct BusHistoryConfig {
    uint32_t canFrameCount     = 0;  // CAN frames to buffer        (0 = auto)
    uint32_t nmeaLineCount     = 0;  // NMEA 0183 lines to buffer   (0 = auto)
    uint32_t nmea2000Count     = 0;  // NMEA 2000 messages to buffer (0 = auto)
    uint32_t obdiiCount        = 0;  // OBD-II records to buffer    (0 = auto)
    uint32_t ramBudgetBytes    = 0;  // total byte budget; 0 = freeHeap - margin
    uint32_t safetyMarginBytes = 65536; // bytes always kept free (64 KB default)
};

// ---------------------------------------------------------------------------
// RingBuffer<T>
//
// Fixed-capacity ring buffer that overwrites the oldest entry when full.
// All public methods are thread-safe.  snapshot() returns entries
// oldest → newest so callers racing with a concurrent push() always see a
// consistent, monotonically-ordered view.
// ---------------------------------------------------------------------------
template<typename T>
class RingBuffer {
public:
    RingBuffer()  = default;
    ~RingBuffer() = default;

    // Non-copyable, non-movable (owns a mutex).
    RingBuffer(const RingBuffer&)            = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // (Re-)allocate to cap entries.  Clears all existing data.
    void resize(size_t cap) {
        std::lock_guard<std::mutex> lk(mu_);
        buf_.clear();
        buf_.resize(cap);
        head_  = 0;
        count_ = 0;
        cap_   = cap;
    }

    // Number of slots allocated.
    size_t capacity() const { return cap_; }

    // Number of entries currently stored.
    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return count_;
    }

    // Push an entry.  Overwrites the oldest slot when full.
    void push(const T& item) {
        if (cap_ == 0) return;
        std::lock_guard<std::mutex> lk(mu_);
        buf_[head_] = item;
        head_ = (head_ + 1) % cap_;
        if (count_ < cap_) ++count_;
    }

    // Return a copy of all stored entries, oldest first.
    // Safe to call concurrently with push().
    std::vector<T> snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<T> out;
        if (count_ == 0 || cap_ == 0) return out;
        out.reserve(count_);
        size_t start = (cap_ + head_ - count_) % cap_;
        for (size_t i = 0; i < count_; ++i) {
            out.push_back(buf_[(start + i) % cap_]);
        }
        return out;
    }

    // Remove all stored entries (keeps allocation intact).
    void clear() {
        std::lock_guard<std::mutex> lk(mu_);
        head_  = 0;
        count_ = 0;
    }

private:
    mutable std::mutex mu_;
    std::vector<T>     buf_;
    size_t             head_  = 0;
    size_t             count_ = 0;
    size_t             cap_   = 0;
};

// ---------------------------------------------------------------------------
// BusHistory — singleton history store for all four bus message types.
//
// Typical lifecycle:
//   BUS_HISTORY.begin();                  // after LittleFS.begin()
//   BUS_HISTORY.feed(busReadResult);      // from any bus-reading code
//   auto frames = BUS_HISTORY.snapshotCAN(); // oldest-first
//   BUS_HISTORY.setConfig(cfg);           // persist + reallocate
// ---------------------------------------------------------------------------
class BusHistory {
public:
    static BusHistory& getInstance() {
        static BusHistory inst;
        return inst;
    }

    // Load persisted config from LittleFS and allocate ring buffers.
    // Must be called after LittleFS.begin().
    void begin();

    // Return the current user-facing configuration (as stored, not resolved).
    BusHistoryConfig getConfig() const;

    // Update configuration, persist to LittleFS, clear all buffers, and
    // reallocate.  Triggers a call to announceCapabilityChange() if a
    // discovery callback has been registered via setAnnounceCb().
    void setConfig(const BusHistoryConfig& cfg);

    // Optional: register a callback invoked after setConfig() so the caller
    // can trigger a discovery re-announcement without a hard dependency.
    void setAnnounceCb(std::function<void()> cb) { announceCb_ = std::move(cb); }

    // Feed all data from a BusReadResult into the appropriate ring buffers.
    void feed(const BusReadResult& result);

    // Individual push methods (also callable from external code).
    void pushCAN     (const CANFrame&      f);
    void pushNMEA    (const std::string&   line);
    void pushNMEA2000(const NMEA2000Data&  d);
    void pushOBDII   (const OBDIIData&     d);

    // Oldest-first snapshots.
    std::vector<CANFrame>     snapshotCAN()      const;
    std::vector<std::string>  snapshotNMEA()     const;
    std::vector<NMEA2000Data> snapshotNMEA2000() const;
    std::vector<OBDIIData>    snapshotOBDII()    const;

    // Currently allocated capacities (number of messages that fit).
    size_t canCapacity()      const { return canBuf_.capacity(); }
    size_t nmeaCapacity()     const { return nmeaBuf_.capacity(); }
    size_t nmea2000Capacity() const { return nmea2000Buf_.capacity(); }
    size_t obdiiCapacity()    const { return obdiiBuf_.capacity(); }

    // Serialise config + allocated sizes + free-heap into a JSON object
    // (for HTTP GET /bus-history and the discovery broadcast).
    std::string configToJson() const;

    // Serialise an oldest-first snapshot as a complete JSON-RPC 2.0 result.
    // An optional limit caps the number of entries returned (0 = all).
    std::string canSnapshotJson     (uint32_t requestId, uint32_t limit = 0) const;
    std::string nmeaSnapshotJson    (uint32_t requestId, uint32_t limit = 0) const;
    std::string nmea2000SnapshotJson(uint32_t requestId, uint32_t limit = 0) const;
    std::string obdiiSnapshotJson   (uint32_t requestId, uint32_t limit = 0) const;

    // Approximate per-item heap cost (used for RAM budget calculations).
    static constexpr size_t BYTES_PER_CAN      = sizeof(CANFrame);
    static constexpr size_t BYTES_PER_NMEA     = 100; // avg sentence + std::string overhead
    static constexpr size_t BYTES_PER_NMEA2000 = sizeof(NMEA2000Data) + 32;
    static constexpr size_t BYTES_PER_OBDII    = sizeof(OBDIIData)    + 48;

    // Path of the persisted config file in LittleFS.
    static constexpr const char* CONFIG_PATH = "/bus_history.json";

    // Query platform free heap (mockable via NATIVE_TEST).
    static uint32_t freeHeapBytes();

private:
    BusHistory()  = default;
    ~BusHistory() = default;
    BusHistory(const BusHistory&)            = delete;
    BusHistory& operator=(const BusHistory&) = delete;

    BusHistoryConfig         config_;
    std::function<void()>    announceCb_;

    RingBuffer<CANFrame>     canBuf_;
    RingBuffer<std::string>  nmeaBuf_;
    RingBuffer<NMEA2000Data> nmea2000Buf_;
    RingBuffer<OBDIIData>    obdiiBuf_;

    void loadConfig();
    void saveConfig() const;
    void allocateBuffers(const BusHistoryConfig& cfg);

    // Compute per-type capacities from a total byte budget split equally.
    static BusHistoryConfig autoConfig(uint32_t budgetBytes,
                                       uint32_t safetyMarginBytes);

    // JSON helpers — hand-built strings, no ArduinoJson, to avoid heap spikes.
    static std::string escapeJsonString(const std::string& s);
    static void appendSnapshotHeader(std::string& out, uint32_t requestId,
                                     const char* type, size_t capacity, size_t count);
    static void appendSnapshotFooter(std::string& out);
};

} // namespace mcp

// Convenience macro mirrors the METRICS macro convention.
#define BUS_HISTORY mcp::BusHistory::getInstance()
