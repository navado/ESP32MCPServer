#include <unity.h>
#include "NetworkManager.h"
#include "mock_wifi.h"  // minimal stub, real WiFi types come from test/stubs/WiFi.h

void setUp(void) {}
void tearDown(void) {}

// Test that a freshly-constructed manager is not connected.
void test_network_manager_init() {
    NetworkManager manager;
    TEST_ASSERT_FALSE(manager.isConnected());
    TEST_ASSERT_EQUAL_STRING("", manager.getSSID().c_str());
}

// Test AP mode: begin() with no saved credentials should queue START_AP;
// calling handleClient() processes it and moves the manager to AP_MODE.
void test_network_manager_ap_mode() {
    NetworkManager manager;
    manager.begin();         // queues START_AP (no stored credentials)
    manager.handleClient();  // processes START_AP -> state = AP_MODE

    TEST_ASSERT_FALSE(manager.isConnected());
    TEST_ASSERT_TRUE(manager.getIPAddress().startsWith("192.168.4."));
    TEST_ASSERT_TRUE(manager.getSSID().startsWith("ESP32_"));
}

// Test that saving credentials stores the SSID and queues a connect attempt.
void test_network_manager_save_credentials() {
    NetworkManager manager;
    manager.saveCredentials("TestSSID", "TestPassword");
    // saveCredentials queues CONNECT; process it so credentials are applied.
    manager.handleClient();  // CONNECT -> connect() -> state = CONNECTING

    // getSSID() in non-AP state returns credentials.ssid
    TEST_ASSERT_EQUAL_STRING("TestSSID", manager.getSSID().c_str());
    TEST_ASSERT_FALSE(manager.isConnected());  // WiFi stub never connects
}

// Test that begin() followed by handleClient() does not crash and produces
// a valid IP string.
void test_network_manager_no_crash_on_begin() {
    NetworkManager manager;
    manager.begin();
    manager.handleClient();
    String ip = manager.getIPAddress();
    TEST_ASSERT_TRUE(ip.length() > 0);
}

int runUnityTests() {
    UNITY_BEGIN();

    RUN_TEST(test_network_manager_init);
    RUN_TEST(test_network_manager_ap_mode);
    RUN_TEST(test_network_manager_save_credentials);
    RUN_TEST(test_network_manager_no_crash_on_begin);

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
