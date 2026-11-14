// test_ble_security.cpp — pairing parameter parsing and status names (#24).
#include <unity.h>
#include "BLESecurityCore.h"

using namespace mcp::blesec;

void setUp(void) {}
void tearDown(void) {}

void test_parse_io_cap(void) {
    IoCap c = IoCap::DisplayOnly;
    TEST_ASSERT_TRUE(parseIoCap("none", c));             TEST_ASSERT_EQUAL(3, (int)c);
    TEST_ASSERT_TRUE(parseIoCap("keyboard", c));         TEST_ASSERT_EQUAL(2, (int)c);
    TEST_ASSERT_TRUE(parseIoCap("display", c));          TEST_ASSERT_EQUAL(0, (int)c);
    TEST_ASSERT_TRUE(parseIoCap("display-yesno", c));    TEST_ASSERT_EQUAL(1, (int)c);
    TEST_ASSERT_TRUE(parseIoCap("keyboard-display", c)); TEST_ASSERT_EQUAL(4, (int)c);
    TEST_ASSERT_FALSE(parseIoCap("Keyboard", c));
    TEST_ASSERT_FALSE(parseIoCap("", c));
    for (int i = 0; i <= 4; ++i) {
        IoCap back;
        TEST_ASSERT_TRUE(parseIoCap(ioCapName(static_cast<IoCap>(i)), back));
        TEST_ASSERT_EQUAL(i, (int)back);
    }
}

void test_default_io_cap(void) {
    TEST_ASSERT_EQUAL((int)IoCap::None, (int)defaultIoCap(false, false));
    TEST_ASSERT_EQUAL((int)IoCap::KeyboardOnly, (int)defaultIoCap(true, false));
    TEST_ASSERT_EQUAL((int)IoCap::DisplayYesNo, (int)defaultIoCap(false, true));
    TEST_ASSERT_EQUAL((int)IoCap::KeyboardOnly, (int)defaultIoCap(true, true));
}

void test_names(void) {
    TEST_ASSERT_EQUAL_STRING("paired", pairStateName(PairState::Paired));
    TEST_ASSERT_EQUAL_STRING("idle", pairStateName(PairState::Idle));
    // SMP reasons come offset by SMP_FAIL_BASE (seen on hardware: wrong passkey -> 0x51)
    TEST_ASSERT_EQUAL_STRING("confirm-value-failed", pairFailReasonName(0x51).c_str());
    TEST_ASSERT_EQUAL_STRING("passkey-entry-failed", pairFailReasonName(SMP_FAIL_BASE + 1).c_str());
    TEST_ASSERT_EQUAL_STRING("cross-transport-key-not-allowed", pairFailReasonName(SMP_FAIL_BASE + 0x0E).c_str());
    // plain HCI errors
    TEST_ASSERT_EQUAL_STRING("remote-user-terminated", pairFailReasonName(0x13).c_str());
    TEST_ASSERT_EQUAL_STRING("0x7f", pairFailReasonName(0x7F).c_str());
    TEST_ASSERT_EQUAL_STRING("0x4d", pairFailReasonName(SMP_FAIL_BASE).c_str());
    TEST_ASSERT_EQUAL_STRING("insufficient-encryption", attErrorName(0x0F).c_str());
    TEST_ASSERT_EQUAL_STRING("0x81", attErrorName(0x81).c_str());
    TEST_ASSERT_TRUE(attErrorNeedsPairing(0x05));
    TEST_ASSERT_TRUE(attErrorNeedsPairing(0x0F));
    TEST_ASSERT_FALSE(attErrorNeedsPairing(0x02));
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_io_cap);
    RUN_TEST(test_default_io_cap);
    RUN_TEST(test_names);
    return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() { delay(2000); runUnityTests(); }
void loop() {}
#else
int main(void) { return runUnityTests(); }
#endif
