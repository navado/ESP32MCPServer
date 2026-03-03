#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include "NetworkManager.h"
#include "MCPServer.h"
#include "SensorManager.h"
#include "I2CInterface.h"

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

// Task handles
TaskHandle_t mcpTaskHandle = nullptr;

// MCP task function
void mcpTask(void* parameter) {
    while (true) {
        mcpServer.handleClient();
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