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
// Tests
// ---------------------------------------------------------------------------

void test_payload_contains_required_fields() {
    DiscoveryConfig cfg;
    cfg.hostname = "test-device";
    cfg.mcpPort  = 9000;
    cfg.httpPort = 80;
    dm.begin(cfg);
    dm.onNetworkConnected("192.168.1.100");

    String payload = dm.buildBroadcastPayload();
    JsonDocument doc;
    parsePayload(payload, doc);

    TEST_ASSERT_EQUAL_STRING("test-device",       doc["hostname"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("test-device.local", doc["fqdn"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("192.168.1.100",     doc["ip"].as<const char*>());
    TEST_ASSERT_EQUAL(9000, doc["mcpPort"].as<int>());
    TEST_ASSERT_EQUAL(80,   doc["httpPort"].as<int>());
    TEST_ASSERT_NOT_NULL(doc["device"].as<const char*>());
    TEST_ASSERT_NOT_NULL(doc["version"].as<const char*>());
}

void test_capabilities_array_present() {
    DiscoveryConfig cfg;
    cfg.hostname = "cap-test";
    dm.begin(cfg);
    dm.onNetworkConnected("10.0.0.1");

    String payload = dm.buildBroadcastPayload();
    JsonDocument doc;
    parsePayload(payload, doc);

    TEST_ASSERT_TRUE(doc["capabilities"].is<JsonArray>());
    TEST_ASSERT_GREATER_THAN(0, (int)doc["capabilities"].as<JsonArray>().size());
}

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
    dm.setHostname("same-host"); // should not crash or change anything
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
    // After disconnect the cached IP should be cleared.
    TEST_ASSERT_EQUAL_STRING("", doc["ip"].as<const char*>());
}

void test_broadcast_disabled_when_interval_zero() {
    DiscoveryConfig cfg;
    cfg.hostname = "no-broadcast";
    cfg.broadcastInterval = 0;
    dm.begin(cfg);
    // update() should be a no-op (no crash) when interval == 0.
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
    RUN_TEST(test_payload_contains_required_fields);
    RUN_TEST(test_capabilities_array_present);
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
