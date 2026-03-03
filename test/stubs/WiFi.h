#pragma once
// WiFi.h stub for native builds.

#include "Arduino.h"
#include <functional>
#include <stdint.h>

// ─── WiFi status constants ────────────────────────────────────────────────────
#define WL_NO_SHIELD        255
#define WL_IDLE_STATUS      0
#define WL_NO_SSID_AVAIL    1
#define WL_CONNECTED        3
#define WL_CONNECT_FAILED   4
#define WL_CONNECTION_LOST  5
#define WL_DISCONNECTED     6

typedef int wl_status_t;

// ─── WiFi mode constants ──────────────────────────────────────────────────────
#define WIFI_OFF    0
#define WIFI_STA    1
#define WIFI_AP     2
#define WIFI_AP_STA 3

// ─── WiFi events (subset used by NetworkManager) ─────────────────────────────
typedef enum {
    ARDUINO_EVENT_WIFI_READY = 0,
    ARDUINO_EVENT_WIFI_STA_CONNECTED,
    ARDUINO_EVENT_WIFI_STA_GOT_IP,
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
    ARDUINO_EVENT_WIFI_AP_START,
    ARDUINO_EVENT_WIFI_AP_STOP,
    ARDUINO_EVENT_MAX
} arduino_event_id_t;

typedef arduino_event_id_t WiFiEvent_t;
struct WiFiEventInfo_t {};

// ─── Legacy ESP-IDF system event IDs (used in some firmware code) ─────────────
typedef int system_event_id_t;
static constexpr system_event_id_t SYSTEM_EVENT_STA_CONNECTED    = 4;
static constexpr system_event_id_t SYSTEM_EVENT_STA_GOT_IP       = 7;
static constexpr system_event_id_t SYSTEM_EVENT_STA_DISCONNECTED = 5;
static constexpr system_event_id_t SYSTEM_EVENT_AP_START         = 11;
static constexpr system_event_id_t SYSTEM_EVENT_AP_STOP          = 12;

// ─── IPAddress ────────────────────────────────────────────────────────────────
class IPAddress {
public:
    IPAddress() : a_(0), b_(0), c_(0), d_(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : a_(a), b_(b), c_(c), d_(d) {}
    String toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d", a_, b_, c_, d_);
        return String(buf);
    }
    operator String() const { return toString(); }
private:
    uint8_t a_, b_, c_, d_;
};

// ─── WiFiClass ────────────────────────────────────────────────────────────────
class WiFiClass {
public:
    using EventCB = std::function<void(WiFiEvent_t, WiFiEventInfo_t)>;

    void   onEvent(EventCB /*cb*/) {}
    wl_status_t status() const { return (wl_status_t)WL_DISCONNECTED; }
    bool   begin(const char* /*ssid*/, const char* /*pass*/ = nullptr) { return false; }
    void   disconnect(bool /*wifioff*/ = false) {}
    void   mode(int /*m*/) {}
    void   softAP(const char* /*ssid*/, const char* /*pass*/ = nullptr) {}

    IPAddress localIP()   const { return IPAddress(127, 0, 0, 1); }
    IPAddress softAPIP()  const { return IPAddress(192, 168, 4, 1); }
    String    SSID()      const { return String(""); }
    int8_t    RSSI()      const { return -70; }

    // Populate mac[6] with a fixed stub address for native builds.
    void macAddress(uint8_t* mac) const {
        for (int i = 0; i < 6; ++i) mac[i] = static_cast<uint8_t>(i + 1);
    }
    String macAddress() const { return String("01:02:03:04:05:06"); }

    operator bool() const { return false; }
};

inline WiFiClass WiFi;
