#include <unity.h>
#include "MetricsSystem.h"
#include <LittleFS.h>
#include <WiFi.h>

using namespace mcp;

void setUp(void) {
    LittleFS.begin(true);
    METRICS.begin();
    METRICS.resetBootMetrics();
}

void tearDown(void) {
    METRICS.end();
    LittleFS.end();
}

void test_counter_metrics() {
    const char* metric_name = "test.counter";
    METRICS.registerCounter(metric_name, "Test counter");
    
    METRICS.incrementCounter(metric_name);
    METRICS.incrementCounter(metric_name, 2);
    
    auto value = METRICS.getMetric(metric_name, true);
    TEST_ASSERT_EQUAL(3, value.counter);
    
    // Test persistence
    METRICS.saveMetrics();
    METRICS.resetBootMetrics();
    METRICS.loadMetrics();
    
    value = METRICS.getMetric(metric_name, true);
    TEST_ASSERT_EQUAL(3, value.counter);
}

void test_gauge_metrics() {
    const char* metric_name = "test.gauge";
    METRICS.registerGauge(metric_name, "Test gauge");
    
    METRICS.setGauge(metric_name, 42.5);
    auto value = METRICS.getMetric(metric_name, true);
    TEST_ASSERT_EQUAL_FLOAT(42.5, value.gauge);
    
    METRICS.setGauge(metric_name, 50.0);
    value = METRICS.getMetric(metric_name, true);
    TEST_ASSERT_EQUAL_FLOAT(50.0, value.gauge);
}

void test_histogram_metrics() {
    const char* metric_name = "test.histogram";
    METRICS.registerHistogram(metric_name, "Test histogram");
    
    // Record some values
    METRICS.recordHistogram(metric_name, 10.0);
    METRICS.recordHistogram(metric_name, 20.0);
    METRICS.recordHistogram(metric_name, 30.0);
    
    auto value = METRICS.getMetric(metric_name, true);
    TEST_ASSERT_EQUAL(3, value.histogram.count);
    TEST_ASSERT_EQUAL_FLOAT(10.0, value.histogram.min);
    TEST_ASSERT_EQUAL_FLOAT(30.0, value.histogram.max);
    TEST_ASSERT_EQUAL_FLOAT(20.0, value.histogram.value); // mean
    TEST_ASSERT_EQUAL_FLOAT(60.0, value.histogram.sum);
}

void test_metric_history() {
    const char* metric_name = "test.history";
    METRICS.registerCounter(metric_name, "Test history");
    
    // Add some values over time
    for (int i = 0; i < 5; i++) {
        METRICS.incrementCounter(metric_name);
        delay(100);
    }
    
    auto history = METRICS.getMetricHistory(metric_name, 1); // Last second
    TEST_ASSERT_EQUAL(5, history.size());
    
    // Test with specific time window
    history = METRICS.getMetricHistory(metric_name, 0); // All time
    TEST_ASSERT_GREATER_OR_EQUAL(5, history.size());
}

void test_system_metrics() {
    // Test system metrics registration
    METRICS.updateSystemMetrics();
    
    auto wifi = METRICS.getMetric("system.wifi.signal", true);
    auto heap = METRICS.getMetric("system.heap.free", true);
    
    TEST_ASSERT_NOT_EQUAL(0, heap.gauge);
    
    // If WiFi is connected, signal should be negative
    if (WiFi.status() == WL_CONNECTED) {
        TEST_ASSERT_LESS_THAN(0, wifi.gauge);
    }
}

void test_metric_timer() {
    const char* metric_name = "test.timer";
    METRICS.registerHistogram(metric_name, "Test timer");
    
    {
        MetricTimer timer(metric_name);
        delay(100); // Simulate work
    }
    
    auto value = METRICS.getMetric(metric_name, true);
    TEST_ASSERT_GREATER_OR_EQUAL(100, value.histogram.value);
    TEST_ASSERT_LESS_THAN(150, value.histogram.value);
}

void test_error_handling() {
    // Test invalid metric name
    METRICS.incrementCounter("nonexistent");
    METRICS.setGauge("nonexistent", 1.0);
    METRICS.recordHistogram("nonexistent", 1.0);
    
    // Test wrong metric type
    const char* counter_name = "test.counter.type";
    const char* gauge_name = "test.gauge.type";
    METRICS.registerCounter(counter_name, "Test counter");
    METRICS.registerGauge(gauge_name, "Test gauge");
    
    METRICS.setGauge(counter_name, 1.0); // Should be ignored
    METRICS.incrementCounter(gauge_name); // Should be ignored
    
    auto counter_value = METRICS.getMetric(counter_name, true);
    auto gauge_value = METRICS.getMetric(gauge_name, true);
    
    TEST_ASSERT_EQUAL(0, counter_value.counter);
    TEST_ASSERT_EQUAL(0.0, gauge_value.gauge);
}

void test_concurrent_access() {
    // This test simulates concurrent access as much as possible in a single thread
    const char* metric_name = "test.concurrent";
    METRICS.registerCounter(metric_name, "Test concurrent");
    
    for (int i = 0; i < 1000; i++) {
        METRICS.incrementCounter(metric_name);
        if (i % 100 == 0) {
            METRICS.getMetric(metric_name, true);
            METRICS.saveMetrics();
        }
    }
    
    auto value = METRICS.getMetric(metric_name, true);
    TEST_ASSERT_EQUAL(1000, value.counter);
}

int runUnityTests() {
    UNITY_BEGIN();
    
    RUN_TEST(test_counter_metrics);
    RUN_TEST(test_gauge_metrics);
    RUN_TEST(test_histogram_metrics);
    RUN_TEST(test_metric_history);
    RUN_TEST(test_system_metrics);
    RUN_TEST(test_metric_timer);
    RUN_TEST(test_error_handling);
    RUN_TEST(test_concurrent_access);
    
    return UNITY_END();
}

#ifdef ARDUINO
void setup() {
    delay(2000);
    runUnityTests();
}

void loop() {
}
#else
int main() {
    return runUnityTests();
}
#endif