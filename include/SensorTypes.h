#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mcp {

// Sensor reading -------------------------------------------------------
struct SensorReading {
    std::string sensorId;   // e.g. "bme280_0x76"
    std::string parameter;  // e.g. "temperature"
    double      value;
    std::string unit;       // e.g. "°C"
    uint64_t    timestamp;  // millis()
    bool        valid;

    SensorReading()
        : value(0), timestamp(0), valid(false) {}

    SensorReading(const std::string& sid, const std::string& param,
                  double val, const std::string& u, uint64_t ts)
        : sensorId(sid), parameter(param), value(val), unit(u),
          timestamp(ts), valid(true) {}
};

// Detected I2C device --------------------------------------------------
struct I2CDevice {
    uint8_t     address;
    std::string name;       // "BME280", "MPU6050", "Unknown"
    bool        supported;  // driver available
    std::vector<std::string> parameters; // readable parameters
};

} // namespace mcp
