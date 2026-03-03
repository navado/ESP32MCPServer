#include "DiscoveryManager.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Preferences.h>

static constexpr const char* PREFS_NS   = "discovery";
static constexpr const char* KEY_HOST   = "hostname";
static constexpr const char* KEY_BCAST  = "bcast_ms";
static constexpr const char* BCAST_ADDR = "255.255.255.255";

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

DiscoveryConfig DiscoveryManager::getConfig() const {
    return config_;
}

String DiscoveryManager::buildBroadcastPayload() const {
    JsonDocument doc;
    doc["device"]    = "esp32-mcp-server";
    doc["hostname"]  = config_.hostname;
    doc["fqdn"]      = String(config_.hostname) + ".local";
    doc["ip"]        = currentIp_;
    doc["mcpPort"]   = config_.mcpPort;
    doc["httpPort"]  = config_.httpPort;
    doc["version"]   = "1.0.0";

    JsonArray caps = doc["capabilities"].to<JsonArray>();
    caps.add("mcp");
    caps.add("sensors");
    caps.add("metrics");

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
    // Register the MCP JSON-RPC WebSocket service
    MDNS.addService("mcp",  "tcp", config_.mcpPort);
    MDNS.addServiceTxt("mcp", "tcp", "version", "1.0.0");
    MDNS.addServiceTxt("mcp", "tcp", "path", "/");
    // Register the HTTP dashboard service
    MDNS.addService("http", "tcp", config_.httpPort);
    MDNS.addServiceTxt("http", "tcp", "path", "/");

    mdnsStarted_ = true;
    log_i("mDNS started: %s.local", config_.hostname.c_str());
}

void DiscoveryManager::stopMDNS() {
    if (!mdnsStarted_) return;
    MDNS.end();
    mdnsStarted_ = false;
}

void DiscoveryManager::sendBroadcast() {
    if (currentIp_.isEmpty() || config_.broadcastPort == 0) return;
    String payload = buildBroadcastPayload();
    udp_.beginPacket(BCAST_ADDR, config_.broadcastPort);
    udp_.write(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
    udp_.endPacket();
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
