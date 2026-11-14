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

void test_auth_off_admits_without_token_with_full_access() {
    auth::Credentials c = secCreds();
    c.required = false;
    server->setCredentials(c);
    TEST_ASSERT_TRUE(server->admitClient(1, "", "/") == MCPServer::Admission::Accepted);
    TEST_ASSERT_TRUE(server->clientAccess(1) == auth::Access::Full);
    std::string resp = mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"resources/list","id":39})");
    TEST_ASSERT_TRUE(resp.find("\"resources\"") != std::string::npos);
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

// --- MCP spec compliance (issue #13) ----------------------------------------

static JsonDocument parse(const std::string& s) {
    JsonDocument d;
    deserializeJson(d, s);
    return d;
}

void test_initialize_echoes_supported_version() {
    JsonDocument r = parse(mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-03-26"}})"));
    TEST_ASSERT_EQUAL_STRING("2025-03-26", r["result"]["protocolVersion"]);
}

void test_initialize_offers_latest_for_unknown_version() {
    JsonDocument r = parse(mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"1999-01-01"}})"));
    TEST_ASSERT_EQUAL_STRING(MCP_LATEST_PROTOCOL_VERSION, r["result"]["protocolVersion"]);
    // Pre-spec clients that send no version still work.
    r = parse(mockWs->simulateMessage(1, R"({"jsonrpc":"2.0","method":"initialize","id":2})"));
    TEST_ASSERT_EQUAL_STRING(MCP_LATEST_PROTOCOL_VERSION, r["result"]["protocolVersion"]);
}

void test_initialize_rejects_malformed_version() {
    JsonDocument r = parse(mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"initialize","id":3,"params":{"protocolVersion":42}})"));
    TEST_ASSERT_EQUAL(-32602, r["error"]["code"].as<int>());
    TEST_ASSERT_TRUE(r["error"]["data"]["supported"].is<JsonArray>());
}

void test_initialize_capability_objects() {
    JsonDocument r = parse(mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"initialize","id":4,"params":{"protocolVersion":"2025-06-18"}})"));
    JsonObject caps = r["result"]["capabilities"];
    TEST_ASSERT_TRUE(caps["resources"].is<JsonObject>());
    TEST_ASSERT_TRUE(caps["resources"]["subscribe"].as<bool>());
    TEST_ASSERT_TRUE(caps["tools"].is<JsonObject>());
    TEST_ASSERT_TRUE(r["result"]["serverInfo"]["name"].is<const char*>());

    // HTTP sessions cannot receive pushes, so they are not offered subscribe.
    r = parse(mockWs->simulateMessage(MCP_HTTP_CLIENT_BASE,
        R"({"jsonrpc":"2.0","method":"initialize","id":5})"));
    TEST_ASSERT_FALSE(r["result"]["capabilities"]["resources"]["subscribe"].as<bool>());
}

void test_notifications_get_no_response() {
    TEST_ASSERT_EQUAL_STRING("", mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})").c_str());
    TEST_ASSERT_EQUAL_STRING("", mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":3}})").c_str());
    // Even an unknown method sent as a notification is not answered.
    TEST_ASSERT_EQUAL_STRING("", mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"nope"})").c_str());
    // A stray response from the client is ignored.
    TEST_ASSERT_EQUAL_STRING("", mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","id":9,"result":{}})").c_str());
}

void test_ping() {
    JsonDocument r = parse(mockWs->simulateMessage(1, R"({"jsonrpc":"2.0","method":"ping","id":6})"));
    TEST_ASSERT_EQUAL(6, r["id"].as<int>());
    TEST_ASSERT_TRUE(r["result"].is<JsonObject>());
}

void test_string_ids_round_trip() {
    JsonDocument r = parse(mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"ping","id":"abc-1"})"));
    TEST_ASSERT_EQUAL_STRING("abc-1", r["id"]);
    r = parse(mockWs->simulateMessage(1, R"({"jsonrpc":"2.0","method":"nope","id":"x"})"));
    TEST_ASSERT_EQUAL_STRING("x", r["id"]);
    TEST_ASSERT_EQUAL(-32601, r["error"]["code"].as<int>());
}

void test_resources_read_spec_shape() {
    server->registerResource(MCPResource("cfg", "test://cfg", "application/json", "{\"a\":1}"));
    JsonDocument r = parse(mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"resources/read","id":7,"params":{"uri":"test://cfg"}})"));
    JsonObject c = r["result"]["contents"][0];
    TEST_ASSERT_EQUAL_STRING("test://cfg", c["uri"]);
    TEST_ASSERT_EQUAL_STRING("application/json", c["mimeType"]);
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", c["text"]);
}

static void registerEchoTool() {
    server->registerMethodHandler("test/echo",
        [](uint8_t, uint32_t id, const JsonObject& p) -> std::string {
            JsonDocument d;
            d["jsonrpc"] = "2.0";
            d["id"] = id;
            d["result"]["echo"] = p["msg"] | "none";
            std::string out; serializeJson(d, out); return out;
        });
    server->registerMethodHandler("test/fail",
        [](uint8_t, uint32_t id, const JsonObject&) -> std::string {
            return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                   ",\"error\":{\"code\":-32010,\"message\":\"Feature busy\",\"data\":{\"state\":\"scanning\"}}}";
        });
    ToolDef echo;
    echo.name = "echo";
    echo.description = "Echo a message";
    echo.inputSchema = R"({"type":"object","properties":{"msg":{"type":"string"}}})";
    echo.method = "test/echo";
    server->registerTool(echo);
    ToolDef fail;
    fail.name = "fail";
    fail.description = "Always busy";
    fail.method = "test/fail";
    fail.readOnly = false;
    fail.destructive = true;
    server->registerTool(fail);
}

