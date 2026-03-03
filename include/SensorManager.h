#pragma once

#include "I2CInterface.h"
#include "I2CSensors.h"
#include "SensorTypes.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mcp {

// ---------------------------------------------------------------------------
// SensorManager — orchestrates I2C bus scanning, driver creation, and polling.
//
// Usage:
//   SensorManager mgr(bus);
//   mgr.scanBus();        // discover devices, create drivers
//   mgr.initDrivers();    // initialise every found driver
//   auto readings = mgr.readAll();   // poll all sensors
// ---------------------------------------------------------------------------
class SensorManager {
public:
    explicit SensorManager(I2CInterface& bus);

    // ----- Discovery -------------------------------------------------------

    // Scan the I2C bus; create a driver for each recognised address.
    // Returns list of all detected devices (supported and unknown).
    std::vector<I2CDevice> scanBus();

    // Initialise every driver that was created by scanBus().
    // Returns number of successfully initialised drivers.
    int initDrivers();

    // ----- Reading ---------------------------------------------------------

    // Read all initialised drivers; returns all readings (including invalid
    // ones — caller should check SensorReading::valid).
    std::vector<SensorReading> readAll();

    // Read a single sensor identified by its sensorId string,
    // e.g. "bme280_0x76".  Returns empty vector when not found.
    std::vector<SensorReading> readSensor(const std::string& sensorId);

    // ----- Introspection ---------------------------------------------------

    // Return metadata for all detected devices (set after scanBus()).
    std::vector<I2CDevice> getDevices() const;

    // Number of drivers that have been successfully initialised.
    int driverCount() const;

    // ----- MCP JSON-RPC helpers --------------------------------------------

    // Build a JSON-RPC 2.0 result for the sensors/i2c/scan method.
    std::string buildScanResponse(uint32_t id) const;

    // Build a JSON-RPC 2.0 result for sensors/read.
    // sensorId may be empty ("") to read all sensors.
    std::string buildReadResponse(uint32_t id,
                                  const std::string& sensorId = "");

private:
    I2CInterface& bus_;

    std::vector<I2CDevice>                         devices_;
    std::vector<std::unique_ptr<I2CSensorDriver>>  drivers_;

    mutable std::mutex mutex_;

    // Map sensorId -> index in drivers_ for fast lookup
    std::map<std::string, size_t> driverIndex_;

    // Helper: derive sensorId from driver name + address
    static std::string makeSensorId(const char* name, uint8_t addr);
};

} // namespace mcp
