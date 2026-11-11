#include "board_config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include "NetworkManager.h"
#include "MCPServer.h"
#include "SensorManager.h"
#include "I2CInterface.h"
#include "MetricsSystem.h"
#include "DiscoveryManager.h"
#include "BusHistory.h"
#include "OTAManager.h"
#include "BLEScanner.h"

// ---------------------------------------------------------------------------
// Minimal ESP32 I2C implementation (wraps Arduino Wire library)
// ---------------------------------------------------------------------------
#include "I2CInterface.h"

class ESP32I2CInterface : public mcp::I2CInterface {
public:
    bool begin(int sda = -1, int scl = -1, uint32_t freq = 100000) override {
        if (sda >= 0 && scl >= 0) return Wire.begin(sda, scl, freq);
        return Wire.begin();
    }
    bool devicePresent(uint8_t address) override {
        Wire.beginTransmission(address);
        return (Wire.endTransmission() == 0);
    }
    std::vector<uint8_t> scan() override {
        std::vector<uint8_t> found;
        for (uint8_t addr = 1; addr < 127; ++addr) {
            if (devicePresent(addr)) found.push_back(addr);
        }
        return found;
    }
    bool writeReg8(uint8_t addr, uint8_t reg, uint8_t val) override {
        Wire.beginTransmission(addr);
        Wire.write(reg); Wire.write(val);
        return (Wire.endTransmission() == 0);
    }
    bool writeReg16(uint8_t addr, uint8_t reg, uint16_t val) override {
        Wire.beginTransmission(addr);
        Wire.write(reg); Wire.write(val >> 8); Wire.write(val & 0xFF);
        return (Wire.endTransmission() == 0);
    }
    uint8_t readReg8(uint8_t addr, uint8_t reg) override {
        Wire.beginTransmission(addr); Wire.write(reg); Wire.endTransmission(false);
        Wire.requestFrom(addr, (uint8_t)1);
        return Wire.available() ? Wire.read() : 0;
    }
    int16_t readReg16s(uint8_t addr, uint8_t reg) override {
        Wire.beginTransmission(addr); Wire.write(reg); Wire.endTransmission(false);
        Wire.requestFrom(addr, (uint8_t)2);
        uint8_t hi = Wire.available() ? Wire.read() : 0;
        uint8_t lo = Wire.available() ? Wire.read() : 0;
        return static_cast<int16_t>((hi << 8) | lo);
    }
    uint16_t readReg16u(uint8_t addr, uint8_t reg) override {
        return static_cast<uint16_t>(readReg16s(addr, reg));
    }
    bool readBytes(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) override {
        Wire.beginTransmission(addr); Wire.write(reg); Wire.endTransmission(false);
        Wire.requestFrom(addr, (uint8_t)len);
        for (size_t i = 0; i < len; ++i) buf[i] = Wire.available() ? Wire.read() : 0;
        return true;
    }
    bool writeBytes(uint8_t addr, uint8_t reg, const uint8_t* buf, size_t len) override {
        Wire.beginTransmission(addr); Wire.write(reg);
        for (size_t i = 0; i < len; ++i) Wire.write(buf[i]);
        return (Wire.endTransmission() == 0);
    }
    bool sendCommand(uint8_t addr, uint8_t cmd) override {
        Wire.beginTransmission(addr); Wire.write(cmd);
        return (Wire.endTransmission() == 0);
    }
    size_t readRaw(uint8_t addr, uint8_t* buf, size_t len) override {
        Wire.requestFrom(addr, (uint8_t)len);
        size_t n = 0;
        while (Wire.available() && n < len) buf[n++] = Wire.read();
        return n;
    }
    void delayMs(uint32_t ms) override { delay(ms); }
};

using namespace mcp;

