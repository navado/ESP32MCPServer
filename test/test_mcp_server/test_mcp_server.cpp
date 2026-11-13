#include <unity.h>
#include "MCPServer.h"
#include <new>
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

void test_handler_bad_alloc_returns_error_and_server_survives() {
    // A handler that simulates heap exhaustion while building its response
    // (the ble/scan/results failure mode on a flooded BLE radio).
    server->registerMethodHandler("test/oom", [](uint8_t, uint32_t, const JsonObject&) -> std::string {
        throw std::bad_alloc();
    });

    const char* oomRequest = R"({
        "jsonrpc": "2.0",
        "method": "test/oom",
        "id": 7
    })";
    std::string response = mockWs->simulateMessage(1, oomRequest);

    // The exception must be converted to a JSON-RPC internal-error reply —
    // it must NEVER escape the dispatch call (on device it reboots the board).
    TEST_ASSERT_TRUE(response.find("\"error\"") != std::string::npos);
    TEST_ASSERT_TRUE(response.find("-32603") != std::string::npos);

    // The server must still serve normal requests afterwards.
    const char* pingRequest = R"({
        "jsonrpc": "2.0",
        "method": "resources/list",
        "id": 8
    })";
    response = mockWs->simulateMessage(1, pingRequest);
    TEST_ASSERT_TRUE(response.find("\"resources\"") != std::string::npos);
    TEST_ASSERT_TRUE(response.find("\"error\"") == std::string::npos);
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

void test_remove_client_drops_its_subscriptions() {
    MCPResource testResource("test", "test://reuse", "application/json", "Slot reuse");
    server->registerResource(testResource);

    const char* subscribeRequest = R"({
        "jsonrpc": "2.0",
        "method": "resources/subscribe",
        "params": {"uri": "test://reuse"},
        "id": 8
    })";
    mockWs->simulateMessage(1, subscribeRequest);
    mockWs->simulateMessage(2, subscribeRequest);

    // Client 1 disconnects; a new client reusing slot 1 must not inherit
    // the old subscription, while client 2 keeps receiving updates.
    server->removeClient(1);
    mockWs->clearNotifications();
    server->broadcastResourceUpdate("test://reuse");
    TEST_ASSERT_EQUAL(1, mockWs->getAllNotifications().size());

    server->removeClient(2);
    mockWs->clearNotifications();
    server->broadcastResourceUpdate("test://reuse");
    TEST_ASSERT_EQUAL(0, mockWs->getAllNotifications().size());
}

// --- transport security (issue #12) ---------------------------------------

static auth::Credentials secCreds() {
    auth::Credentials c;
    c.fullToken = "full-0123456789abcdef";
    c.readToken = "read-0123456789abcdef";
    return c;
}

void test_unauthenticated_connection_is_refused() {
    server->setCredentials(secCreds());
    TEST_ASSERT_TRUE(server->admitClient(1, "", "/") == MCPServer::Admission::Unauthorized);
    TEST_ASSERT_TRUE(server->admitClient(1, "Bearer wrong", "/") ==
                     MCPServer::Admission::Unauthorized);
    TEST_ASSERT_EQUAL(0, (int)server->clientCount());
}

void test_message_from_unadmitted_client_is_rejected() {
    server->setCredentials(secCreds());
    std::string resp = mockWs->simulateMessage(3,
        R"({"jsonrpc":"2.0","method":"resources/list","id":40})");
    TEST_ASSERT_TRUE(resp.find("-32020") != std::string::npos);
    TEST_ASSERT_TRUE(resp.find("\"resources\"") == std::string::npos);
}

void test_token_via_header_or_query_admits() {
    server->setCredentials(secCreds());
    TEST_ASSERT_TRUE(server->admitClient(1, "Bearer full-0123456789abcdef", "/") ==
                     MCPServer::Admission::Accepted);
    TEST_ASSERT_TRUE(server->admitClient(2, "", "/?token=read-0123456789abcdef") ==
                     MCPServer::Admission::Accepted);
    TEST_ASSERT_TRUE(server->clientAccess(1) == auth::Access::Full);
    TEST_ASSERT_TRUE(server->clientAccess(2) == auth::Access::ReadOnly);

    std::string resp = mockWs->simulateMessage(2,
        R"({"jsonrpc":"2.0","method":"resources/list","id":41})");
    TEST_ASSERT_TRUE(resp.find("\"resources\"") != std::string::npos);
}

