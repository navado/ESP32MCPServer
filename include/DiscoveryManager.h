#pragma once

#include <Arduino.h>
#include <WiFiUDP.h>

// Configuration for the discovery subsystem.
struct DiscoveryConfig {
    String   hostname;                 // mDNS hostname (no .local suffix)
    uint16_t mcpPort          = 9000; // JSON-RPC WebSocket port
    uint16_t httpPort         = 80;   // HTTP dashboard port
    uint32_t broadcastInterval = 30000; // UDP broadcast period (ms); 0 = off
    uint16_t broadcastPort    = 5354; // UDP destination port for broadcasts
};

// DiscoveryManager
//
// Provides two complementary LAN discovery mechanisms:
//
//   1. mDNS / Zeroconf (ESPmDNS) — registers _mcp._tcp and _http._tcp so that
//      mDNS-capable clients (macOS, iOS, Linux/avahi, Windows 10+) can resolve
//      <hostname>.local without knowing the IP address.
//
//   2. Periodic UDP broadcast — sends a JSON capability beacon to
//      255.255.255.255:<broadcastPort> every broadcastInterval milliseconds.
//      Any client on the same LAN can listen on that port to auto-discover
//      the device.
//
// Typical lifecycle:
//   discoveryManager.begin(cfg);               // load stored config, derive hostname
//   networkManager.setDiscoveryManager(&dm);   // wire up network callbacks
//   // ... later, called by NetworkManager:
//   discoveryManager.onNetworkConnected(ip);   // starts mDNS + sends first broadcast
//   // ... in the periodic task:
//   discoveryManager.update();                 // sends subsequent broadcasts
//   discoveryManager.onNetworkDisconnected();  // stops mDNS
class DiscoveryManager {
public:
    DiscoveryManager() = default;

    // Load stored config (Preferences) and derive hostname from MAC if unset.
    void begin(const DiscoveryConfig& cfg);

    // Stop mDNS.
    void end();

    // Send a periodic UDP broadcast if the interval has elapsed.
    // Call this from a FreeRTOS task loop.
    void update();

    // Start mDNS and send an immediate broadcast.
    void onNetworkConnected(const String& ip);

    // Stop mDNS; suppress broadcasts until reconnected.
    void onNetworkDisconnected();

    // Update the mDNS hostname and persist it to NVS.
    // If a network is already connected the mDNS service is restarted.
    void setHostname(const String& hostname);

    // Update the broadcast interval and persist it to NVS.
    void setBroadcastInterval(uint32_t ms);

    // Return the current configuration snapshot.
    DiscoveryConfig getConfig() const;

    // Returns true while mDNS is actively registered.
    bool isMdnsActive() const { return mdnsStarted_; }

    // Build the JSON broadcast payload. Public so tests can verify its content
    // without exercising hardware networking.
    String buildBroadcastPayload() const;

private:
    DiscoveryConfig config_;
    bool            mdnsStarted_  = false;
    uint32_t        lastBroadcast_ = 0;
    String          currentIp_;
    WiFiUDP         udp_;

    void startMDNS();
    void stopMDNS();
    void sendBroadcast();
    void saveConfig();
    void loadConfig();
};
