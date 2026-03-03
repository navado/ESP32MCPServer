#include "MetricsSystem.h"
#include <WiFi.h>
#include <mutex>
#include <ArduinoJson.h>

using namespace mcp;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const char* BOOT_METRICS_FILE = "/boot_metrics.json";
static const uint32_t SAVE_INTERVAL  = 60000; // ms
static const size_t   MAX_METRICS    = 50;

// Static member initialisation
std::mutex MetricsSystem::metricsMutex;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MetricsSystem::MetricsSystem()
    : initialized_(false), lastSaveTime_(0) {}

MetricsSystem::~MetricsSystem() {
    end();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool MetricsSystem::begin() {
    std::lock_guard<std::mutex> lock(metricsMutex);

    if (initialized_) return true;

    if (!LittleFS.begin(true)) {
        log_e("Failed to mount filesystem");
        return false;
    }

    if (!logger_.begin()) {
        log_e("Failed to initialise logger");
        return false;
    }

    if (!loadBootMetricsImpl()) {
        resetBootMetricsImpl();
    }

    initializeSystemMetrics();
    initialized_  = true;
    lastSaveTime_ = millis();
    return true;
}

void MetricsSystem::end() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    if (initialized_) {
        saveBootMetricsImpl();
        logger_.end();
        initialized_ = false;
    }
}

// ---------------------------------------------------------------------------
// Metric registration (public — acquires lock)
// ---------------------------------------------------------------------------

void MetricsSystem::registerMetric(const String& name, MetricType type,
                                   const String& description,
                                   const String& unit, const String& category) {
    // Assumes mutex is already held by the caller.
    if (metrics_.size() >= MAX_METRICS) {
        log_w("Max metrics limit reached, ignoring: %s", name.c_str());
        return;
    }

    MetricInfo info = {name, type, description, unit, category};
    metrics_[name] = info;

    MetricValue value = {};
    value.timestamp = millis();
    switch (type) {
        case MetricType::COUNTER:   value.counter = 0; break;
        case MetricType::GAUGE:     value.gauge   = 0.0; break;
        case MetricType::HISTOGRAM: value.histogram = {0.0, 0.0, 0.0, 0.0, 0}; break;
    }
    bootMetrics_[name] = value;
}

void MetricsSystem::registerCounter(const String& name, const String& description,
                                    const String& unit, const String& category) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    registerMetric(name, MetricType::COUNTER, description, unit, category);
}

void MetricsSystem::registerGauge(const String& name, const String& description,
                                  const String& unit, const String& category) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    registerMetric(name, MetricType::GAUGE, description, unit, category);
}

void MetricsSystem::registerHistogram(const String& name, const String& description,
                                      const String& unit, const String& category) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    registerMetric(name, MetricType::HISTOGRAM, description, unit, category);
}

// ---------------------------------------------------------------------------
// Metric updates — public (acquire lock, call impl)
// ---------------------------------------------------------------------------

void MetricsSystem::incrementCounter(const String& name, int64_t value) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    incrementCounterImpl(name, value);
}

void MetricsSystem::setGauge(const String& name, double value) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    setGaugeImpl(name, value);
}

void MetricsSystem::recordHistogram(const String& name, double value) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    recordHistogramImpl(name, value);
}

// ---------------------------------------------------------------------------
// Metric updates — private impl (mutex already held)
// ---------------------------------------------------------------------------

void MetricsSystem::incrementCounterImpl(const String& name, int64_t value) {
    auto it = metrics_.find(name);
    if (it == metrics_.end() || it->second.type != MetricType::COUNTER) return;

    bootMetrics_[name].counter   += value;
    bootMetrics_[name].timestamp  = millis();
    logger_.logMetric(name.c_str(), &bootMetrics_[name].counter, sizeof(int64_t));
}

void MetricsSystem::setGaugeImpl(const String& name, double value) {
    auto it = metrics_.find(name);
    if (it == metrics_.end() || it->second.type != MetricType::GAUGE) return;

    bootMetrics_[name].gauge     = value;
    bootMetrics_[name].timestamp = millis();
    logger_.logMetric(name.c_str(), &bootMetrics_[name].gauge, sizeof(double));
}

void MetricsSystem::recordHistogramImpl(const String& name, double value) {
    auto it = metrics_.find(name);
    if (it == metrics_.end() || it->second.type != MetricType::HISTOGRAM) return;

    auto& hist = bootMetrics_[name].histogram;
    if (hist.count == 0) {
        hist.min = value;
        hist.max = value;
    } else {
        hist.min = std::min(hist.min, value);
        hist.max = std::max(hist.max, value);
    }
    hist.sum += value;
    hist.count++;
    hist.value = hist.sum / hist.count;
    bootMetrics_[name].timestamp = millis();
    logger_.logMetric(name.c_str(), &hist, sizeof(hist));
}

