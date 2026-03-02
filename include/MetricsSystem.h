#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <memory>
#include <vector>
#include "uLogger.h"

namespace mcp {

struct MetricValue {
    uint64_t timestamp;
    union {
        int64_t counter;
        double gauge;
        struct {
            double   value;   // current mean
            double   min;
            double   max;
            double   sum;
            uint32_t count;
        } histogram;
    };
};

class MetricsSystem {
public:
    enum class MetricType { COUNTER, GAUGE, HISTOGRAM };

    struct MetricInfo {
        String     name;
        MetricType type;
        String     description;
        String     unit;
        String     category;
    };

    // Singleton accessor
    static MetricsSystem& getInstance() {
        static MetricsSystem instance;
        return instance;
    }

    bool begin();
    void end();

    void registerCounter  (const String& name, const String& description,
                           const String& unit = "", const String& category = "");
    void registerGauge    (const String& name, const String& description,
                           const String& unit = "", const String& category = "");
    void registerHistogram(const String& name, const String& description,
                           const String& unit = "", const String& category = "");

    void incrementCounter (const String& name, int64_t value = 1);
    void setGauge         (const String& name, double value);
    void recordHistogram  (const String& name, double value);

    MetricValue              getMetric       (const String& name, bool fromBoot = true);
    std::vector<MetricValue> getMetricHistory(const String& name, uint32_t seconds = 0);

    std::map<String, MetricInfo> getMetrics(const String& category = "");

    // Update built-in system metrics (heap, uptime, WiFi RSSI).
    // Must NOT be called while holding metricsMutex.
    void updateSystemMetrics();

    void resetBootMetrics();
    bool saveBootMetrics();
    bool loadBootMetrics();

    // Convenience aliases used by test code
    bool saveMetrics() { return saveBootMetrics(); }
    bool loadMetrics() { return loadBootMetrics(); }

    void clearHistory();
    bool isInitialized() const;

private:
    MetricsSystem();
    ~MetricsSystem();
    MetricsSystem(const MetricsSystem&)            = delete;
    MetricsSystem& operator=(const MetricsSystem&) = delete;

    static std::mutex metricsMutex;
    bool     initialized_;
    uint32_t lastSaveTime_;

    std::map<String, MetricInfo>  metrics_;
    std::map<String, MetricValue> bootMetrics_;
    uLogger logger_;

    // ---- private helpers that assume the mutex is already held ----
    void registerMetric(const String& name, MetricType type,
                        const String& description,
                        const String& unit, const String& category);

    void incrementCounterImpl(const String& name, int64_t value);
    void setGaugeImpl        (const String& name, double value);
    void recordHistogramImpl (const String& name, double value);

    bool saveBootMetricsImpl();
    bool loadBootMetricsImpl();
    void resetBootMetricsImpl();

    void initializeSystemMetrics();
    MetricValue calculateHistogram(const std::vector<MetricValue>& values);
};

// ---------------------------------------------------------------------------
// MetricTimer — RAII helper for timing code blocks
// ---------------------------------------------------------------------------
class MetricTimer {
public:
    explicit MetricTimer(const String& metricName)
        : name_(metricName), startTime_(micros()) {}

    ~MetricTimer() {
        uint32_t duration = micros() - startTime_;
        MetricsSystem::getInstance().recordHistogram(name_, duration / 1000.0);
    }

private:
    String   name_;
    uint32_t startTime_;
};

#define METRIC_TIMER(name) mcp::MetricTimer __timer(name)

} // namespace mcp

// Global singleton shorthand used by test and application code
#define METRICS mcp::MetricsSystem::getInstance()
