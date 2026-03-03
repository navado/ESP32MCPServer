#pragma once

#include "SensorTypes.h"
#include "I2CInterface.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mcp {

// ---------------------------------------------------------------------------
// Base class for all I2C sensor drivers
// ---------------------------------------------------------------------------
class I2CSensorDriver {
public:
    I2CSensorDriver(I2CInterface& bus, uint8_t address)
        : bus_(bus), address_(address), initialized_(false) {}
    virtual ~I2CSensorDriver() = default;

    virtual const char* name()  const = 0;

    // Return true when the sensor responds and identifies itself correctly
    virtual bool probe() = 0;

    // Initialize sensor (set measurement mode, read calibration, …)
    virtual bool init() = 0;

    // Take a reading; caller must check SensorReading::valid
    virtual std::vector<SensorReading> read() = 0;

    // List the parameter names this driver can return
    virtual std::vector<std::string> parameters() const = 0;

    uint8_t address()       const { return address_; }
    bool    isInitialized() const { return initialized_; }

    // MCP resource URI for this sensor, e.g. "sensor://i2c/bme280_0x76"
    std::string uri() const;

protected:
    I2CInterface& bus_;
    uint8_t       address_;
    bool          initialized_;

    // Convenience: build a SensorReading for this sensor
    SensorReading make(const std::string& param, double value,
                       const std::string& unit, uint64_t ts = 0) const;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
// Try to match the address against all known sensors; returns nullptr if
// nothing responds or the sensor is unsupported.
std::unique_ptr<I2CSensorDriver> createI2CDriver(I2CInterface& bus,
                                                  uint8_t address);

// Enumerate addresses that are recognised (but not necessarily present)
std::vector<uint8_t> knownI2CAddresses();

// ---------------------------------------------------------------------------
// BME280 — temperature, humidity, pressure
// Addresses: 0x76 (SDO low) or 0x77 (SDO high)
// Chip-ID: 0x60 (BME280) | 0x58 (BMP280 — no humidity)
// ---------------------------------------------------------------------------
class BME280Driver : public I2CSensorDriver {
public:
    explicit BME280Driver(I2CInterface& bus, uint8_t addr = 0x76);
    const char* name() const override { return isBMP280_ ? "BMP280" : "BME280"; }
    bool probe() override;
    bool init()  override;
    std::vector<SensorReading> read() override;
    std::vector<std::string>   parameters() const override;

private:
    struct Calib {
        uint16_t T1; int16_t T2, T3;
        uint16_t P1; int16_t P2,P3,P4,P5,P6,P7,P8,P9;
        uint8_t  H1; int16_t H2; uint8_t H3;
        int16_t  H4, H5; int8_t H6;
    } calib_{};
    bool isBMP280_ = false;

    bool readCalibration();
    // Returns temperature in °C; also populates t_fine for other compensation
    double compensateTemp    (int32_t adc, int32_t& t_fine);
    double compensatePressure(int32_t adc, int32_t t_fine);   // Pa
    double compensateHumidity(int32_t adc, int32_t t_fine);   // %RH
};

// ---------------------------------------------------------------------------
// MPU6050 — accelerometer, gyroscope, onboard temperature
// Addresses: 0x68 (AD0 low) or 0x69 (AD0 high)
// WHO_AM_I: 0x68
// ---------------------------------------------------------------------------
class MPU6050Driver : public I2CSensorDriver {
public:
    explicit MPU6050Driver(I2CInterface& bus, uint8_t addr = 0x68);
    const char* name() const override { return "MPU6050"; }
    bool probe() override;
    bool init()  override;
    std::vector<SensorReading> read() override;
    std::vector<std::string>   parameters() const override {
        return {"accel_x","accel_y","accel_z",
                "gyro_x", "gyro_y", "gyro_z", "temperature"};
    }
private:
    float accelScale_ = 16384.0f; // ±2 g  → LSB/g
    float gyroScale_  =   131.0f; // ±250°/s → LSB/(°/s)
};

// ---------------------------------------------------------------------------
// ADS1115 — 4-channel 16-bit ADC
// Addresses: 0x48..0x4B
// ---------------------------------------------------------------------------
class ADS1115Driver : public I2CSensorDriver {
public:
    explicit ADS1115Driver(I2CInterface& bus, uint8_t addr = 0x48);
    const char* name() const override { return "ADS1115"; }
    bool probe() override;
    bool init()  override;
    std::vector<SensorReading> read() override;
    std::vector<std::string>   parameters() const override {
        return {"ch0_V","ch1_V","ch2_V","ch3_V"};
    }
private:
    // Trigger a single-shot conversion on `mux` (0=AIN0 vs GND, …)
    // and return the result in raw LSBs.
    int16_t readChannel(uint8_t muxBits);
    // Convert raw LSBs to volts for PGA=1 (±4.096 V range)
    static float toVolts(int16_t raw) { return raw * 4.096f / 32768.0f; }
};

// ---------------------------------------------------------------------------
// SHT31 — temperature, humidity (Sensirion)
// Addresses: 0x44 (ADDR pin low) or 0x45 (ADDR pin high)
// ---------------------------------------------------------------------------
class SHT31Driver : public I2CSensorDriver {
public:
    explicit SHT31Driver(I2CInterface& bus, uint8_t addr = 0x44);
    const char* name() const override { return "SHT31"; }
    bool probe() override;
    bool init()  override;
    std::vector<SensorReading> read() override;
    std::vector<std::string>   parameters() const override {
        return {"temperature","humidity"};
    }
private:
    static bool crc8(const uint8_t* data, size_t len, uint8_t expected);
};

// ---------------------------------------------------------------------------
// BH1750 — ambient light (lux)
// Addresses: 0x23 (ADDR low) or 0x5C (ADDR high)
// ---------------------------------------------------------------------------
class BH1750Driver : public I2CSensorDriver {
public:
    explicit BH1750Driver(I2CInterface& bus, uint8_t addr = 0x23);
    const char* name() const override { return "BH1750"; }
    bool probe() override;
    bool init()  override;
    std::vector<SensorReading> read() override;
    std::vector<std::string>   parameters() const override {
        return {"light_lux"};
    }
};

// ---------------------------------------------------------------------------
// INA219 — current, voltage, power (Texas Instruments)
// Addresses: 0x40..0x4F (configurable via A0/A1 pins)
// ---------------------------------------------------------------------------
class INA219Driver : public I2CSensorDriver {
public:
    explicit INA219Driver(I2CInterface& bus, uint8_t addr = 0x40);
    const char* name() const override { return "INA219"; }
    bool probe() override;
    bool init()  override;
    std::vector<SensorReading> read() override;
    std::vector<std::string>   parameters() const override {
        return {"bus_voltage_V","shunt_mV","current_mA","power_mW"};
    }
private:
    float currentLSB_ = 0.1f; // mA per LSB
};

} // namespace mcp