// ---------------------------------------------------------------------------
// System metrics initialisation and update
// ---------------------------------------------------------------------------

void MetricsSystem::initializeSystemMetrics() {
    // Assumes mutex is already held.
    registerMetric("system.wifi.signal", MetricType::GAUGE,
                   "WiFi signal strength", "dBm", "system");
    registerMetric("system.heap.free",   MetricType::GAUGE,
                   "Free heap memory",    "bytes", "system");
    registerMetric("system.heap.min",    MetricType::GAUGE,
                   "Minimum free heap",   "bytes", "system");
    registerMetric("system.uptime",      MetricType::GAUGE,
                   "System uptime",       "ms",    "system");
}

void MetricsSystem::updateSystemMetrics() {
    // Do NOT hold metricsMutex here: setGauge() acquires it internally.
    if (WiFi.status() == WL_CONNECTED) {
        setGauge("system.wifi.signal", WiFi.RSSI());
    }
    setGauge("system.heap.free", ESP.getFreeHeap());
    setGauge("system.heap.min",  ESP.getMinFreeHeap());
    setGauge("system.uptime",    millis());

    uint32_t now = millis();
    {
        std::lock_guard<std::mutex> lock(metricsMutex);
        if (now - lastSaveTime_ >= SAVE_INTERVAL) {
            saveBootMetricsImpl();
            lastSaveTime_ = now;
        }
    }
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

MetricValue MetricsSystem::getMetric(const String& name, bool fromBoot) {
    std::lock_guard<std::mutex> lock(metricsMutex);

    auto it = metrics_.find(name);
    if (it == metrics_.end()) return MetricValue{};

    if (fromBoot) {
        return bootMetrics_.count(name) ? bootMetrics_[name] : MetricValue{};
    }

    std::vector<uLogger::Record> records;
    logger_.queryMetrics(name.c_str(), 0, records);
    if (records.empty()) return MetricValue{};

    MetricValue result = {};
    result.timestamp   = millis();

    switch (it->second.type) {
        case MetricType::COUNTER: {
            result.counter = 0;
            for (const auto& rec : records) {
                int64_t v = 0;
                memcpy(&v, rec.data, sizeof(v));
                result.counter += v;
            }
            break;
        }
        case MetricType::GAUGE:
            memcpy(&result.gauge, records.back().data, sizeof(result.gauge));
            break;
        case MetricType::HISTOGRAM: {
            std::vector<MetricValue> histVals;
            histVals.reserve(records.size());
            for (const auto& rec : records) {
                MetricValue hv = {};
                memcpy(&hv.histogram, rec.data, sizeof(hv.histogram));
                histVals.push_back(hv);
            }
            result = calculateHistogram(histVals);
            break;
        }
    }
    return result;
}

std::vector<MetricValue> MetricsSystem::getMetricHistory(const String& name,
                                                         uint32_t seconds) {
    std::lock_guard<std::mutex> lock(metricsMutex);

    auto it = metrics_.find(name);
    if (it == metrics_.end()) return {};

    uint64_t startTime = (seconds > 0)
        ? static_cast<uint64_t>(millis()) - static_cast<uint64_t>(seconds) * 1000
        : 0;

    std::vector<uLogger::Record> records;
    logger_.queryMetrics(name.c_str(), startTime, records);

    std::vector<MetricValue> result;
    result.reserve(records.size());

    for (const auto& rec : records) {
        MetricValue val = {};
        val.timestamp   = rec.timestamp;
        switch (it->second.type) {
            case MetricType::COUNTER:
                memcpy(&val.counter, rec.data, sizeof(val.counter));
                break;
            case MetricType::GAUGE:
                memcpy(&val.gauge, rec.data, sizeof(val.gauge));
                break;
            case MetricType::HISTOGRAM:
                memcpy(&val.histogram, rec.data, sizeof(val.histogram));
                break;
        }
        result.push_back(val);
    }
    return result;
}

std::map<String, MetricsSystem::MetricInfo>
MetricsSystem::getMetrics(const String& category) {
    std::lock_guard<std::mutex> lock(metricsMutex);

    if (category.isEmpty()) return metrics_;

    std::map<String, MetricInfo> filtered;
    for (const auto& kv : metrics_) {
        if (kv.second.category == category) filtered[kv.first] = kv.second;
    }
    return filtered;
}

// ---------------------------------------------------------------------------
// Persistence — public (acquire lock, call impl)
// ---------------------------------------------------------------------------

bool MetricsSystem::saveBootMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    return saveBootMetricsImpl();
}

