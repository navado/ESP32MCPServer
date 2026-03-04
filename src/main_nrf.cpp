// main_nrf.cpp — Entry point for nRF52840 builds (sensor-only, no WiFi).
//
// WiFi-dependent subsystems (NetworkManager, MCPServer, DiscoveryManager) are
// excluded from nRF builds via build_src_filter in platformio.ini.  This file
// replaces main.cpp and provides a minimal setup that:
//   • initialises the I2C bus using BOARD_I2C_SDA / SCL from board_config.h
//   • scans for supported sensors and initialises their drivers
//   • prints sensor readings to Serial every 2 seconds

#include "board_config.h"
#include <Arduino.h>
#include <Wire.h>
#include "I2CInterface.h"
#include "SensorManager.h"
#include "MetricsSystem.h"

// ---------------------------------------------------------------------------
// Concrete I2C implementation for nRF52840 (Arduino Wire)
//
// The nRF52 Arduino core does not support runtime SDA/SCL remapping; pins are
// fixed by the board variant.  The sda/scl parameters are accepted but ignored
// so that this class satisfies the I2CInterface contract.
// ---------------------------------------------------------------------------
class NRF52I2CInterface : public mcp::I2CInterface {
public:
    bool begin(int /*sda*/ = -1, int /*scl*/ = -1,
               uint32_t freq = 100000) override {
        Wire.begin();
        Wire.setClock(freq);
        return true;
    }
    bool devicePresent(uint8_t address) override {
        Wire.beginTransmission(address);
        return (Wire.endTransmission() == 0);
    }
    std::vector<uint8_t> scan() override {
        std::vector<uint8_t> found;
        for (uint8_t addr = 1; addr < 127; ++addr)
            if (devicePresent(addr)) found.push_back(addr);
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
    bool writeBytes(uint8_t addr, uint8_t reg,
                    const uint8_t* buf, size_t len) override {
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
// Globals
// ---------------------------------------------------------------------------
static NRF52I2CInterface i2cBus;
static SensorManager*   sensorManager = nullptr;

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(BOARD_SERIAL_BAUD);
    // Wait up to 3 s for serial terminal (USB CDC) — skip if nothing connects.
    for (int i = 0; i < 30 && !Serial; ++i) delay(100);

    Serial.println("=== ESP32MCPServer — nRF52840 sensor node ===");
    Serial.print("Board: "); Serial.println(BOARD_NAME);

    // Initialise I2C.  On nRF52 the SDA/SCL pins come from the variant so
    // the sda/scl arguments are ignored by NRF52I2CInterface::begin().
    i2cBus.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ);

    sensorManager = new SensorManager(i2cBus);
    auto devices = sensorManager->scanBus();
    Serial.print("I2C devices found: ");
    Serial.println((int)devices.size());
    for (auto& d : devices) {
        Serial.print("  0x");
        if (d.address < 0x10) Serial.print('0');
        Serial.print(d.address, HEX);
        Serial.print("  ");
        Serial.print(d.name.c_str());
        Serial.println(d.supported ? "  [supported]" : "  [unknown]");
    }

    int inited = sensorManager->initDrivers();
    Serial.print("Sensor drivers initialised: ");
    Serial.println(inited);
    Serial.println("Reporting readings every 2 s...");
}

// ---------------------------------------------------------------------------
// loop — periodic sensor read and Serial print
// ---------------------------------------------------------------------------
void loop() {
    delay(2000);

    const auto& devices = sensorManager->getDevices();
    if (devices.empty()) {
        Serial.println("[no sensors]");
        return;
    }

    for (const auto& dev : devices) {
        if (!dev.supported) continue;
        // Build a sensor id the same way SensorManager does
        char addrBuf[8];
        snprintf(addrBuf, sizeof(addrBuf), "0x%02x", dev.address);
        std::string lower = dev.name;
        for (char& c : lower) c = static_cast<char>(tolower(c));
        std::string sid = lower + "_" + addrBuf;

        // buildReadResponse returns a JSON-RPC string; parse out the result
        // values and print them to Serial.
        std::string resp = sensorManager->buildReadResponse(0, sid);
        Serial.print(dev.name.c_str());
        Serial.print(" @ "); Serial.print(addrBuf);
        Serial.print(": ");
        // Print the raw JSON response (compact but human-readable on terminal)
        Serial.println(resp.c_str());
    }
}
