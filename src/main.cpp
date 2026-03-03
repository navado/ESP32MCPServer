#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include "NetworkManager.h"
#include "MCPServer.h"
#include "SensorManager.h"
#include "I2CInterface.h"
#include "MetricsSystem.h"
#include "DiscoveryManager.h"

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

// Global instances
NetworkManager    networkManager;
MCPServer         mcpServer;
ESP32I2CInterface i2cBus;
SensorManager*    sensorManager = nullptr;
DiscoveryManager  discoveryManager;

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

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up...");

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    // Initialize discovery manager (loads NVS config; hostname derived from MAC
    // if not previously set).  Must be called before networkManager.begin() so
    // the mDNS/broadcast callbacks fire correctly when the network comes up.
    DiscoveryConfig discoveryCfg;
    discoveryCfg.mcpPort  = 9000;
    discoveryCfg.httpPort = 80;
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

    // Initialize I2C and scan for sensors
    i2cBus.begin();
    sensorManager = new SensorManager(i2cBus);
    auto devices = sensorManager->scanBus();
    Serial.printf("Found %d I2C device(s)\n", (int)devices.size());
    int initialised = sensorManager->initDrivers();
    Serial.printf("Initialised %d sensor driver(s)\n", initialised);

    // Register sensor MCP method handlers
    mcpServer.registerMethodHandler("sensors/i2c/scan",
        [](uint8_t /*cid*/, uint32_t id, const JsonObject& /*p*/) {
            return sensorManager->buildScanResponse(id);
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