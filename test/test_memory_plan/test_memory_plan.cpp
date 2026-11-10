#include <unity.h>
#include "MemoryPlan.h"

using namespace mcp::mem;

void setUp(void) {}
void tearDown(void) {}

void test_psram_board_puts_bus_history_in_psram(void) {
    MemoryPlan p = planMemory(true, 8u * 1024u * 1024u, true, 160u * 1024u);
    TEST_ASSERT_TRUE(p.mode == MemoryMode::Psram);
    TEST_ASSERT_TRUE(p.busHistoryInPsram);
    TEST_ASSERT_EQUAL_UINT32(PSRAM_BUS_HISTORY_MAX, p.busHistoryCap);  // min(1 MB, 2 MB)
    TEST_ASSERT_EQUAL(BLE_DEVICES_PSRAM, p.bleMaxDevices);
    TEST_ASSERT_EQUAL(BLE_BYTES_PSRAM, p.bleCaptureBytes);
}

void test_psram_budget_is_quarter_of_free_when_small(void) {
    MemoryPlan p = planMemory(true, 2u * 1024u * 1024u, true, 0);
    TEST_ASSERT_EQUAL_UINT32(512u * 1024u, p.busHistoryCap);
}

void test_psram_without_ble_has_no_ble_limits(void) {
    MemoryPlan p = planMemory(true, 8u * 1024u * 1024u, false, 128u * 1024u);
    TEST_ASSERT_TRUE(p.mode == MemoryMode::Psram);
    TEST_ASSERT_EQUAL(0, p.bleMaxDevices);
}

void test_too_little_psram_falls_back_to_internal_plan(void) {
    MemoryPlan p = planMemory(true, 256u * 1024u, true, 160u * 1024u);
    TEST_ASSERT_TRUE(p.mode == MemoryMode::SharedInternal);
    TEST_ASSERT_FALSE(p.busHistoryInPsram);
}

void test_no_psram_with_ble_shares_internal_heap(void) {
    MemoryPlan p = planMemory(false, 0, true, 160u * 1024u);
    TEST_ASSERT_TRUE(p.mode == MemoryMode::SharedInternal);
    TEST_ASSERT_FALSE(p.busHistoryInPsram);
    TEST_ASSERT_EQUAL_UINT32(SHARED_BUS_HISTORY_CAP, p.busHistoryCap);  // 160 KB lowered to 96 KB
    TEST_ASSERT_EQUAL(BLE_DEVICES_SHARED, p.bleMaxDevices);
    TEST_ASSERT_EQUAL(BLE_BYTES_SHARED, p.bleCaptureBytes);
}

void test_shared_plan_keeps_a_lower_board_cap(void) {
    MemoryPlan p = planMemory(false, 0, true, 64u * 1024u);
    TEST_ASSERT_EQUAL_UINT32(64u * 1024u, p.busHistoryCap);
}

void test_shared_plan_caps_uncapped_board(void) {
    MemoryPlan p = planMemory(false, 0, true, 0);
    TEST_ASSERT_EQUAL_UINT32(SHARED_BUS_HISTORY_CAP, p.busHistoryCap);
}

void test_no_psram_no_ble_keeps_board_cap(void) {
    MemoryPlan p = planMemory(false, 0, false, 128u * 1024u);
    TEST_ASSERT_TRUE(p.mode == MemoryMode::Internal);
    TEST_ASSERT_EQUAL_UINT32(128u * 1024u, p.busHistoryCap);
    TEST_ASSERT_EQUAL(0, p.bleMaxDevices);
}

void test_mode_names(void) {
    TEST_ASSERT_EQUAL_STRING("psram", modeName(MemoryMode::Psram));
    TEST_ASSERT_EQUAL_STRING("shared-internal", modeName(MemoryMode::SharedInternal));
    TEST_ASSERT_EQUAL_STRING("internal", modeName(MemoryMode::Internal));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_psram_board_puts_bus_history_in_psram);
    RUN_TEST(test_psram_budget_is_quarter_of_free_when_small);
    RUN_TEST(test_psram_without_ble_has_no_ble_limits);
    RUN_TEST(test_too_little_psram_falls_back_to_internal_plan);
    RUN_TEST(test_no_psram_with_ble_shares_internal_heap);
    RUN_TEST(test_shared_plan_keeps_a_lower_board_cap);
    RUN_TEST(test_shared_plan_caps_uncapped_board);
    RUN_TEST(test_no_psram_no_ble_keeps_board_cap);
    RUN_TEST(test_mode_names);
    return UNITY_END();
}
