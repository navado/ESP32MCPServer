#include "DiscoveryManager.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <Preferences.h>

// File-scoped UDP socket — keeps WiFiUDP.h out of the public header so it
// doesn't have to be resolved by every translation unit that includes
// DiscoveryManager.h (avoids "WiFiUDP.h: No such file or directory" on
// non-WiFi or partial-framework build environments).
static WiFiUDP g_udp;

static constexpr const char* PREFS_NS   = "discovery";
static constexpr const char* KEY_HOST   = "hostname";
static constexpr const char* KEY_BCAST  = "bcast_ms";
static constexpr const char* BCAST_ADDR = "255.255.255.255";

// MCP methods advertised in every capability beacon.
static const char* const MCP_METHODS[] = {
    "initialize",
    "resources/list", "resources/subscribe", "resources/unsubscribe",
    "sensors/i2c/scan", "sensors/read",
    "metrics/list", "metrics/get", "metrics/history",
    "logs/query", "logs/clear",
};
static constexpr size_t MCP_METHODS_COUNT =
    sizeof(MCP_METHODS) / sizeof(MCP_METHODS[0]);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void DiscoveryManager::begin(const DiscoveryConfig& cfg) {
    // Stop any previously active mDNS session and reset runtime state so that
    // calling begin() a second time (e.g. in unit tests) starts from a clean
    // slate without stale mdnsStarted_ / currentIp_ values.
    stopMDNS();
    currentIp_     = "";
    lastBroadcast_ = 0;

    config_ = cfg;
    loadConfig(); // NVS values override caller-supplied defaults

    if (config_.hostname.isEmpty()) {
        // Derive a unique hostname from the last 3 bytes of the MAC address.
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char buf[32];
        snprintf(buf, sizeof(buf), "esp32-mcp-%02x%02x%02x", mac[3], mac[4], mac[5]);
        config_.hostname = buf;
        saveConfig();
    }
}

void DiscoveryManager::end() {
    stopMDNS();
}

void DiscoveryManager::update() {
    if (currentIp_.isEmpty() || config_.broadcastInterval == 0) return;
    uint32_t now = millis();
    if (now - lastBroadcast_ >= config_.broadcastInterval) {
        sendBroadcast();
        lastBroadcast_ = now;
    }
}

// ---------------------------------------------------------------------------
// Network events
// ---------------------------------------------------------------------------

void DiscoveryManager::onNetworkConnected(const String& ip) {
    currentIp_ = ip;
    startMDNS();
    sendBroadcast();
    lastBroadcast_ = millis();
}

void DiscoveryManager::onNetworkDisconnected() {
    currentIp_ = "";
    stopMDNS();
}

// ---------------------------------------------------------------------------
// Configuration mutators
// ---------------------------------------------------------------------------

void DiscoveryManager::setHostname(const String& hostname) {
    if (hostname == config_.hostname) return;
    config_.hostname = hostname;
    saveConfig();
    if (!currentIp_.isEmpty()) {
        stopMDNS();
        startMDNS();
    }
}

void DiscoveryManager::setBroadcastInterval(uint32_t ms) {
    config_.broadcastInterval = ms;
    saveConfig();
}

void DiscoveryManager::setSensors(const std::vector<DiscoverySensorInfo>& sensors) {
    sensors_ = sensors;
    // Announce immediately so LAN clients learn about the new sensors without
    // waiting for the next periodic broadcast.
    announceCapabilityChange();
}

void DiscoveryManager::clearSensors() {
    sensors_.clear();
    announceCapabilityChange();
}

