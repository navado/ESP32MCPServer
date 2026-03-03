#include <unity.h>
#include <ArduinoJson.h>
#include "DiscoveryManager.h"

// One shared instance — each test re-calls begin() to reset state.
static DiscoveryManager dm;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Helper: parse a broadcast payload into a JsonDocument.
// ---------------------------------------------------------------------------
static void parsePayload(const String& payload, JsonDocument& doc) {
    DeserializationError err = deserializeJson(doc, payload);
    TEST_ASSERT_EQUAL((int)DeserializationError::Ok, (int)err.code());
}

// ---------------------------------------------------------------------------
// Helpers: build a small sensor list
// ---------------------------------------------------------------------------
static std::vector<DiscoverySensorInfo> makeSensors() {
    DiscoverySensorInfo bme;
    bme.id         = "bme280_0x76";
    bme.type       = "BME280";
    bme.address    = 0x76;
    bme.parameters = {"temperature", "humidity", "pressure"};

    DiscoverySensorInfo mpu;
    mpu.id         = "mpu6050_0x68";
    mpu.type       = "MPU6050";
    mpu.address    = 0x68;
    mpu.parameters = {"accel_x", "accel_y", "accel_z"};

    return {bme, mpu};
}

// ---------------------------------------------------------------------------
// Tests — identity fields
// ---------------------------------------------------------------------------

