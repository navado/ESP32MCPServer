#pragma once

#include "I2CInterface.h"
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <functional>

namespace mcp {

// ---------------------------------------------------------------------------
// MockI2C — in-memory I2C bus for unit tests.
//
// Usage:
//   MockI2C bus;
//   bus.addDevice(0x76);                       // BME280 address
//   bus.setReg(0x76, 0xD0, 0x60);             // WHO_AM_I
//   bus.setRegs(0x76, 0x88, calibBytes, 24);  // calibration block
//
// Then pass `bus` to a driver constructor and call probe()/init()/read().
// ---------------------------------------------------------------------------
class MockI2C : public I2CInterface {
public:
    // --- Configuration (call before driver under test) --------------------

    // Make address ACK to devicePresent() / scan()
    void addDevice(uint8_t addr) {
        devices_.insert(addr);
    }

    // Set a single register value
    void setReg(uint8_t addr, uint8_t reg, uint8_t value) {
        regs_[addr][reg] = value;
    }

    // Set a block of registers starting at `reg`
    void setRegs(uint8_t addr, uint8_t reg,
                 const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            regs_[addr][reg + i] = data[i];
        }
    }

    // Set raw read bytes (for readRaw(), e.g. BH1750, SHT31)
    void setRawRead(uint8_t addr, const uint8_t* data, size_t len) {
        rawRead_[addr] = std::vector<uint8_t>(data, data + len);
    }

    // Retrieve last written value for a register (for assertion in tests)
    uint8_t getWrittenReg(uint8_t addr, uint8_t reg) const {
        auto it = written_.find(addr);
        if (it == written_.end()) return 0;
        auto it2 = it->second.find(reg);
        return (it2 != it->second.end()) ? it2->second : 0;
    }

    // --- I2CInterface implementation --------------------------------------

    bool begin(int /*sda*/ = -1, int /*scl*/ = -1,
               uint32_t /*freq*/ = 100000) override {
        return true;
    }

    bool devicePresent(uint8_t address) override {
        return devices_.count(address) > 0;
    }

    std::vector<uint8_t> scan() override {
        return std::vector<uint8_t>(devices_.begin(), devices_.end());
    }

    bool writeReg8(uint8_t addr, uint8_t reg, uint8_t val) override {
        written_[addr][reg] = val;
        regs_[addr][reg]    = val; // writes are visible on readback
        return true;
    }

    bool writeReg16(uint8_t addr, uint8_t reg, uint16_t val) override {
        // Big-endian write (matches typical I2C sensors)
        uint8_t hi = val >> 8, lo = val & 0xFF;
        written_[addr][reg]     = hi;
        written_[addr][reg + 1] = lo;
        regs_[addr][reg]        = hi;
        regs_[addr][reg + 1]    = lo;
        return true;
    }

    uint8_t readReg8(uint8_t addr, uint8_t reg) override {
        auto it = regs_.find(addr);
        if (it == regs_.end()) return 0;
        auto it2 = it->second.find(reg);
        return (it2 != it->second.end()) ? it2->second : 0;
    }

    int16_t readReg16s(uint8_t addr, uint8_t reg) override {
        uint8_t hi = readReg8(addr, reg);
        uint8_t lo = readReg8(addr, reg + 1);
        return static_cast<int16_t>((hi << 8) | lo);
    }

    uint16_t readReg16u(uint8_t addr, uint8_t reg) override {
        uint8_t hi = readReg8(addr, reg);
        uint8_t lo = readReg8(addr, reg + 1);
        return static_cast<uint16_t>((hi << 8) | lo);
    }

    bool readBytes(uint8_t addr, uint8_t reg,
                   uint8_t* buf, size_t len) override {
        for (size_t i = 0; i < len; ++i) {
            buf[i] = readReg8(addr, static_cast<uint8_t>(reg + i));
        }
        return true;
    }

    bool writeBytes(uint8_t addr, uint8_t reg,
                    const uint8_t* buf, size_t len) override {
        for (size_t i = 0; i < len; ++i) {
            writeReg8(addr, static_cast<uint8_t>(reg + i), buf[i]);
        }
        return true;
    }

    bool sendCommand(uint8_t addr, uint8_t cmd) override {
        lastCmd_[addr] = cmd;
        return true;
    }

    size_t readRaw(uint8_t addr, uint8_t* buf, size_t len) override {
        auto it = rawRead_.find(addr);
        if (it == rawRead_.end() || it->second.empty()) return 0;
        size_t n = std::min(len, it->second.size());
        std::copy(it->second.begin(), it->second.begin() + n, buf);
        return n;
    }

    void delayMs(uint32_t /*ms*/) override {
        // No-op in tests
    }

    // Last command sent to an address
    uint8_t lastCommand(uint8_t addr) const {
        auto it = lastCmd_.find(addr);
        return (it != lastCmd_.end()) ? it->second : 0;
    }

private:
    std::set<uint8_t>                                devices_;
    std::map<uint8_t, std::map<uint8_t, uint8_t>>   regs_;
    std::map<uint8_t, std::map<uint8_t, uint8_t>>   written_;
    std::map<uint8_t, std::vector<uint8_t>>          rawRead_;
    std::map<uint8_t, uint8_t>                       lastCmd_;
};

} // namespace mcp
