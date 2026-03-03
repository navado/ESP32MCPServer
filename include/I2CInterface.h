#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mcp {

// Abstract I2C hardware-abstraction layer.
// Concrete implementations: ESP32I2CInterface (Wire) and MockI2C (tests).
class I2CInterface {
public:
    virtual ~I2CInterface() = default;

    virtual bool begin(int sda = -1, int scl = -1,
                       uint32_t freq = 100000) = 0;

    // Return true when a device ACKs at `address`
    virtual bool devicePresent(uint8_t address) = 0;

    // Scan all 7-bit addresses; return the ones that ACK
    virtual std::vector<uint8_t> scan() = 0;

    // Single-register read / write
    virtual bool    writeReg8  (uint8_t addr, uint8_t reg, uint8_t  val) = 0;
    virtual bool    writeReg16 (uint8_t addr, uint8_t reg, uint16_t val) = 0;
    virtual uint8_t readReg8   (uint8_t addr, uint8_t reg) = 0;
    virtual int16_t readReg16s (uint8_t addr, uint8_t reg) = 0; // signed
    virtual uint16_t readReg16u(uint8_t addr, uint8_t reg) = 0; // unsigned

    // Burst transfers
    virtual bool   readBytes (uint8_t addr, uint8_t reg,
                               uint8_t* buf, size_t len) = 0;
    virtual bool   writeBytes(uint8_t addr, uint8_t reg,
                               const uint8_t* buf, size_t len) = 0;

    // Send a bare command (no register byte) – for BH1750, SHT31, etc.
    virtual bool   sendCommand(uint8_t addr, uint8_t cmd) = 0;

    // Read raw bytes with no preceding register byte
    virtual size_t readRaw(uint8_t addr, uint8_t* buf, size_t len) = 0;

    // Delay helper (overridable for tests)
    virtual void delayMs(uint32_t ms) = 0;
};

} // namespace mcp