// ---------------------------------------------------------------------------
// Helper: convert the SensorManager device list to DiscoverySensorInfo
// so DiscoveryManager can include them in capability announcements.
// ---------------------------------------------------------------------------
static std::vector<DiscoverySensorInfo>
makeDiscoverySensors(const std::vector<mcp::I2CDevice>& devs) {
    std::vector<DiscoverySensorInfo> result;
    for (const auto& dev : devs) {
        if (!dev.supported) continue;  // skip unrecognised addresses
        DiscoverySensorInfo si;
        // Construct the same id format used by SensorManager: name_0xNN (lowercase)
        char addrBuf[8];
        snprintf(addrBuf, sizeof(addrBuf), "0x%02x", dev.address);
        std::string lower = dev.name;
        for (char& c : lower) c = static_cast<char>(tolower(c));
        si.id         = lower + "_" + addrBuf;
        si.type       = dev.name;
        si.address    = dev.address;
        si.parameters = dev.parameters;
        result.push_back(si);
    }
    return result;
}

// Global instances
NetworkManager    networkManager;
MCPServer         mcpServer;
ESP32I2CInterface i2cBus;
SensorManager*    sensorManager = nullptr;
DiscoveryManager  discoveryManager;
OTAManager        otaManager;
#if BOARD_HAS_BLE
mcp::BLEScanner   bleScanner;
#endif

// Task handles
TaskHandle_t mcpTaskHandle = nullptr;