void test_payload_identity_fields() {
    DiscoveryConfig cfg;
    cfg.hostname = "test-device";
    cfg.mcpPort  = 9000;
    cfg.httpPort = 80;
    dm.begin(cfg);
    dm.onNetworkConnected("192.168.1.100");

    String payload = dm.buildBroadcastPayload();
    JsonDocument doc;
    parsePayload(payload, doc);

    TEST_ASSERT_EQUAL_STRING("mcp-v1",        doc["discovery"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("test-device",   doc["hostname"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("test-device.local", doc["fqdn"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", doc["ip"].as<const char*>());
    TEST_ASSERT_EQUAL(9000, doc["mcpPort"].as<int>());
    TEST_ASSERT_EQUAL(80,   doc["httpPort"].as<int>());
    TEST_ASSERT_NOT_NULL(doc["device"].as<const char*>());
}

// ---------------------------------------------------------------------------
// Tests — MCP capability object
// ---------------------------------------------------------------------------

void test_payload_capabilities_object() {
    DiscoveryConfig cfg;
    cfg.hostname = "cap-test";
    dm.begin(cfg);
    dm.onNetworkConnected("10.0.0.1");

    String payload = dm.buildBroadcastPayload();
    JsonDocument doc;
    parsePayload(payload, doc);

    // capabilities is now an object (mirrors MCP initialize response)
    TEST_ASSERT_TRUE(doc["capabilities"].is<JsonObject>());
}

void test_payload_methods_array_present() {
    DiscoveryConfig cfg;
    cfg.hostname = "methods-test";
    dm.begin(cfg);
    dm.onNetworkConnected("10.0.0.1");

    String payload = dm.buildBroadcastPayload();
    // Verify methods array is present via substring search (avoids deep-parse)
    TEST_ASSERT_TRUE(payload.indexOf("\"methods\"") >= 0);
    TEST_ASSERT_TRUE(payload.indexOf("\"initialize\"") >= 0);
    TEST_ASSERT_TRUE(payload.indexOf("\"sensors/i2c/scan\"") >= 0);
    TEST_ASSERT_TRUE(payload.indexOf("\"metrics/list\"") >= 0);
}

void test_payload_server_info_present() {
    DiscoveryConfig cfg;
    cfg.hostname = "si-test";
    dm.begin(cfg);
    dm.onNetworkConnected("10.0.0.1");

    String payload = dm.buildBroadcastPayload();
    TEST_ASSERT_TRUE(payload.indexOf("\"serverInfo\"") >= 0);
    TEST_ASSERT_TRUE(payload.indexOf("\"version\"") >= 0);
}

// ---------------------------------------------------------------------------
// Tests — sensor advertisement
// ---------------------------------------------------------------------------

void test_sensor_count_default_zero() {
    DiscoveryConfig cfg;
    cfg.hostname = "sens-zero";
    dm.begin(cfg);
    TEST_ASSERT_EQUAL(0, (int)dm.sensorCount());
}

void test_set_sensors_updates_count() {
    DiscoveryConfig cfg;
    cfg.hostname = "sens-count";
    dm.begin(cfg);
    dm.setSensors(makeSensors());
    TEST_ASSERT_EQUAL(2, (int)dm.sensorCount());
}

void test_clear_sensors_resets_count() {
    DiscoveryConfig cfg;
    cfg.hostname = "sens-clear";
    dm.begin(cfg);
    dm.setSensors(makeSensors());
    dm.clearSensors();
    TEST_ASSERT_EQUAL(0, (int)dm.sensorCount());
}

void test_payload_sensors_array_present() {
    DiscoveryConfig cfg;
    cfg.hostname = "sens-payload";
    dm.begin(cfg);
    dm.onNetworkConnected("10.0.0.5");
    dm.setSensors(makeSensors());

    String payload = dm.buildBroadcastPayload();
    TEST_ASSERT_TRUE(payload.indexOf("\"sensors\"")   >= 0);
    TEST_ASSERT_TRUE(payload.indexOf("bme280_0x76")   >= 0);
    TEST_ASSERT_TRUE(payload.indexOf("BME280")        >= 0);
    TEST_ASSERT_TRUE(payload.indexOf("temperature")   >= 0);
    TEST_ASSERT_TRUE(payload.indexOf("mpu6050_0x68")  >= 0);
}

void test_payload_sensors_empty_array_when_no_sensors() {
    DiscoveryConfig cfg;
    cfg.hostname = "sens-empty";
    dm.begin(cfg);
    dm.onNetworkConnected("10.0.0.6");

    String payload = dm.buildBroadcastPayload();
    // sensors key must exist (empty array is valid)
    TEST_ASSERT_TRUE(payload.indexOf("\"sensors\"") >= 0);
}

void test_announce_noop_when_not_connected() {
    DiscoveryConfig cfg;
    cfg.hostname = "ann-noop";
    dm.begin(cfg);
    // Must not crash; no network is up yet.
    dm.announceCapabilityChange();
    TEST_ASSERT_FALSE(dm.isMdnsActive());
}

void test_announce_triggers_mdns_restart_when_connected() {
    DiscoveryConfig cfg;
    cfg.hostname = "ann-mdns";
    dm.begin(cfg);
    dm.onNetworkConnected("10.0.0.7");
    TEST_ASSERT_TRUE(dm.isMdnsActive());
    // Trigger a capability change (e.g. sensors added)
    dm.setSensors(makeSensors());
    // mDNS should have been restarted and remain active
    TEST_ASSERT_TRUE(dm.isMdnsActive());
}

// ---------------------------------------------------------------------------
// Tests — existing config behaviour
// ---------------------------------------------------------------------------

void test_default_broadcast_interval() {
    DiscoveryConfig cfg;
    cfg.hostname = "interval-test";
    dm.begin(cfg);
    TEST_ASSERT_EQUAL(30000, (int)dm.getConfig().broadcastInterval);
}

void test_set_broadcast_interval() {
    DiscoveryConfig cfg;
    cfg.hostname = "interval-test-2";
    dm.begin(cfg);
    dm.setBroadcastInterval(60000);
    TEST_ASSERT_EQUAL(60000, (int)dm.getConfig().broadcastInterval);
}

void test_set_hostname_updates_config() {
    DiscoveryConfig cfg;
    cfg.hostname = "original-host";
    dm.begin(cfg);
    dm.setHostname("new-host");
    TEST_ASSERT_EQUAL_STRING("new-host", dm.getConfig().hostname.c_str());
}

void test_set_hostname_noop_same_value() {
    DiscoveryConfig cfg;
    cfg.hostname = "same-host";
    dm.begin(cfg);
    dm.setHostname("same-host");
    TEST_ASSERT_EQUAL_STRING("same-host", dm.getConfig().hostname.c_str());
}

void test_mdns_not_active_before_connect() {
    DiscoveryConfig cfg;
    cfg.hostname = "mdns-test";
    dm.begin(cfg);
    TEST_ASSERT_FALSE(dm.isMdnsActive());
}

void test_mdns_active_after_connect() {
    DiscoveryConfig cfg;
    cfg.hostname = "mdns-test-2";
    dm.begin(cfg);
    dm.onNetworkConnected("10.0.0.2");
    TEST_ASSERT_TRUE(dm.isMdnsActive());
}

void test_mdns_inactive_after_disconnect() {
    DiscoveryConfig cfg;
    cfg.hostname = "mdns-test-3";
    dm.begin(cfg);
    dm.onNetworkConnected("10.0.0.3");
    dm.onNetworkDisconnected();
    TEST_ASSERT_FALSE(dm.isMdnsActive());
}

void test_payload_ip_empty_when_disconnected() {
    DiscoveryConfig cfg;
    cfg.hostname = "ip-test";
    dm.begin(cfg);
    dm.onNetworkConnected("172.16.0.5");
    dm.onNetworkDisconnected();

    String payload = dm.buildBroadcastPayload();
    JsonDocument doc;
    parsePayload(payload, doc);
    TEST_ASSERT_EQUAL_STRING("", doc["ip"].as<const char*>());
}

void test_broadcast_disabled_when_interval_zero() {
    DiscoveryConfig cfg;
    cfg.hostname = "no-broadcast";
    cfg.broadcastInterval = 0;
    dm.begin(cfg);
    dm.onNetworkConnected("192.168.0.1");
    dm.update(); // must not crash
    TEST_ASSERT_EQUAL(0, (int)dm.getConfig().broadcastInterval);
}

void test_config_ports() {
    DiscoveryConfig cfg;
    cfg.hostname  = "port-test";
    cfg.mcpPort   = 8888;
    cfg.httpPort  = 8080;
    dm.begin(cfg);

    DiscoveryConfig out = dm.getConfig();
    TEST_ASSERT_EQUAL(8888, (int)out.mcpPort);
    TEST_ASSERT_EQUAL(8080, (int)out.httpPort);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main() {
    UNITY_BEGIN();
    // Identity
    RUN_TEST(test_payload_identity_fields);
    RUN_TEST(test_payload_capabilities_object);
    RUN_TEST(test_payload_methods_array_present);
    RUN_TEST(test_payload_server_info_present);
    // Sensor advertisement
    RUN_TEST(test_sensor_count_default_zero);
    RUN_TEST(test_set_sensors_updates_count);
    RUN_TEST(test_clear_sensors_resets_count);
    RUN_TEST(test_payload_sensors_array_present);
    RUN_TEST(test_payload_sensors_empty_array_when_no_sensors);
    RUN_TEST(test_announce_noop_when_not_connected);
    RUN_TEST(test_announce_triggers_mdns_restart_when_connected);
    // Config
    RUN_TEST(test_default_broadcast_interval);
    RUN_TEST(test_set_broadcast_interval);
    RUN_TEST(test_set_hostname_updates_config);
    RUN_TEST(test_set_hostname_noop_same_value);
    RUN_TEST(test_mdns_not_active_before_connect);
    RUN_TEST(test_mdns_active_after_connect);
    RUN_TEST(test_mdns_inactive_after_disconnect);
    RUN_TEST(test_payload_ip_empty_when_disconnected);
    RUN_TEST(test_broadcast_disabled_when_interval_zero);
    RUN_TEST(test_config_ports);
    return UNITY_END();
}
