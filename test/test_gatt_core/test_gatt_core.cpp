#include <unity.h>
#include "BLEGattCore.h"
#include <string>

using namespace mcp::gattcore;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// GattState transitions
// ---------------------------------------------------------------------------

void test_state_idle_not_connected(void) {
    TEST_ASSERT_FALSE(gattStateConnected(GattState::Idle));
    TEST_ASSERT_FALSE(gattStateConnected(GattState::Connecting));
    TEST_ASSERT_FALSE(gattStateConnected(GattState::Closing));
}

void test_state_connected_states(void) {
    TEST_ASSERT_TRUE(gattStateConnected(GattState::Connected));
    TEST_ASSERT_TRUE(gattStateConnected(GattState::Discovering));
    TEST_ASSERT_TRUE(gattStateConnected(GattState::Reading));
    TEST_ASSERT_TRUE(gattStateConnected(GattState::NotifyWait));
}

// ---------------------------------------------------------------------------
// Deadline arithmetic (monotonic millis)
// ---------------------------------------------------------------------------

void test_deadline_not_expired_before_timeout(void) {
    TEST_ASSERT_FALSE(deadlineExpired(1000, 5000, 5999));
}

void test_deadline_expired_at_timeout(void) {
    TEST_ASSERT_TRUE(deadlineExpired(1000, 5000, 6000));
}

void test_deadline_expired_after_timeout(void) {
    TEST_ASSERT_TRUE(deadlineExpired(1000, 5000, 7000));
}

void test_deadline_handles_millis_wraparound(void) {
    // millis() wraps at 2^32: start near the top of the range.
    TEST_ASSERT_FALSE(deadlineExpired(0xFFFFFF00u, 5000, 0xFFFFFF01u));
    // (0x1300 - 0xFFFFFF00) mod 2^32 = 0x1400 = 5120 ms >= 5000 -> expired.
    TEST_ASSERT_TRUE(deadlineExpired(0xFFFFFF00u, 5000, 0x00001300u));
}

void test_deadline_zero_timeout_immediately_expired(void) {
    TEST_ASSERT_TRUE(deadlineExpired(1000, 0, 1000));
}

// ---------------------------------------------------------------------------
// UUID formatting (16 / 32 / 128-bit, Bluedroid layout)
// ---------------------------------------------------------------------------

void test_uuid_16bit_short_form_expands(void) {
    // 0x1800 Generic Access — stored as a scalar (host-endian).
    RawUuid u;
    u.len = 2;
    u.u16 = 0x1800;
    TEST_ASSERT_EQUAL_STRING("00001800-0000-1000-8000-00805f9b34fb",
                             uuidToString(u).c_str());
}

void test_uuid_32bit_short_form_expands(void) {
    RawUuid u;
    u.len = 4;
    u.u32 = 0x12345678;
    TEST_ASSERT_EQUAL_STRING("12345678-0000-1000-8000-00805f9b34fb",
                             uuidToString(u).c_str());
}

void test_uuid_128bit_bluedroid_little_endian(void) {
    // README example: LE-HD 350BT exposes 00002a00-0000-1000-8000-00805f9b34fb.
    // Bluedroid stores uuid128[0] as the least significant byte, so for
    // 00002a00-0000-1000-8000-00805f9b34fb the byte order is:
    // [15..0] = 00 00 2a 00 00 00 10 00 80 00 00 80 5f 9b 34 fb
    RawUuid u;
    u.len = 16;
    u.u128[0] = 0xfb;
    u.u128[1] = 0x34;
    u.u128[2] = 0x9b;
    u.u128[3] = 0x5f;
    u.u128[4] = 0x80;
    u.u128[5] = 0x00;
    u.u128[6] = 0x00;
    u.u128[7] = 0x80;
    u.u128[8] = 0x00;
    u.u128[9] = 0x10;
    u.u128[10] = 0x00;
    u.u128[11] = 0x00;
    u.u128[12] = 0x00;
    u.u128[13] = 0x2a;
    u.u128[14] = 0x00;
    u.u128[15] = 0x00;
    TEST_ASSERT_EQUAL_STRING("00002a00-0000-1000-8000-00805f9b34fb",
                             uuidToString(u).c_str());
}

void test_uuid_unknown_len_empty(void) {
    RawUuid u;
    TEST_ASSERT_EQUAL_STRING("", uuidToString(u).c_str());
}

// ---------------------------------------------------------------------------
// Characteristic property formatting
// ---------------------------------------------------------------------------

void test_properties_read_notify(void) {
    // READ (0x02) | NOTIFY (0x10)
    TEST_ASSERT_EQUAL_STRING("read,notify", propertiesString(0x12).c_str());
}

void test_properties_all_flags(void) {
    TEST_ASSERT_EQUAL_STRING(
        "read,write-no-response,write,notify,indicate,broadcast,extended-properties",
        propertiesString(0xFF).c_str());
}

void test_properties_none_empty(void) {
    TEST_ASSERT_EQUAL_STRING("", propertiesString(0).c_str());
}

// ---------------------------------------------------------------------------

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_state_idle_not_connected);
    RUN_TEST(test_state_connected_states);
    RUN_TEST(test_deadline_not_expired_before_timeout);
    RUN_TEST(test_deadline_expired_at_timeout);
    RUN_TEST(test_deadline_expired_after_timeout);
    RUN_TEST(test_deadline_handles_millis_wraparound);
    RUN_TEST(test_deadline_zero_timeout_immediately_expired);
    RUN_TEST(test_uuid_16bit_short_form_expands);
    RUN_TEST(test_uuid_32bit_short_form_expands);
    RUN_TEST(test_uuid_128bit_bluedroid_little_endian);
    RUN_TEST(test_uuid_unknown_len_empty);
    RUN_TEST(test_properties_read_notify);
    RUN_TEST(test_properties_all_flags);
    RUN_TEST(test_properties_none_empty);
    return UNITY_END();
}

int main(void) {
    return runUnityTests();
}
