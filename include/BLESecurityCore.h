#pragma once

// BLE pairing / bonding — pure helpers (issue #24).
//
// Parameter parsing and status names shared by the firmware (BLEScanner,
// ble/pair handlers) and the native tests.  No platform code.

#include <cstdint>
#include <cstdio>
#include <string>

namespace mcp {
namespace blesec {

// SMP IO capabilities, numerically identical to ESP_IO_CAP_*.
enum class IoCap : uint8_t {
    DisplayOnly = 0, DisplayYesNo = 1, KeyboardOnly = 2, None = 3, KeyboardDisplay = 4
};

// "none" | "display" | "display-yesno" | "keyboard" | "keyboard-display"
inline bool parseIoCap(const std::string& s, IoCap& out) {
    if (s == "none")             { out = IoCap::None;            return true; }
    if (s == "display")          { out = IoCap::DisplayOnly;     return true; }
    if (s == "display-yesno")    { out = IoCap::DisplayYesNo;    return true; }
    if (s == "keyboard")         { out = IoCap::KeyboardOnly;    return true; }
    if (s == "keyboard-display") { out = IoCap::KeyboardDisplay; return true; }
    return false;
}

inline const char* ioCapName(IoCap c) {
    switch (c) {
        case IoCap::DisplayOnly:     return "display";
        case IoCap::DisplayYesNo:    return "display-yesno";
        case IoCap::KeyboardOnly:    return "keyboard";
        case IoCap::KeyboardDisplay: return "keyboard-display";
        default:                     return "none";
    }
}

enum class PairState { Idle, Pairing, Paired, Failed };

inline const char* pairStateName(PairState s) {
    switch (s) {
        case PairState::Pairing: return "pairing";
        case PairState::Paired:  return "paired";
        case PairState::Failed:  return "failed";
        default:                 return "idle";
    }
}

constexpr uint32_t PAIR_TIMEOUT_MS = 30000;   // SMP itself times out after 30 s
constexpr uint32_t MAX_PASSKEY     = 999999;  // six decimal digits

// Pairing options from ble/pair params, validated.
struct PairOptions {
    bool     mitm = false;      // require an authenticated (MITM-protected) key
    IoCap    ioCap = IoCap::None;
    bool     hasPasskey = false;
    uint32_t passkey = 0;       // entered for the peer's display, or our static passkey
    bool     confirm = false;   // accept a numeric comparison
};

// Default IO capability: a passkey means we can type it (keyboard); a
// confirmation means we can say yes/no to a number (display-yesno);
// otherwise Just Works (none).
inline IoCap defaultIoCap(bool hasPasskey, bool confirm) {
    if (hasPasskey) return IoCap::KeyboardOnly;
    if (confirm)    return IoCap::DisplayYesNo;
    return IoCap::None;
}

// esp_ble_auth_cmpl_t.fail_reason: Bluedroid reports an SMP pairing-failed
// reason (Core Vol 3, Part H, 3.5.5) as SMP_FAIL_BASE + reason, and a link
// failure as the plain HCI error code.
constexpr uint8_t SMP_FAIL_BASE = 0x43 + 10;   // HCI_ERR_MAX_ERR + 10 (bta_api.h)

inline std::string pairFailReasonName(uint8_t r) {
    static const char* const kSmp[] = {
        nullptr, "passkey-entry-failed", "oob-not-available", "authentication-requirements",
        "confirm-value-failed", "pairing-not-supported", "encryption-key-size",
        "command-not-supported", "unspecified-reason", "repeated-attempts",
        "invalid-parameters", "dhkey-check-failed", "numeric-comparison-failed",
        "br-edr-pairing-in-progress", "cross-transport-key-not-allowed"};
    if (r > SMP_FAIL_BASE && r - SMP_FAIL_BASE < sizeof(kSmp) / sizeof(kSmp[0]))
        return kSmp[r - SMP_FAIL_BASE];
    switch (r) {   // HCI errors (link level)
        case 0x05: return "authentication-failure";
        case 0x06: return "pin-or-key-missing";
        case 0x08: return "connection-timeout";
        case 0x13: return "remote-user-terminated";
        case 0x16: return "local-host-terminated";
        case 0x22: return "ll-response-timeout";
        case 0x3D: return "mic-failure";
        default: {
            char buf[8];
            snprintf(buf, sizeof(buf), "0x%02x", r);
            return buf;
        }
    }
}

// ATT error codes a read can fail with (for readError on characteristics).
inline std::string attErrorName(int status) {
    switch (status) {
        case 0x02: return "read-not-permitted";
        case 0x05: return "insufficient-authentication";
        case 0x08: return "insufficient-authorization";
        case 0x0C: return "insufficient-key-size";
        case 0x0F: return "insufficient-encryption";
        default: {
            char buf[8];
            snprintf(buf, sizeof(buf), "0x%02x", status & 0xFF);
            return buf;
        }
    }
}

// Does reading this characteristic need pairing first?
inline bool attErrorNeedsPairing(int status) {
    return status == 0x05 || status == 0x0F || status == 0x0C;
}

} // namespace blesec
} // namespace mcp