void test_read_only_client_cannot_call_full_control_methods() {
    server->setCredentials(secCreds());
    bool called = false;
    server->registerMethodHandler("logs/clear",
        [&called](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            called = true;
            return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + std::to_string(id) +
                   ",\"result\":{}}";
        });
    server->admitClient(2, "Bearer read-0123456789abcdef", "/");
    std::string resp = mockWs->simulateMessage(2,
        R"({"jsonrpc":"2.0","method":"logs/clear","id":42})");
    TEST_ASSERT_FALSE(called);
    TEST_ASSERT_TRUE(resp.find("-32020") != std::string::npos);
    TEST_ASSERT_TRUE(resp.find("\"required\":\"full\"") != std::string::npos);

    server->admitClient(1, "Bearer full-0123456789abcdef", "/");
    mockWs->simulateMessage(1, R"({"jsonrpc":"2.0","method":"logs/clear","id":43})");
    TEST_ASSERT_TRUE(called);
}

void test_max_clients_enforced_and_slot_reuse_allowed() {
    server->setCredentials(secCreds());
    server->setMaxClients(2);
    const char* tok = "Bearer full-0123456789abcdef";
    TEST_ASSERT_TRUE(server->admitClient(0, tok, "/") == MCPServer::Admission::Accepted);
    TEST_ASSERT_TRUE(server->admitClient(1, tok, "/") == MCPServer::Admission::Accepted);
    TEST_ASSERT_TRUE(server->admitClient(2, tok, "/") == MCPServer::Admission::TooManyClients);
    // Re-admitting an existing slot replaces it instead of counting twice.
    TEST_ASSERT_TRUE(server->admitClient(1, tok, "/") == MCPServer::Admission::Accepted);
    server->removeClient(0);
    TEST_ASSERT_TRUE(server->admitClient(2, tok, "/") == MCPServer::Admission::Accepted);
}

void test_removed_client_loses_access() {
    server->setCredentials(secCreds());
    server->admitClient(1, "Bearer full-0123456789abcdef", "/");
    server->removeClient(1);
    std::string resp = mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"resources/list","id":44})");
    TEST_ASSERT_TRUE(resp.find("-32020") != std::string::npos);
}

void test_per_client_rate_limit() {
    uint32_t fakeNow = 1000;
    server->setClock([&fakeNow]() { return fakeNow; });
    server->setCredentials(secCreds());
    server->setRateLimit(2, 10);
    server->admitClient(1, "Bearer full-0123456789abcdef", "/");
    server->admitClient(2, "Bearer full-0123456789abcdef", "/");
    const char* req = R"({"jsonrpc":"2.0","method":"resources/list","id":45})";
    TEST_ASSERT_TRUE(mockWs->simulateMessage(1, req).find("-32021") == std::string::npos);
    TEST_ASSERT_TRUE(mockWs->simulateMessage(1, req).find("-32021") == std::string::npos);
    std::string limited = mockWs->simulateMessage(1, req);
    TEST_ASSERT_TRUE(limited.find("-32021") != std::string::npos);
    TEST_ASSERT_TRUE(limited.find("retryAfterMs") != std::string::npos);
    // Other clients have their own bucket.
    TEST_ASSERT_TRUE(mockWs->simulateMessage(2, req).find("-32021") == std::string::npos);
    fakeNow += 100;
    TEST_ASSERT_TRUE(mockWs->simulateMessage(1, req).find("-32021") == std::string::npos);
}

int runUnityTests() {
    UNITY_BEGIN();
    
    RUN_TEST(test_server_initialization);
    RUN_TEST(test_resource_registration);
    RUN_TEST(test_resource_read);
    RUN_TEST(test_resource_subscription);
    RUN_TEST(test_error_handling);
    RUN_TEST(test_handler_bad_alloc_returns_error_and_server_survives);
    RUN_TEST(test_concurrent_clients);
    RUN_TEST(test_remove_client_drops_its_subscriptions);
    RUN_TEST(test_unauthenticated_connection_is_refused);
    RUN_TEST(test_message_from_unadmitted_client_is_rejected);
    RUN_TEST(test_token_via_header_or_query_admits);
    RUN_TEST(test_read_only_client_cannot_call_full_control_methods);
    RUN_TEST(test_max_clients_enforced_and_slot_reuse_allowed);
    RUN_TEST(test_removed_client_loses_access);
    RUN_TEST(test_per_client_rate_limit);
    
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