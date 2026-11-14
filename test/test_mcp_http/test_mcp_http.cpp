// test_mcp_http.cpp — Streamable HTTP session table, Origin validation and
// the MCP tool catalogue (issue #13).  Pure logic.

#include <unity.h>
#include <ArduinoJson.h>
#include <set>
#include "McpHttpSession.h"
#include "McpTools.h"

using namespace mcp;
using namespace mcp::http;

static uint32_t counter;
static uint32_t rnd() { return counter++ * 2654435761u; }

void setUp(void) { counter = 1; }
void tearDown(void) {}

// --- sessions -----------------------------------------------------------------

void test_session_create_and_find(void) {
    SessionTable t(2, 1000);
    const Session* s = t.create(auth::Access::Full, 0, rnd);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL(32, (int)s->id.size());
    TEST_ASSERT_TRUE(s->clientId >= MCP_HTTP_CLIENT_BASE);
    std::string id = s->id;
    const Session* f = t.find(id, 500);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(f->access == auth::Access::Full);
    TEST_ASSERT_NULL(t.find("not-a-session", 500));
    TEST_ASSERT_NULL(t.find("", 500));
}

void test_session_ids_and_client_ids_are_distinct(void) {
    SessionTable t(3, 1000);
    std::set<std::string> ids;
    std::set<int> cids;
    for (int i = 0; i < 3; ++i) {
        const Session* s = t.create(auth::Access::ReadOnly, 0, rnd);
        TEST_ASSERT_NOT_NULL(s);
        ids.insert(s->id);
        cids.insert(s->clientId);
    }
    TEST_ASSERT_EQUAL(3, (int)ids.size());
    TEST_ASSERT_EQUAL(3, (int)cids.size());
}

void test_session_capacity(void) {
    SessionTable t(1, 1000);
    TEST_ASSERT_NOT_NULL(t.create(auth::Access::Full, 0, rnd));
    TEST_ASSERT_NULL(t.create(auth::Access::Full, 10, rnd));
}

void test_session_idle_expiry_frees_slot(void) {
    SessionTable t(1, 1000);
    std::string id = t.create(auth::Access::Full, 0, rnd)->id;
    TEST_ASSERT_NULL(t.find(id, 1500));            // expired
    auto gone = t.expire(1500);
    TEST_ASSERT_EQUAL(1, (int)gone.size());
    TEST_ASSERT_EQUAL(MCP_HTTP_CLIENT_BASE, gone[0]);
    TEST_ASSERT_NOT_NULL(t.create(auth::Access::Full, 1500, rnd));
}

void test_session_create_never_reclaims_silently(void) {
    // An expired session's client id must come back through expire() so the
    // server state is released; create() must not reuse the slot on its own.
    SessionTable t(1, 1000);
    t.create(auth::Access::Full, 0, rnd);
    TEST_ASSERT_NULL(t.create(auth::Access::Full, 5000, rnd));
    TEST_ASSERT_EQUAL(1, (int)t.expire(5000).size());
    TEST_ASSERT_NOT_NULL(t.create(auth::Access::Full, 5000, rnd));
}

void test_session_use_keeps_it_alive(void) {
    SessionTable t(1, 1000);
    std::string id = t.create(auth::Access::Full, 0, rnd)->id;
    TEST_ASSERT_NOT_NULL(t.find(id, 900));
    TEST_ASSERT_NOT_NULL(t.find(id, 1800));
    TEST_ASSERT_EQUAL(0, (int)t.expire(1800).size());
}

void test_session_remove(void) {
    SessionTable t(2, 1000);
    const Session* s = t.create(auth::Access::Full, 0, rnd);
    std::string id = s->id;
    int cid = s->clientId;
    TEST_ASSERT_EQUAL(cid, t.remove(id));
    TEST_ASSERT_EQUAL(-1, t.remove(id));
    TEST_ASSERT_NULL(t.find(id, 1));
    TEST_ASSERT_EQUAL(0, (int)t.size());
}

// --- origin -------------------------------------------------------------------

void test_host_only(void) {
    TEST_ASSERT_EQUAL_STRING("192.168.1.20", hostOnly("http://192.168.1.20:80/app").c_str());
    TEST_ASSERT_EQUAL_STRING("esp32.local", hostOnly("ESP32.local:80").c_str());
    TEST_ASSERT_EQUAL_STRING("[fe80::1]", hostOnly("http://[fe80::1]:80").c_str());
    TEST_ASSERT_EQUAL_STRING("host", hostOnly("https://user:pw@host:8443/x").c_str());
}

void test_origin_allowed(void) {
    TEST_ASSERT_TRUE(originAllowed("", "192.168.1.20"));                       // no Origin
    TEST_ASSERT_TRUE(originAllowed("http://192.168.1.20", "192.168.1.20"));    // same host
    TEST_ASSERT_TRUE(originAllowed("http://esp32.local:8080", "esp32.local")); // port ignored
    TEST_ASSERT_FALSE(originAllowed("http://evil.example", "192.168.1.20"));   // DNS rebinding
    TEST_ASSERT_FALSE(originAllowed("null", "192.168.1.20"));
    TEST_ASSERT_FALSE(originAllowed("http://192.168.1.20.evil.example", "192.168.1.20"));
}

// --- tool catalogue -------------------------------------------------------------

void test_tool_schemas_are_valid_object_schemas(void) {
    MCPServer server(9000);
    registerMcpTools(server);
    TEST_ASSERT_TRUE(server.toolCount() >= 15);
    std::set<std::string> names;
    for (const auto& t : server.tools()) {
        JsonDocument d;
        DeserializationError err = deserializeJson(d, t.inputSchema);
        if (err) TEST_FAIL_MESSAGE(("invalid schema JSON for " + t.name).c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE("object", d["type"] | "", t.name.c_str());
        TEST_ASSERT_TRUE_MESSAGE(d["properties"].is<JsonObject>(), t.name.c_str());
        // MCP tool names: letters, digits, underscore, hyphen; unique.
        for (char c : t.name)
            TEST_ASSERT_TRUE(isalnum((unsigned char)c) || c == '_' || c == '-');
        TEST_ASSERT_TRUE_MESSAGE(names.insert(t.name).second, t.name.c_str());
        TEST_ASSERT_FALSE(t.description.empty());
        // Destructive implies not read-only.
        if (t.destructive) TEST_ASSERT_FALSE(t.readOnly);
    }
}

void test_read_only_tokens_only_reach_read_only_tools(void) {
    // Every tool a read-only token can call must be annotated read-only and
    // non-destructive; otherwise the whitelist and the hints disagree.
    MCPServer server(9000);
    registerMcpTools(server);
    for (const auto& t : server.tools()) {
        if (!auth::methodAllowed(auth::Access::ReadOnly, t.method)) continue;
        TEST_ASSERT_TRUE_MESSAGE(t.readOnly, t.name.c_str());
        TEST_ASSERT_FALSE_MESSAGE(t.destructive, t.name.c_str());
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_session_create_and_find);
    RUN_TEST(test_session_ids_and_client_ids_are_distinct);
    RUN_TEST(test_session_capacity);
    RUN_TEST(test_session_idle_expiry_frees_slot);
    RUN_TEST(test_session_create_never_reclaims_silently);
    RUN_TEST(test_session_use_keeps_it_alive);
    RUN_TEST(test_session_remove);
    RUN_TEST(test_host_only);
    RUN_TEST(test_origin_allowed);
    RUN_TEST(test_tool_schemas_are_valid_object_schemas);
    RUN_TEST(test_read_only_tokens_only_reach_read_only_tools);
    return UNITY_END();
}
