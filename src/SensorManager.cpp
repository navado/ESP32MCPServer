#include "SensorManager.h"
#include <cstdio>
#include <cstring>

namespace mcp {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/* static */ std::string SensorManager::makeSensorId(const char* name,
                                                      uint8_t addr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s_0x%02X", name,
                  static_cast<unsigned>(addr));
    std::string s = buf;
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SensorManager::SensorManager(I2CInterface& bus) : bus_(bus) {}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

std::vector<I2CDevice> SensorManager::scanBus() {
    #ifndef MCP_NO_THREADS
    std::lock_guard<std::mutex> lk(mutex_);
    #endif

    // Clear previous state
    devices_.clear();
    drivers_.clear();
    driverIndex_.clear();

    // Scan all 7-bit addresses
    auto found = bus_.scan();

    for (uint8_t addr : found) {
        I2CDevice dev;
        dev.address   = addr;
        dev.supported = false;

        auto drv = createI2CDriver(bus_, addr);
        if (drv) {
            dev.name      = drv->name();
            dev.supported = true;
            dev.parameters = drv->parameters();

            std::string sid = makeSensorId(drv->name(), addr);
            driverIndex_[sid] = drivers_.size();
            drivers_.push_back(std::move(drv));
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "0x%02X", static_cast<unsigned>(addr));
            dev.name = std::string("Unknown@") + buf;
        }
        devices_.push_back(dev);
    }

    return devices_;
}

int SensorManager::initDrivers() {
    #ifndef MCP_NO_THREADS
    std::lock_guard<std::mutex> lk(mutex_);
    #endif

    int count = 0;
    for (auto& drv : drivers_) {
        if (drv->init()) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

std::vector<SensorReading> SensorManager::readAll() {
    #ifndef MCP_NO_THREADS
    std::lock_guard<std::mutex> lk(mutex_);
    #endif

    std::vector<SensorReading> all;
    for (auto& drv : drivers_) {
        if (!drv->isInitialized()) continue;
        auto readings = drv->read();
        all.insert(all.end(), readings.begin(), readings.end());
    }
    return all;
}

std::vector<SensorReading> SensorManager::readSensor(
    const std::string& sensorId) {
    #ifndef MCP_NO_THREADS
    std::lock_guard<std::mutex> lk(mutex_);
    #endif

    auto it = driverIndex_.find(sensorId);
    if (it == driverIndex_.end()) return {};
    auto& drv = drivers_[it->second];
    if (!drv->isInitialized()) return {};
    return drv->read();
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

std::vector<I2CDevice> SensorManager::getDevices() const {
    #ifndef MCP_NO_THREADS
    std::lock_guard<std::mutex> lk(mutex_);
    #endif
    return devices_;
}

int SensorManager::driverCount() const {
    #ifndef MCP_NO_THREADS
    std::lock_guard<std::mutex> lk(mutex_);
    #endif
    return static_cast<int>(drivers_.size());
}

// ---------------------------------------------------------------------------
// MCP JSON builders
// ---------------------------------------------------------------------------

static void appendStr(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        if (c == '"')  { out += "\\\""; continue; }
        if (c == '\\') { out += "\\\\"; continue; }
        out += c;
    }
    out += '"';
}

std::string SensorManager::buildScanResponse(uint32_t id) const {
    #ifndef MCP_NO_THREADS
    std::lock_guard<std::mutex> lk(mutex_);
    #endif

    std::string r = "{\"jsonrpc\":\"2.0\",\"id\":";
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "%u", id);
    r += tmp;
    r += ",\"result\":{\"devices\":[";

    bool first = true;
    for (const auto& dev : devices_) {
        if (!first) r += ",";
        first = false;

        std::snprintf(tmp, sizeof(tmp), "{\"address\":\"0x%02X\",\"name\":",
                      static_cast<unsigned>(dev.address));
        r += tmp;
        appendStr(r, dev.name);
        r += ",\"supported\":";
        r += dev.supported ? "true" : "false";

        if (!dev.parameters.empty()) {
            r += ",\"parameters\":[";
            for (size_t i = 0; i < dev.parameters.size(); ++i) {
                if (i) r += ",";
                appendStr(r, dev.parameters[i]);
            }
            r += "]";
        }
        r += "}";
    }

    r += "]}}";
    return r;
}

std::string SensorManager::buildReadResponse(uint32_t id,
                                              const std::string& sensorId) {
    std::vector<SensorReading> readings;
    if (sensorId.empty()) {
        readings = readAll();
    } else {
        readings = readSensor(sensorId);
    }

    std::string r = "{\"jsonrpc\":\"2.0\",\"id\":";
    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "%u", id);
    r += tmp;
    r += ",\"result\":{\"readings\":[";

    bool first = true;
    for (const auto& rd : readings) {
        if (!first) r += ",";
        first = false;

        r += "{\"sensorId\":";
        appendStr(r, rd.sensorId);
        r += ",\"parameter\":";
        appendStr(r, rd.parameter);
        std::snprintf(tmp, sizeof(tmp), ",\"value\":%.6g,\"unit\":", rd.value);
        r += tmp;
        appendStr(r, rd.unit);
        r += ",\"valid\":";
        r += rd.valid ? "true" : "false";
        r += "}";
    }

    r += "]}}";
    return r;
}

} // namespace mcp