void DiscoveryManager::announceCapabilityChange() {
    if (currentIp_.isEmpty()) return; // not yet connected; will announce on connect
    // Restart mDNS so TXT records reflect the latest sensor list.
    stopMDNS();
    startMDNS();
    // Send an immediate broadcast beacon.
    sendBroadcast();
    lastBroadcast_ = millis();
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

DiscoveryConfig DiscoveryManager::getConfig() const {
    return config_;
}

// ---------------------------------------------------------------------------
// Broadcast payload (public for testing)
// ---------------------------------------------------------------------------

String DiscoveryManager::buildBroadcastPayload() const {
    JsonDocument doc;

    // --- Identity ----------------------------------------------------------
    doc["discovery"]  = "mcp-v1";    // protocol marker for receivers
    doc["device"]     = "esp32-mcp-server";
    doc["hostname"]   = config_.hostname;
    doc["fqdn"]       = String(config_.hostname) + ".local";
    doc["ip"]         = currentIp_;

    // --- Server info (mirrors MCP initialize response) ---------------------
    JsonObject serverInfo    = doc["serverInfo"].to<JsonObject>();
    serverInfo["name"]       = "esp32-mcp-server";
    serverInfo["version"]    = "1.0.0";

    // --- Transport ---------------------------------------------------------
    doc["mcpPort"]   = config_.mcpPort;
    doc["httpPort"]  = config_.httpPort;

    // --- MCP capabilities (mirrors initialize capabilities object) ---------
    JsonObject caps         = doc["capabilities"].to<JsonObject>();
    caps["resources"]       = true;
    caps["subscriptions"]   = true;
    caps["sensors"]         = true;
    caps["metrics"]         = true;

    // --- Registered MCP methods --------------------------------------------
    JsonArray methods = doc["methods"].to<JsonArray>();
    for (size_t i = 0; i < MCP_METHODS_COUNT; ++i) {
        methods.add(MCP_METHODS[i]);
    }

    // --- Detected sensors --------------------------------------------------
    JsonArray sensArr = doc["sensors"].to<JsonArray>();
    for (const auto& s : sensors_) {
        JsonObject sObj = sensArr.add<JsonObject>();
        sObj["id"]   = s.id.c_str();
        sObj["type"] = s.type.c_str();
        char addrBuf[8];
        snprintf(addrBuf, sizeof(addrBuf), "0x%02x", s.address);
        sObj["address"] = addrBuf;
        JsonArray params = sObj["parameters"].to<JsonArray>();
        for (const auto& p : s.parameters) {
            params.add(p.c_str());
        }
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void DiscoveryManager::startMDNS() {
    if (mdnsStarted_) stopMDNS();
    if (!MDNS.begin(config_.hostname.c_str())) {
        log_e("mDNS start failed for hostname: %s", config_.hostname.c_str());
        return;
    }

    // ── _mcp._tcp — MCP JSON-RPC over WebSocket ───────────────────────────
    MDNS.addService("mcp", "tcp", config_.mcpPort);
    MDNS.addServiceTxt("mcp", "tcp", "version",      "1.0.0");
    MDNS.addServiceTxt("mcp", "tcp", "path",         "/");
    MDNS.addServiceTxt("mcp", "tcp", "caps",         "resources,subscriptions,sensors,metrics");

    // Embed sensor IDs in a TXT record (comma-separated, RFC 6763 §6.5 limit
    // is 255 bytes per string, so we keep this concise).
    if (!sensors_.empty()) {
        String sensorIds;
        for (size_t i = 0; i < sensors_.size(); ++i) {
            if (i) sensorIds += ',';
            sensorIds += sensors_[i].id.c_str();
        }
        MDNS.addServiceTxt("mcp", "tcp", "sensors", sensorIds.c_str());
    }

    // ── _http._tcp — REST / dashboard ────────────────────────────────────
    MDNS.addService("http", "tcp", config_.httpPort);
    MDNS.addServiceTxt("http", "tcp", "path", "/");

    mdnsStarted_ = true;
    log_i("mDNS started: %s.local  sensors=%d", config_.hostname.c_str(),
          (int)sensors_.size());
}

void DiscoveryManager::stopMDNS() {
    if (!mdnsStarted_) return;
    MDNS.end();
    mdnsStarted_ = false;
}

void DiscoveryManager::sendBroadcast() {
    if (currentIp_.isEmpty() || config_.broadcastPort == 0) return;
    String payload = buildBroadcastPayload();
    g_udp.beginPacket(BCAST_ADDR, config_.broadcastPort);
    g_udp.write(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
    g_udp.endPacket();
}

void DiscoveryManager::saveConfig() {
    Preferences prefs;
    prefs.begin(PREFS_NS, false);
    prefs.putString(KEY_HOST,  config_.hostname);
    prefs.putUInt  (KEY_BCAST, config_.broadcastInterval);
    prefs.end();
}

void DiscoveryManager::loadConfig() {
    Preferences prefs;
    prefs.begin(PREFS_NS, true);
    String   h = prefs.getString(KEY_HOST,  "");
    uint32_t b = prefs.getUInt  (KEY_BCAST, config_.broadcastInterval);
    prefs.end();
    if (!h.isEmpty()) config_.hostname = h;
    config_.broadcastInterval = b;
}