// MCP task function — also drives the periodic UDP broadcast.
void mcpTask(void* parameter) {
    while (true) {
        mcpServer.handleClient();
        discoveryManager.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Serialise a BLEServiceInfo vector into a JSON-RPC result envelope under
// "result.services".  Shared by the ble/services/start and ble/services/results
// handlers.
static std::string buildServicesResponse(uint32_t id,
                                         const std::vector<mcp::BLEServiceInfo>& services,
                                         bool connected) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;
    doc["result"]["connected"] = connected;
    JsonArray svcArr = doc["result"]["services"].to<JsonArray>();
    for (const auto& svc : services) {
        JsonObject so = svcArr.add<JsonObject>();
        so["uuid"] = svc.uuid;
        JsonArray chrArr = so["characteristics"].to<JsonArray>();
        for (const auto& chr : svc.characteristics) {
            JsonObject co = chrArr.add<JsonObject>();
            co["uuid"]       = chr.uuid;
            co["properties"] = chr.properties;
            if (!chr.valueHex.empty())  co["valueHex"] = chr.valueHex;
            if (!chr.valueText.empty()) co["valueText"] = chr.valueText;
        }
    }
    std::string out; serializeJson(doc, out); return out;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up...");

    // Initialise the BLE stack FIRST, while the heap is still unfragmented.
    // Bluetooth needs large contiguous allocations (bt_workqueue, controller
    // RAM); doing this after the bus-history ring buffers allocate fails with
    // "BTU_StartUp Unable to allocate resources for bt_workqueue".
#if BOARD_HAS_BLE
    bleScanner.begin();
#endif

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    // Initialise bus history ring buffers (loads config from LittleFS; allocates
    // from free heap if no explicit sizes are configured).
    BUS_HISTORY.begin();
    // Wire the announce callback so that a config change re-advertises
    // capabilities to LAN clients immediately.
    BUS_HISTORY.setAnnounceCb([&]() { discoveryManager.announceCapabilityChange(); });
    // Give NetworkManager access for the /bus-history HTTP endpoints.
    networkManager.setBusHistory(&BUS_HISTORY);
    // Give DiscoveryManager a way to embed history info in capability beacons.
    discoveryManager.setHistoryInfoCb([]() -> std::string {
        return BUS_HISTORY.configToJson();
    });

    // Register bus/history/* MCP handlers.
    mcpServer.registerMethodHandler("bus/history/read",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            std::string type = "can";
            if (p["type"].is<const char*>()) type = p["type"].as<const char*>();
            uint32_t limit = p["limit"] | 0u;

            if (type == "can")      return BUS_HISTORY.canSnapshotJson(id, limit);
            if (type == "nmea")     return BUS_HISTORY.nmeaSnapshotJson(id, limit);
            if (type == "nmea2000") return BUS_HISTORY.nmea2000SnapshotJson(id, limit);
            if (type == "obdii")    return BUS_HISTORY.obdiiSnapshotJson(id, limit);

            // Unknown type — return an error.
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"]      = id;
            doc["error"]["code"]    = -32602;
            doc["error"]["message"] = "Unknown history type; use can|nmea|nmea2000|obdii";
            std::string out; serializeJson(doc, out); return out;
        });

    mcpServer.registerMethodHandler("bus/history/config/get",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            std::string cfg = BUS_HISTORY.configToJson();
            // Wrap in a JSON-RPC result envelope.
            std::string out = "{\"jsonrpc\":\"2.0\",\"id\":";
            char tmp[16]; std::snprintf(tmp, sizeof(tmp), "%u", (unsigned)id);
            out += tmp; out += ",\"result\":"; out += cfg; out += "}";
            return out;
        });

    mcpServer.registerMethodHandler("bus/history/config/set",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            mcp::BusHistoryConfig cfg = BUS_HISTORY.getConfig();
            auto applyUint = [&](const char* key, uint32_t& field) {
                if (p[key].is<unsigned int>() || p[key].is<int>())
                    field = p[key].as<uint32_t>();
            };
            applyUint("canFrameCount",     cfg.canFrameCount);
            applyUint("nmeaLineCount",     cfg.nmeaLineCount);
            applyUint("nmea2000Count",     cfg.nmea2000Count);
            applyUint("obdiiCount",        cfg.obdiiCount);
            applyUint("ramBudgetBytes",    cfg.ramBudgetBytes);
            applyUint("safetyMarginBytes", cfg.safetyMarginBytes);
            BUS_HISTORY.setConfig(cfg); // persists + reallocates + announces

            std::string cfgJson = BUS_HISTORY.configToJson();
            std::string out = "{\"jsonrpc\":\"2.0\",\"id\":";
            char tmp[16]; std::snprintf(tmp, sizeof(tmp), "%u", (unsigned)id);
            out += tmp; out += ",\"result\":"; out += cfgJson; out += "}";
            return out;
        });

    // Initialize OTA manager — loads password from NVS, registers /ota/* routes.
    otaManager.begin();
    networkManager.setOTAManager(&otaManager);

    // Initialize discovery manager (loads NVS config; hostname derived from MAC
    // if not previously set).  Must be called before networkManager.begin() so
    // the mDNS/broadcast callbacks fire correctly when the network comes up.
    DiscoveryConfig discoveryCfg;
    discoveryCfg.mcpPort  = BOARD_MCP_PORT;
    discoveryCfg.httpPort = BOARD_HTTP_PORT;
    discoveryManager.begin(discoveryCfg);
    networkManager.setDiscoveryManager(&discoveryManager);

    // Start network manager
    networkManager.begin();

    // Wait for network connection or AP mode
    while (!networkManager.isConnected() && networkManager.getIPAddress().isEmpty()) {
        delay(100);
    }

    Serial.print("Device IP: ");
    Serial.println(networkManager.getIPAddress());

    // Initialize MCP server
    mcpServer.begin(networkManager.isConnected());
    // Immediately announce MCP capability so LAN listeners learn the server
    // is ready (sensor list is still empty at this point; will be updated
    // after the I2C scan below).
    discoveryManager.announceCapabilityChange();

    // Initialize I2C and scan for sensors
    i2cBus.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ);
    sensorManager = new SensorManager(i2cBus);
    auto devices = sensorManager->scanBus();
    Serial.printf("Found %d I2C device(s)\n", (int)devices.size());
    int initialised = sensorManager->initDrivers();
    Serial.printf("Initialised %d sensor driver(s)\n", initialised);

    // Advertise detected sensors; triggers an immediate capability re-announcement
    // so the broadcast payload and mDNS TXT records include the sensor manifest.
    discoveryManager.setSensors(makeDiscoverySensors(devices));

    // Register sensor MCP method handlers
    mcpServer.registerMethodHandler("sensors/i2c/scan",
        [](uint8_t /*cid*/, uint32_t id, const JsonObject& /*p*/) {
            auto response = sensorManager->buildScanResponse(id);
            // Re-advertise sensors whenever a client triggers a bus scan so the
            // discovery broadcast reflects any newly attached devices.
            discoveryManager.setSensors(makeDiscoverySensors(sensorManager->getDevices()));
            return response;
        });

    mcpServer.registerMethodHandler("sensors/read",
        [](uint8_t /*cid*/, uint32_t id, const JsonObject& p) {
            std::string sid;
            if (p["sensorId"].is<const char*>()) {
                sid = p["sensorId"].as<const char*>();
            }
            return sensorManager->buildReadResponse(id, sid);
        });

    // --- metrics/list ---
    mcpServer.registerMethodHandler("metrics/list",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            String category;
            if (p["category"].is<const char*>()) category = p["category"].as<const char*>();
            auto all = METRICS.getMetrics(category);
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            JsonArray list = doc["result"]["metrics"].to<JsonArray>();
            for (auto& [name, info] : all) {
                JsonObject m = list.add<JsonObject>();
                m["name"]        = info.name;
                m["type"]        = info.type == mcp::MetricsSystem::MetricType::COUNTER   ? "counter"
                                 : info.type == mcp::MetricsSystem::MetricType::GAUGE     ? "gauge"
                                                                                           : "histogram";
                m["description"] = info.description;
                m["unit"]        = info.unit;
                m["category"]    = info.category;
            }
            std::string out; serializeJson(doc, out); return out;
        });

    // --- metrics/get ---
    mcpServer.registerMethodHandler("metrics/get",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            String name;
            if (p["name"].is<const char*>()) name = p["name"].as<const char*>();
            auto all = METRICS.getMetrics();
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            auto it = all.find(name);
            if (it == all.end()) {
                doc["error"]["code"]    = -32602;
                doc["error"]["message"] = "Metric not found";
            } else {
                auto& info     = it->second;
                MetricValue val = METRICS.getMetric(name);
                JsonObject result = doc["result"].to<JsonObject>();
                result["name"]      = name;
                result["unit"]      = info.unit;
                result["category"]  = info.category;
                result["timestamp"] = (uint64_t)val.timestamp;
                if (info.type == mcp::MetricsSystem::MetricType::COUNTER) {
                    result["type"]  = "counter";
                    result["value"] = (int64_t)val.counter;
                } else if (info.type == mcp::MetricsSystem::MetricType::GAUGE) {
                    result["type"]  = "gauge";
                    result["value"] = val.gauge;
                } else {
                    result["type"]              = "histogram";
                    result["value"]["mean"]     = val.histogram.value;
                    result["value"]["min"]      = val.histogram.min;
                    result["value"]["max"]      = val.histogram.max;
                    result["value"]["count"]    = val.histogram.count;
                }
            }
            std::string out; serializeJson(doc, out); return out;
        });

    // --- metrics/history ---
    mcpServer.registerMethodHandler("metrics/history",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            String name;
            uint32_t seconds = 300; // 5-minute default window
            if (p["name"].is<const char*>()) name    = p["name"].as<const char*>();
            if (p["seconds"].is<int>())      seconds = p["seconds"].as<int>();
            auto history = METRICS.getMetricHistory(name, seconds);
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            JsonObject result  = doc["result"].to<JsonObject>();
            result["name"]     = name;
            JsonArray  entries = result["entries"].to<JsonArray>();
            for (auto& v : history) {
                JsonObject e = entries.add<JsonObject>();
                e["ts"]  = (uint64_t)v.timestamp;
                e["val"] = v.gauge; // union first field covers gauge, counter, histogram.value
            }
            std::string out; serializeJson(doc, out); return out;
        });

    // --- logs/query ---
    mcpServer.registerMethodHandler("logs/query",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            String name;
            uint32_t seconds = 3600; // 1-hour default window
            if (p["name"].is<const char*>()) name    = p["name"].as<const char*>();
            if (p["seconds"].is<int>())      seconds = p["seconds"].as<int>();
            auto history = METRICS.getMetricHistory(name, seconds);
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            JsonObject result = doc["result"].to<JsonObject>();
            result["name"]    = name;
            result["count"]   = (uint32_t)history.size();
            JsonArray entries = result["entries"].to<JsonArray>();
            for (auto& v : history) {
                JsonObject e = entries.add<JsonObject>();
                e["ts"]  = (uint64_t)v.timestamp;
                e["val"] = v.gauge;
            }
            std::string out; serializeJson(doc, out); return out;
        });

    // --- logs/clear ---
    mcpServer.registerMethodHandler("logs/clear",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            METRICS.clearHistory();
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            doc["result"]["ok"] = true;
            std::string out; serializeJson(doc, out); return out;
        });

    // --- BLE scanning / GATT enumeration ---
    // (BLE stack initialised at the top of setup() so it allocates before the
    // bus-history ring buffers fragment the heap.)