bool MetricsSystem::loadBootMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    return loadBootMetricsImpl();
}

void MetricsSystem::resetBootMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    resetBootMetricsImpl();
}

// ---------------------------------------------------------------------------
// Persistence — private impl (mutex already held)
// ---------------------------------------------------------------------------

bool MetricsSystem::saveBootMetricsImpl() {
    File file = LittleFS.open(BOOT_METRICS_FILE, "w");
    if (!file) {
        log_e("Failed to open boot metrics file for writing");
        return false;
    }

    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();

    for (const auto& kv : metrics_) {
        JsonObject metric = root[kv.first].to<JsonObject>();
        metric["type"]        = static_cast<int>(kv.second.type);
        metric["description"] = kv.second.description;
        metric["unit"]        = kv.second.unit;
        metric["category"]    = kv.second.category;

        // Persist current boot value
        auto bit = bootMetrics_.find(kv.first);
        if (bit != bootMetrics_.end()) {
            switch (kv.second.type) {
                case MetricType::COUNTER:
                    metric["value"] = bit->second.counter;
                    break;
                case MetricType::GAUGE:
                    metric["value"] = bit->second.gauge;
                    break;
                case MetricType::HISTOGRAM:
                    metric["value"] = bit->second.histogram.value;
                    break;
            }
        }
    }

    bool ok = (serializeJson(doc, file) > 0);
    file.close();
    if (!ok) log_e("Failed to write boot metrics");
    return ok;
}

bool MetricsSystem::loadBootMetricsImpl() {
    File file = LittleFS.open(BOOT_METRICS_FILE, "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        log_e("Failed to parse boot metrics file");
        return false;
    }

    metrics_.clear();
    bootMetrics_.clear();

    for (JsonPair kv : doc.as<JsonObject>()) {
        String name = kv.key().c_str();

        MetricInfo info;
        info.name        = name;
        info.type        = static_cast<MetricType>(kv.value()["type"].as<int>());
        info.description = kv.value()["description"].as<const char*>();
        info.unit        = kv.value()["unit"].as<const char*>();
        info.category    = kv.value()["category"].as<const char*>();
        metrics_[name]   = info;

        MetricValue val  = {};
        val.timestamp    = millis();
        switch (info.type) {
            case MetricType::COUNTER:
                val.counter = kv.value()["value"] | (int64_t)0;
                break;
            case MetricType::GAUGE:
                val.gauge = kv.value()["value"] | 0.0;
                break;
            case MetricType::HISTOGRAM:
                val.histogram.value = kv.value()["value"] | 0.0;
                break;
        }
        bootMetrics_[name] = val;
    }
    return true;
}

void MetricsSystem::resetBootMetricsImpl() {
    bootMetrics_.clear();
    for (const auto& kv : metrics_) {
        MetricValue val = {};
        val.timestamp   = millis();
        switch (kv.second.type) {
            case MetricType::COUNTER:   val.counter = 0; break;
            case MetricType::GAUGE:     val.gauge   = 0.0; break;
            case MetricType::HISTOGRAM: val.histogram = {0.0, 0.0, 0.0, 0.0, 0}; break;
        }
        bootMetrics_[kv.first] = val;
    }
}

// ---------------------------------------------------------------------------
// clearHistory
// ---------------------------------------------------------------------------

void MetricsSystem::clearHistory() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    logger_.clear();
    resetBootMetricsImpl(); // already holding the lock
}

// ---------------------------------------------------------------------------
// calculateHistogram
// ---------------------------------------------------------------------------

MetricValue MetricsSystem::calculateHistogram(
    const std::vector<MetricValue>& values) {

    MetricValue result = {};
    result.timestamp = millis();
    result.histogram = {0.0, 0.0, 0.0, 0.0, 0};

    if (values.empty()) return result;

    auto& h   = result.histogram;
    h.min     = values[0].histogram.value;
    h.max     = values[0].histogram.value;
    h.count   = static_cast<uint32_t>(values.size());

    for (const auto& v : values) {
        h.min  = std::min(h.min, v.histogram.value);
        h.max  = std::max(h.max, v.histogram.value);
        h.sum += v.histogram.value;
    }
    h.value = h.sum / h.count;
    return result;
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

bool MetricsSystem::isInitialized() const { return initialized_; }
