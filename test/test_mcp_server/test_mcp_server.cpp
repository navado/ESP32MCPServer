#include <unity.h>
#include "MCPServer.h"
#include <string>
#include <memory>
#include "mock/mock_websocket.h"

using namespace mcp;

MCPServer* server = nullptr;
MockWebSocket* mockWs = nullptr;

void setUp(void) {
    server = new MCPServer(9000);
    server->begin(true);
    mockWs = new MockWebSocket(server);
}

void tearDown(void) {
    delete server;
    delete mockWs;
}

void test_server_initialization() {
    // Test basic initialization
    TEST_ASSERT_NOT_NULL(server);
    
    // Verify server capabilities
    const char* initRequest = R"({
        "jsonrpc": "2.0",
        "method": "initialize",
        "id": 1
    })";
    
    std::string response = mockWs->simulateMessage(1, initRequest);
    
    // Verify response contains expected fields
    TEST_ASSERT_TRUE(response.find("protocolVersion") != std::string::npos);
    TEST_ASSERT_TRUE(response.find("serverInfo") != std::string::npos);
    TEST_ASSERT_TRUE(response.find("capabilities") != std::string::npos);
}

void test_resource_registration() {
    MCPResource testResource("test", "test://resource", "text/plain", "Test resource");
    server->registerResource(testResource);
    
    // List resources
    const char* listRequest = R"({
        "jsonrpc": "2.0",
        "method": "resources/list",
        "id": 2
    })";
    
    std::string response = mockWs->simulateMessage(1, listRequest);
    
    // Verify test resource is in the list
    TEST_ASSERT_TRUE(response.find("test://resource") != std::string::npos);
    TEST_ASSERT_TRUE(response.find("Test resource") != std::string::npos);
}

void test_resource_read() {
    MCPResource testResource("test", "test://data", "application/json", "Test data");
    server->registerResource(testResource);
    
    const char* readRequest = R"({
        "jsonrpc": "2.0",
        "method": "resources/read",
        "params": {"uri": "test://data"},
        "id": 3
    })";
    
    std::string response = mockWs->simulateMessage(1, readRequest);
    
    // Verify response structure
    TEST_ASSERT_TRUE(response.find("success") != std::string::npos);
    TEST_ASSERT_TRUE(response.find("test://data") != std::string::npos);
}

void test_resource_subscription() {
    MCPResource testResource("test", "test://subscribe", "application/json", "Test subscription");
    server->registerResource(testResource);
    
    // Subscribe to resource
    const char* subscribeRequest = R"({
        "jsonrpc": "2.0",
        "method": "resources/subscribe",
        "params": {"uri": "test://subscribe"},
        "id": 4
    })";
    
    std::string response = mockWs->simulateMessage(1, subscribeRequest);
    TEST_ASSERT_TRUE(response.find("success") != std::string::npos);
    
    // Verify notification is sent when resource updates
    server->broadcastResourceUpdate("test://subscribe");
    std::string notification = mockWs->getLastNotification();
    TEST_ASSERT_TRUE(notification.find("notifications/resources/updated") != std::string::npos);
}

void test_error_handling() {
    // Test invalid method
    const char* invalidRequest = R"({
        "jsonrpc": "2.0",
        "method": "invalid_method",
        "id": 5
    })";
    
    std::string response = mockWs->simulateMessage(1, invalidRequest);
    TEST_ASSERT_TRUE(response.find("error") != std::string::npos);
    TEST_ASSERT_TRUE(response.find("Method not found") != std::string::npos);
    
    // Test invalid resource URI
    const char* invalidResourceRequest = R"({
        "jsonrpc": "2.0",
        "method": "resources/read",
        "params": {"uri": "invalid://uri"},
        "id": 6
    })";
    
    response = mockWs->simulateMessage(1, invalidResourceRequest);
    TEST_ASSERT_TRUE(response.find("error") != std::string::npos);
    TEST_ASSERT_TRUE(response.find("Resource not found") != std::string::npos);
}

void test_concurrent_clients() {
    MCPResource testResource("test", "test://concurrent", "application/json", "Test concurrent access");
    server->registerResource(testResource);
    
    // Subscribe multiple clients
    const char* subscribeRequest = R"({
        "jsonrpc": "2.0",
        "method": "resources/subscribe",
        "params": {"uri": "test://concurrent"},
        "id": 7
    })";
    
    std::string response1 = mockWs->simulateMessage(1, subscribeRequest);
    std::string response2 = mockWs->simulateMessage(2, subscribeRequest);
    
    TEST_ASSERT_TRUE(response1.find("success") != std::string::npos);
    TEST_ASSERT_TRUE(response2.find("success") != std::string::npos);
    
    // Verify both clients receive updates
    server->broadcastResourceUpdate("test://concurrent");
    std::vector<std::string> notifications = mockWs->getAllNotifications();
    TEST_ASSERT_EQUAL(2, notifications.size());
}

int runUnityTests() {
    UNITY_BEGIN();
    
    RUN_TEST(test_server_initialization);
    RUN_TEST(test_resource_registration);
    RUN_TEST(test_resource_read);
    RUN_TEST(test_resource_subscription);
    RUN_TEST(test_error_handling);
    RUN_TEST(test_concurrent_clients);
    
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