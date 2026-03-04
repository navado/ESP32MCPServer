#pragma once

#include <Arduino.h>
#include <string>
#include <vector>

// Configuration for the discovery subsystem.
struct DiscoveryConfig {
    String   hostname;                  // mDNS hostname (no .local suffix)
    uint16_t mcpPort          = 9000;  // JSON-RPC WebSocket port
    uint16_t httpPort         = 80;    // HTTP dashboard port
    uint32_t broadcastInterval = 30000; // UDP broadcast period (ms); 0 = off
    uint16_t broadcastPort    = 5354;  // UDP destination port for broadcasts
};

// Metadata for a single sensor attached to the device.
// Populated from the I2C bus scan and included in every discovery announcement.
struct DiscoverySensorInfo {
    std::string id;          // e.g. "bme280_0x76"
    std::string type;        // e.g. "BME280"
    uint8_t     address = 0; // raw I2C address byte
    std::vector<std::string> parameters; // readable parameters, e.g. ["temperature","humidity"]
};

// DiscoveryManager
//
// Provides two complementary LAN discovery mechanisms:
//
//   1. mDNS / Zeroconf (ESPmDNS) — registers _mcp._tcp and _http._tcp so that
//      mDNS-capable clients (macOS, iOS, Linux/avahi, Windows 10+) can resolve
//      <hostname>.local without knowing the IP address.  DNS-SD TXT records
//      advertise the server version, capability set, and detected sensors.
//
//   2. Periodic UDP broadcast — sends a JSON capability beacon to
//      255.255.255.255:<broadcastPort> every broadcastInterval milliseconds.
//      The payload follows the MCP server-info schema and includes the full
//      list of detected sensors and their readable parameters, so clients can
//      discover the device and know what it offers without any prior connection.
//
// Capability announcements are also triggered immediately on:
//   - Network connection / AP start
//   - MCP server ready (call announceCapabilityChange() after begin())
//   - Successful I2C bus scan (call setSensors() with results)
//
// Typical lifecycle:
//   discoveryManager.begin(cfg);               // load stored config, derive hostname
//   networkManager.setDiscoveryManager(&dm);   // wire up network callbacks
//   // after MCP server is ready:
//   discoveryManager.announceCapabilityChange();
//   // after sensor scan:
//   discoveryManager.setSensors(devices);       // also triggers announcement
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

    // Update the advertised sensor list.  If the network is already connected
    // an immediate capability announcement (mDNS restart + UDP broadcast) is
    // triggered so that listening clients receive the updated sensor manifest
    // without waiting for the next periodic interval.
    void setSensors(const std::vector<DiscoverySensorInfo>& sensors);

    // Clear the sensor list (e.g. before re-scanning).
    void clearSensors();

    // Trigger an immediate UDP broadcast and mDNS TXT-record update.
    // Call this after any capability change: MCP server ready, sensor
    // scan complete, resource registration, etc.
    void announceCapabilityChange();

    // Return the current configuration snapshot.
    DiscoveryConfig getConfig() const;

    // Returns true while mDNS is actively registered.
    bool isMdnsActive() const { return mdnsStarted_; }

    // Number of sensors currently advertised.
    size_t sensorCount() const { return sensors_.size(); }

    // Build the JSON broadcast payload. Public so tests can verify its content
    // without exercising hardware networking.
    String buildBroadcastPayload() const;

private:
    DiscoveryConfig                  config_;
    bool                             mdnsStarted_   = false;
    uint32_t                         lastBroadcast_ = 0;
    String                           currentIp_;
    std::vector<DiscoverySensorInfo> sensors_;

    void startMDNS();
    void stopMDNS();
    void sendBroadcast();
    void saveConfig();
    void loadConfig();
};