#if BOARD_HAS_BLE

    // ble/scan — one-shot scan for `durationMs` (default 5000); 0 = continuous
    // until ble/scan/stop.  When a scan is already running the call reports
    // started:false with the remaining time so clients can tell the two apart.
    mcpServer.registerMethodHandler("ble/scan",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            uint32_t durationMs = p["durationMs"] | 5000u;
            bool started = bleScanner.startScan(durationMs);
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            doc["result"]["started"] = started;
            doc["result"]["scanning"] = bleScanner.isScanning();
            if (!started) {
                uint32_t dur = bleScanner.getScanDurationMs();
                uint64_t startAt = bleScanner.getScanStartedAt();
                if (dur == 0 || startAt == 0) {
                    doc["result"]["remainingMs"] = 0;
                } else {
                    uint64_t elapsed = millis() - startAt;
                    doc["result"]["remainingMs"] = elapsed >= dur ? 0u : dur - elapsed;
                }
            }
            std::string out; serializeJson(doc, out); return out;
        });

    // ble/scan/results — stop scan (if running) and return captured reports.
    // Optional "mac" param: case-insensitive filter that returns ONLY that
    // device with its FULL payload history.  Without mac, every device is
    // returned with only the newest MAX_PAYLOADS_VIEW payloads (count still
    // reports the true distinct total) so the response stays small even
    // under a flooded radio; the deep history is available via ?mac=.
    // Optional AND-combined response filters: serviceUuid (128-bit string),
    // manufacturerId (hex, e.g. "ffae" or "ae8f"), macPrefix (first 3 octets
    // of the MAC).  Filters only affect serialisation, never capture, so a
    // wrong filter cannot lose data.
    mcpServer.registerMethodHandler("ble/scan/results",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            bleScanner.stopScan();
            auto reports = bleScanner.getResults();
            std::string macFilter;
            if (p["mac"].is<const char*>()) {
                macFilter = p["mac"].as<const char*>();
                for (char& c : macFilter) c = static_cast<char>(tolower(c));
            }
            std::string svcFilter, mfrFilter, prefixFilter;
            if (p["serviceUuid"].is<const char*>()) svcFilter = p["serviceUuid"].as<const char*>();
            if (p["manufacturerId"].is<const char*>()) mfrFilter = p["manufacturerId"].as<const char*>();
            if (p["macPrefix"].is<const char*>()) prefixFilter = p["macPrefix"].as<const char*>();
            for (char& c : mfrFilter) c = static_cast<char>(tolower(c));
            for (char& c : prefixFilter) c = static_cast<char>(tolower(c));
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            uint64_t startAt = bleScanner.getScanStartedAt();
            uint32_t dur = bleScanner.getScanDurationMs();
            if (startAt != 0) doc["result"]["scanStartedMs"] = millis() - startAt;
            if (dur != 0 || startAt != 0) doc["result"]["durationMs"] = dur;
            JsonArray arr = doc["result"]["devices"].to<JsonArray>();
            for (const auto& r : reports) {
                if (!macFilter.empty()) {
                    std::string mac = r.mac;
                    for (char& c : mac) c = static_cast<char>(tolower(c));
                    if (mac != macFilter) continue;
                    // Deep fetch: serialise the complete history.
                    JsonObject o = arr.add<JsonObject>();
                    o["mac"]          = r.mac;
                    o["name"]         = r.name;
                    o["rssi"]         = r.rssi;
                    o["services"]     = r.servicesHex;
                    o["manufacturer"] = r.manufacturer;
                    o["connectable"]  = r.isConnectable;
                    o["count"]        = r.count;
                    if (r.firstSeen != 0) o["firstSeen"] = r.firstSeen;
                    JsonArray hist = o["payloadHistory"].to<JsonArray>();
                    for (const auto& pl : r.payloadHistory) {
                        hist.add(pl);
                    }
                    break;  // at most one matching device
                }
                if (!svcFilter.empty()) {
                    if (r.servicesHex.find(svcFilter) == std::string::npos) continue;
                }
                if (!mfrFilter.empty()) {
                    std::string mfr = r.manufacturer;
                    for (char& c : mfr) c = static_cast<char>(tolower(c));
                    if (mfr.compare(0, mfrFilter.length(), mfrFilter) != 0) continue;
                }
                if (!prefixFilter.empty()) {
                    std::string mac = r.mac;
                    for (char& c : mac) c = static_cast<char>(tolower(c));
                    if (mac.compare(0, prefixFilter.length(), prefixFilter) != 0) continue;
                }
                JsonObject o = arr.add<JsonObject>();
                o["mac"]          = r.mac;
                o["name"]         = r.name;
                o["rssi"]         = r.rssi;
                o["services"]     = r.servicesHex;
                o["manufacturer"] = r.manufacturer;
                o["connectable"]  = r.isConnectable;
                o["count"]        = r.count;
                if (r.firstSeen != 0) o["firstSeen"] = r.firstSeen;
                // Quick-look view: only the newest MAX_PAYLOADS_VIEW payloads.
                JsonArray hist = o["payloadHistory"].to<JsonArray>();
                constexpr size_t view = mcp::blecore::MAX_PAYLOADS_VIEW;
                size_t start = r.payloadHistory.size() > view
                                   ? r.payloadHistory.size() - view
                                   : 0;
                for (size_t i = start; i < r.payloadHistory.size(); ++i) {
                    hist.add(r.payloadHistory[i]);
                }
            }
            std::string out; serializeJson(doc, out); return out;
        });

    // ble/scan/payloads — stop scan (if running) and return the MAC-independent
    // payload registry: distinct payloads seen across ALL broadcasters, in
    // first-seen order, with count and first/last seen (ms relative to scan
    // start).  This survives per-MAC MAC-rotation churn (the phone rotates its
    // MAC every ~1s) and device eviction, so a full command surface is captured
    // in a single scan.
    mcpServer.registerMethodHandler("ble/scan/payloads",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            bleScanner.stopScan();
            auto registry = bleScanner.getPayloadRegistry();
            uint64_t startAt = bleScanner.getScanStartedAt();
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            uint32_t dur = bleScanner.getScanDurationMs();
            if (startAt != 0) doc["result"]["scanStartedMs"] = millis() - startAt;
            if (dur != 0 || startAt != 0) doc["result"]["durationMs"] = dur;
            JsonArray arr = doc["result"]["payloads"].to<JsonArray>();
            for (const auto& e : registry) {
                JsonObject o = arr.add<JsonObject>();
                o["payload"]  = e.payload;
                o["count"]    = e.count;
                if (startAt != 0) {
                    o["firstSeen"] = e.firstSeen - startAt;
                    o["lastSeen"]  = e.lastSeen - startAt;
                }
            }
            std::string out; serializeJson(doc, out); return out;
        });

    // ble/scan/poll — live read of the payload registry WITHOUT stopping the
    // scan.  Stateless: returns the full current registry; clients diff by
    // firstSeen.  Meant for continuous scans (durationMs:0) and long captures.
    mcpServer.registerMethodHandler("ble/scan/poll",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            auto registry = bleScanner.getPayloadRegistry();
            uint64_t startAt = bleScanner.getScanStartedAt();
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            doc["result"]["scanning"] = bleScanner.isScanning();
            uint32_t dur = bleScanner.getScanDurationMs();
            if (startAt != 0) doc["result"]["scanStartedMs"] = millis() - startAt;
            if (dur != 0 || startAt != 0) doc["result"]["durationMs"] = dur;
            JsonArray arr = doc["result"]["payloads"].to<JsonArray>();
            for (const auto& e : registry) {
                JsonObject o = arr.add<JsonObject>();
                o["payload"]  = e.payload;
                o["count"]    = e.count;
                if (startAt != 0) {
                    o["firstSeen"] = e.firstSeen - startAt;
                    o["lastSeen"]  = e.lastSeen - startAt;
                }
            }
            std::string out; serializeJson(doc, out); return out;
        });

    // ble/scan/stop — stop a continuous scan early.
    mcpServer.registerMethodHandler("ble/scan/stop",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            bleScanner.stopScan();
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            doc["result"]["ok"] = true;
            std::string out; serializeJson(doc, out); return out;
        });

    // ble/connect — begin an asynchronous GATT connect to a peer MAC.  The
    // attempt completes in the background within `timeoutMs`; poll the result
    // via ble/connect/results.
    mcpServer.registerMethodHandler("ble/connect",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            std::string mac;
            if (p["mac"].is<const char*>()) mac = p["mac"].as<const char*>();
            uint32_t timeoutMs = p["timeoutMs"] | 5000u;
            if (mac.empty()) {
                JsonDocument doc;
                doc["jsonrpc"] = "2.0";
                doc["id"]      = id;
                doc["error"]["code"]    = -32602;
                doc["error"]["message"] = "Invalid params: mac required";
                std::string out; serializeJson(doc, out); return out;
            }
            bool started = bleScanner.connect(mac, timeoutMs);
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            doc["result"]["started"]    = started;
            doc["result"]["connecting"] = started && bleScanner.isConnecting();
            doc["result"]["connected"]  = bleScanner.isConnected();
            std::string out; serializeJson(doc, out); return out;
        });

    // ble/connect/results — poll the outcome of the last async connect.
    mcpServer.registerMethodHandler("ble/connect/results",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            doc["result"]["connecting"] = bleScanner.isConnecting();
            doc["result"]["connected"]  = bleScanner.isConnected();
            std::string out; serializeJson(doc, out); return out;
        });

    // ble/disconnect — drop the GATT link.
    mcpServer.registerMethodHandler("ble/disconnect",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            bleScanner.disconnect();
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            doc["result"]["ok"] = true;
            std::string out; serializeJson(doc, out); return out;
        });

    // ble/services/start — enumerate services/characteristics of the connected
    // device, read readable values, subscribe to notifiable characteristics.
    // Returns immediately; notified values arrive via ble/services/results.
    mcpServer.registerMethodHandler("ble/services/start",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            auto services = bleScanner.getServicesStart();
            return buildServicesResponse(id, services, bleScanner.isConnected());
        });

    // ble/services/results — wait up to notifyCaptureMs for notified values,
    // then return the (possibly updated) enumeration.
    mcpServer.registerMethodHandler("ble/services/results",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            uint32_t captureMs = p["notifyCaptureMs"] | 3000u;
            auto services = bleScanner.getServicesResults(captureMs);
            return buildServicesResponse(id, services, bleScanner.isConnected());
        });

    // ble/services/stop — unsubscribe from all notifiable characteristics.
    mcpServer.registerMethodHandler("ble/services/stop",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            bleScanner.getServicesStop();
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["id"] = id;
            doc["result"]["ok"] = true;
            std::string out; serializeJson(doc, out); return out;
        });
#endif // BOARD_HAS_BLE

    // Create MCP task
    xTaskCreatePinnedToCore(
        mcpTask,
        "MCPTask",
        8192,
        nullptr,
        1,
        &mcpTaskHandle,
        1  // Run on core 1
    );
}

void loop() {
    // Main loop can be used for other tasks
    // Network and MCP handling is done in their respective tasks
    delay(1000);
}