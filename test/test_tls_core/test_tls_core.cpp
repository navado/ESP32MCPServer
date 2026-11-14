// test_tls_core.cpp — certificate SAN encoding and fingerprint format (#23).
#include <unity.h>
#include "TlsCore.h"

using namespace mcp::tls;

void setUp(void) {}
void tearDown(void) {}

void test_parse_ipv4(void) {
    std::array<uint8_t, 4> ip{};
    TEST_ASSERT_TRUE(parseIPv4("10.42.0.93", ip));
    TEST_ASSERT_EQUAL(10, ip[0]); TEST_ASSERT_EQUAL(93, ip[3]);
    TEST_ASSERT_FALSE(parseIPv4("10.42.0", ip));
    TEST_ASSERT_FALSE(parseIPv4("10.42.0.256", ip));
    TEST_ASSERT_FALSE(parseIPv4("10.42.0.1x", ip));
    TEST_ASSERT_FALSE(parseIPv4("", ip));
}

void test_san_der(void) {
    std::array<uint8_t, 4> ip{};
    parseIPv4("10.42.0.93", ip);
    auto der = subjectAltNameDer({"a.local"}, {ip});
    const uint8_t want[] = {0x30, 0x0F, 0x82, 0x07, 'a', '.', 'l', 'o', 'c', 'a', 'l',
                            0x87, 0x04, 10, 42, 0, 93};
    TEST_ASSERT_EQUAL(sizeof(want), der.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, der.data(), sizeof(want));
    // empty names are skipped
    auto none = subjectAltNameDer({""}, {});
    TEST_ASSERT_EQUAL(2, none.size());
}

void test_san_der_long_form_length(void) {
    std::string name(200, 'x');
    auto der = subjectAltNameDer({name}, {});
    // outer: 30 81 CB (203 = 2+1 header bytes + 200); inner: 82 81 C8
    TEST_ASSERT_EQUAL(0x30, der[0]);
    TEST_ASSERT_EQUAL(0x81, der[1]);
    TEST_ASSERT_EQUAL(203, der[2]);
    TEST_ASSERT_EQUAL(0x82, der[3]);
    TEST_ASSERT_EQUAL(0x81, der[4]);
    TEST_ASSERT_EQUAL(200, der[5]);
    TEST_ASSERT_EQUAL(3 + 3 + 200, der.size());
}

void test_fingerprint_hex(void) {
    const uint8_t d[] = {0x00, 0xAB, 0x0F};
    TEST_ASSERT_EQUAL_STRING("00:AB:0F", fingerprintHex(d, 3).c_str());
    TEST_ASSERT_EQUAL_STRING("", fingerprintHex(d, 0).c_str());
}

void test_ws_close_payload(void) {
    std::string p = wsClosePayload(4401, "Unauthorized");
    TEST_ASSERT_EQUAL(0x11, (uint8_t)p[0]);   // 4401 = 0x1131
    TEST_ASSERT_EQUAL(0x31, (uint8_t)p[1]);
    TEST_ASSERT_EQUAL_STRING("Unauthorized", p.substr(2).c_str());
    TEST_ASSERT_TRUE(wsClosePayload(1000, std::string(300, 'r')).size() <= 125);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_ipv4);
    RUN_TEST(test_san_der);
    RUN_TEST(test_san_der_long_form_length);
    RUN_TEST(test_fingerprint_hex);
    RUN_TEST(test_ws_close_payload);
    return UNITY_END();
}
