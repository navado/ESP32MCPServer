#pragma once

// Boot-time memory plan: decides how the large consumers (bus-history ring
// buffers, BLE advertising capture) share memory, based on what the board
// ACTUALLY has at runtime.  Pure logic, no platform dependencies, so the
// policy is unit tested natively.
//
//   * PSRAM present  -> bus history is budgeted from PSRAM (large allocations
//                        land there), BLE capture gets generous limits, and the
//                        ~150 KB of internal heap stays free for WiFi, BLE and
//                        JSON responses.
//   * no PSRAM, BLE  -> both compete for internal heap: bus history is capped
//                        lower and BLE capture is kept small.  (Measured on an
//                        ESP32-S3 without PSRAM: GATT enumeration of a phone
//                        drove free heap down to ~7 KB with a 160 KB bus-history
//                        budget.)
//   * no PSRAM, no BLE -> legacy behaviour: bus history uses the board cap.
//
// Whether PSRAM support is compiled in (and the octal/quad memory mode) is a
// build-time property; this plan handles everything that can vary at runtime.

#include <cstddef>
#include <cstdint>

namespace mcp {
namespace mem {

enum class MemoryMode { Psram, SharedInternal, Internal };

struct MemoryPlan {
    MemoryMode mode = MemoryMode::Internal;
    bool       busHistoryInPsram = false;
    uint32_t   busHistoryCap = 0;      // cap on the bus-history auto budget; 0 = uncapped
    size_t     bleMaxDevices = 0;      // 0 when BLE is not compiled in
    size_t     bleCaptureBytes = 0;
};

// PSRAM below this is not worth planning around (e.g. mostly in use already).
constexpr uint32_t MIN_USABLE_PSRAM       = 512u * 1024u;
// Bus history budget when it can live in PSRAM: the smaller of 1 MB and a
// quarter of the free PSRAM.
constexpr uint32_t PSRAM_BUS_HISTORY_MAX  = 1024u * 1024u;
// Bus history cap when it shares internal heap with BLE.
constexpr uint32_t SHARED_BUS_HISTORY_CAP = 96u * 1024u;

constexpr size_t BLE_DEVICES_PSRAM   = 64;
constexpr size_t BLE_BYTES_PSRAM     = 48u * 1024u;
constexpr size_t BLE_DEVICES_SHARED  = 24;
constexpr size_t BLE_BYTES_SHARED    = 16u * 1024u;

inline const char* modeName(MemoryMode m) {
    switch (m) {
        case MemoryMode::Psram:          return "psram";
        case MemoryMode::SharedInternal: return "shared-internal";
        default:                         return "internal";
    }
}

// boardBusHistoryCap: the board's BOARD_BUS_HISTORY_HEAP_CAP (0 = uncapped).
inline MemoryPlan planMemory(bool psramFound, uint32_t psramFree, bool bleEnabled,
                             uint32_t boardBusHistoryCap) {
    MemoryPlan p;
    if (psramFound && psramFree >= MIN_USABLE_PSRAM) {
        p.mode = MemoryMode::Psram;
        p.busHistoryInPsram = true;
        uint32_t quarter = psramFree / 4;
        p.busHistoryCap = quarter < PSRAM_BUS_HISTORY_MAX ? quarter : PSRAM_BUS_HISTORY_MAX;
        if (bleEnabled) {
            p.bleMaxDevices = BLE_DEVICES_PSRAM;
            p.bleCaptureBytes = BLE_BYTES_PSRAM;
        }
        return p;
    }
    if (bleEnabled) {
        p.mode = MemoryMode::SharedInternal;
        p.busHistoryCap = (boardBusHistoryCap != 0 && boardBusHistoryCap < SHARED_BUS_HISTORY_CAP)
                              ? boardBusHistoryCap
                              : SHARED_BUS_HISTORY_CAP;
        p.bleMaxDevices = BLE_DEVICES_SHARED;
        p.bleCaptureBytes = BLE_BYTES_SHARED;
        return p;
    }
    p.mode = MemoryMode::Internal;
    p.busHistoryCap = boardBusHistoryCap;
    return p;
}

} // namespace mem
} // namespace mcp