void test_tools_list() {
    registerEchoTool();
    JsonDocument r = parse(mockWs->simulateMessage(1, R"({"jsonrpc":"2.0","method":"tools/list","id":8})"));
    JsonArray tools = r["result"]["tools"];
    TEST_ASSERT_EQUAL(2, (int)tools.size());
    TEST_ASSERT_EQUAL_STRING("echo", tools[0]["name"]);
    TEST_ASSERT_EQUAL_STRING("object", tools[0]["inputSchema"]["type"]);
    TEST_ASSERT_TRUE(tools[0]["inputSchema"]["properties"]["msg"].is<JsonObject>());
    TEST_ASSERT_TRUE(tools[0]["annotations"]["readOnlyHint"].as<bool>());
    TEST_ASSERT_TRUE(tools[1]["annotations"]["destructiveHint"].as<bool>());
}

void test_tools_call_success() {
    registerEchoTool();
    JsonDocument r = parse(mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"tools/call","id":9,"params":{"name":"echo","arguments":{"msg":"hi"}}})"));
    TEST_ASSERT_EQUAL(9, r["id"].as<int>());
    TEST_ASSERT_FALSE(r["result"]["isError"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("hi", r["result"]["structuredContent"]["echo"]);
    TEST_ASSERT_EQUAL_STRING("text", r["result"]["content"][0]["type"]);
    TEST_ASSERT_EQUAL_STRING("{\"echo\":\"hi\"}", r["result"]["content"][0]["text"]);
}

void test_tools_call_error_is_reported_in_result() {
    registerEchoTool();
    JsonDocument r = parse(mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"tools/call","id":10,"params":{"name":"fail"}})"));
    TEST_ASSERT_TRUE(r["result"]["isError"].as<bool>());
    TEST_ASSERT_EQUAL(-32010, r["result"]["structuredContent"]["error"]["code"].as<int>());
    std::string text = r["result"]["content"][0]["text"] | "";
    TEST_ASSERT_TRUE(text.find("Feature busy") != std::string::npos);
    TEST_ASSERT_TRUE(text.find("scanning") != std::string::npos);
}

void test_tools_call_unknown_tool() {
    JsonDocument r = parse(mockWs->simulateMessage(1,
        R"({"jsonrpc":"2.0","method":"tools/call","id":11,"params":{"name":"nope"}})"));
    TEST_ASSERT_EQUAL(-32602, r["error"]["code"].as<int>());
}

void test_tools_inherit_method_authorization() {
    registerEchoTool();
    // "test/echo" is not on the read-only whitelist, so a read-only client
    // neither sees nor calls it.
    server->setCredentials(secCreds());
    server->admitClient(2, "Bearer read-0123456789abcdef", "/");
    JsonDocument r = parse(mockWs->simulateMessage(2, R"({"jsonrpc":"2.0","method":"tools/list","id":12})"));
    TEST_ASSERT_EQUAL(0, (int)r["result"]["tools"].as<JsonArray>().size());
    r = parse(mockWs->simulateMessage(2,
        R"({"jsonrpc":"2.0","method":"tools/call","id":13,"params":{"name":"echo"}})"));
    TEST_ASSERT_EQUAL(-32020, r["error"]["code"].as<int>());

    server->admitClient(1, "Bearer full-0123456789abcdef", "/");
    r = parse(mockWs->simulateMessage(1, R"({"jsonrpc":"2.0","method":"tools/list","id":14})"));
    TEST_ASSERT_EQUAL(2, (int)r["result"]["tools"].as<JsonArray>().size());
}

void test_tool_without_handler_is_hidden() {
    ToolDef ghost;
    ghost.name = "ghost";
    ghost.method = "not/registered";   // e.g. BLE compiled out
    server->registerTool(ghost);
    JsonDocument r = parse(mockWs->simulateMessage(1, R"({"jsonrpc":"2.0","method":"tools/list","id":15})"));
    TEST_ASSERT_EQUAL(0, (int)r["result"]["tools"].as<JsonArray>().size());
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
    RUN_TEST(test_auth_off_admits_without_token_with_full_access);
    RUN_TEST(test_unauthenticated_connection_is_refused);
    RUN_TEST(test_message_from_unadmitted_client_is_rejected);
    RUN_TEST(test_token_via_header_or_query_admits);
    RUN_TEST(test_read_only_client_cannot_call_full_control_methods);
    RUN_TEST(test_max_clients_enforced_and_slot_reuse_allowed);
    RUN_TEST(test_removed_client_loses_access);
    RUN_TEST(test_per_client_rate_limit);
    RUN_TEST(test_initialize_echoes_supported_version);
    RUN_TEST(test_initialize_offers_latest_for_unknown_version);
    RUN_TEST(test_initialize_rejects_malformed_version);
    RUN_TEST(test_initialize_capability_objects);
    RUN_TEST(test_notifications_get_no_response);
    RUN_TEST(test_ping);
    RUN_TEST(test_string_ids_round_trip);
    RUN_TEST(test_resources_read_spec_shape);
    RUN_TEST(test_tools_list);
    RUN_TEST(test_tools_call_success);
    RUN_TEST(test_tools_call_error_is_reported_in_result);
    RUN_TEST(test_tools_call_unknown_tool);
    RUN_TEST(test_tools_inherit_method_authorization);
    RUN_TEST(test_tool_without_handler_is_hidden);
    
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