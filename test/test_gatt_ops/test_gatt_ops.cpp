// test_gatt_ops.cpp — hex parsing, characteristic resolution, property
// matching and the notification queue for ble/write and ble/subscribe
// (issue #14).  Pure logic.

#include <unity.h>
#include "BLEGattOps.h"

using namespace mcp;
using namespace mcp::gattops;

void setUp(void) {}
void tearDown(void) {}

// --- hex ----------------------------------------------------------------------

void test_parse_hex_forms(void) {
    std::vector<uint8_t> b;
    std::string err;
    TEST_ASSERT_TRUE(parseHexBytes("01ff", b, err));
    TEST_ASSERT_EQUAL(2, (int)b.size());
    TEST_ASSERT_EQUAL_HEX8(0x01, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, b[1]);
    TEST_ASSERT_TRUE(parseHexBytes("0x0A1b", b, err));
    TEST_ASSERT_EQUAL_HEX8(0x0A, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x1B, b[1]);
    TEST_ASSERT_TRUE(parseHexBytes("de:ad be-ef", b, err));
    TEST_ASSERT_EQUAL(4, (int)b.size());
    TEST_ASSERT_EQUAL_HEX8(0xEF, b[3]);
}

void test_parse_hex_rejects_bad_input(void) {
    std::vector<uint8_t> b;
    std::string err;
    TEST_ASSERT_FALSE(parseHexBytes("", b, err));
    TEST_ASSERT_FALSE(parseHexBytes("0x", b, err));
    TEST_ASSERT_FALSE(parseHexBytes("abc", b, err));       // odd
    TEST_ASSERT_FALSE(parseHexBytes("zz", b, err));
    TEST_ASSERT_FALSE(parseHexBytes("a:b", b, err));       // separator inside a byte
    std::string big(2 * (MAX_WRITE_BYTES + 1), 'a');
    TEST_ASSERT_FALSE(parseHexBytes(big, b, err));
    std::string max(2 * MAX_WRITE_BYTES, 'a');
    TEST_ASSERT_TRUE(parseHexBytes(max, b, err));
    TEST_ASSERT_EQUAL((int)MAX_WRITE_BYTES, (int)b.size());
}

// --- properties -------------------------------------------------------------------

void test_has_property_whole_entries(void) {
    TEST_ASSERT_TRUE(hasProperty("read,write,notify", "write"));
    TEST_ASSERT_TRUE(hasProperty("read,write,notify", "notify"));
    TEST_ASSERT_FALSE(hasProperty("read,write-no-response", "write"));
    TEST_ASSERT_TRUE(hasProperty("read,write-no-response", "write-no-response"));
    TEST_ASSERT_FALSE(hasProperty("", "read"));
    TEST_ASSERT_TRUE(hasProperty("indicate", "indicate"));
}

void test_cccd_values(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0001, cccdValue(true, false));
    TEST_ASSERT_EQUAL_HEX16(0x0002, cccdValue(true, true));
    TEST_ASSERT_EQUAL_HEX16(0x0000, cccdValue(false, true));
}

// --- resolution ---------------------------------------------------------------------

static std::vector<BLEServiceInfo> sampleDb() {
    BLEServiceInfo battery;
    battery.uuid = "0000180f-0000-1000-8000-00805f9b34fb";
    BLECharInfo level;
    level.handle = 0x0012;
    level.uuid = "00002a19-0000-1000-8000-00805f9b34fb";
    level.properties = "read,notify";
    battery.characteristics.push_back(level);

    BLEServiceInfo uart;
    uart.uuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
    BLECharInfo rx, tx;
    rx.handle = 0x0020;
    rx.uuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
    rx.properties = "write,write-no-response";
    tx.handle = 0x0022;
    tx.uuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
    tx.properties = "notify";
    uart.characteristics = {rx, tx};

    // The same characteristic UUID in two services.
    BLEServiceInfo dup;
    dup.uuid = "0000aaaa-0000-1000-8000-00805f9b34fb";
    BLECharInfo d1, d2;
    d1.handle = 0x0030; d1.uuid = "0000bbbb-0000-1000-8000-00805f9b34fb"; d1.properties = "read";
    d2.handle = 0x0040; d2.uuid = "0000bbbb-0000-1000-8000-00805f9b34fb"; d2.properties = "read";
    dup.characteristics = {d1};
    BLEServiceInfo dup2 = dup;
    dup2.uuid = "0000cccc-0000-1000-8000-00805f9b34fb";
    dup2.characteristics = {d2};
    return {battery, uart, dup, dup2};
}

void test_resolve_by_handle(void) {
    CharRef ref;
    std::vector<uint16_t> c;
    TEST_ASSERT_TRUE(resolveCharacteristic(sampleDb(), 0x0022, "", ref, c) == Resolve::Ok);
    TEST_ASSERT_EQUAL_HEX16(0x0022, ref.handle);
    TEST_ASSERT_EQUAL_STRING("notify", ref.properties.c_str());
    TEST_ASSERT_EQUAL_STRING("6e400001-b5a3-f393-e0a9-e50e24dcca9e", ref.serviceUuid.c_str());
}

void test_resolve_by_uuid_is_case_insensitive(void) {
    CharRef ref;
    std::vector<uint16_t> c;
    TEST_ASSERT_TRUE(resolveCharacteristic(sampleDb(), 0, "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
                                           ref, c) == Resolve::Ok);
    TEST_ASSERT_EQUAL_HEX16(0x0020, ref.handle);
}

void test_resolve_handle_wins_over_uuid(void) {
    CharRef ref;
    std::vector<uint16_t> c;
    TEST_ASSERT_TRUE(resolveCharacteristic(sampleDb(), 0x0012, "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
                                           ref, c) == Resolve::Ok);
    TEST_ASSERT_EQUAL_HEX16(0x0012, ref.handle);
}

void test_resolve_ambiguous_and_missing(void) {
    CharRef ref;
    std::vector<uint16_t> c;
    TEST_ASSERT_TRUE(resolveCharacteristic(sampleDb(), 0, "0000bbbb-0000-1000-8000-00805f9b34fb",
                                           ref, c) == Resolve::Ambiguous);
    TEST_ASSERT_EQUAL(2, (int)c.size());
    TEST_ASSERT_TRUE(resolveCharacteristic(sampleDb(), 0x9999, "", ref, c) == Resolve::NotFound);
    TEST_ASSERT_TRUE(resolveCharacteristic(sampleDb(), 0, "", ref, c) == Resolve::NotFound);
    TEST_ASSERT_TRUE(resolveCharacteristic({}, 0x0012, "", ref, c) == Resolve::NotFound);
}

// --- notification queue -------------------------------------------------------------

static NotifyEvent ev(uint16_t h, const char* hex) {
    NotifyEvent e;
    e.handle = h;
    e.valueHex = hex;
    return e;
}

void test_queue_fifo_and_drain(void) {
    NotifyQueue q(4);
    q.push(ev(1, "01"));
    q.push(ev(1, "02"));
    uint32_t dropped = 99;
    auto out = q.drain(dropped);
    TEST_ASSERT_EQUAL(2, (int)out.size());
    TEST_ASSERT_EQUAL_STRING("01", out[0].valueHex.c_str());
    TEST_ASSERT_EQUAL(0, (int)dropped);
    TEST_ASSERT_EQUAL(0, (int)q.size());
}

void test_queue_bounded_drops_oldest_and_counts(void) {
    NotifyQueue q(2);
    q.push(ev(1, "01"));
    q.push(ev(1, "02"));
    q.push(ev(1, "03"));
    q.push(ev(1, "04"));
    uint32_t dropped = 0;
    auto out = q.drain(dropped);
    TEST_ASSERT_EQUAL(2, (int)out.size());
    TEST_ASSERT_EQUAL_STRING("03", out[0].valueHex.c_str());
    TEST_ASSERT_EQUAL(2, (int)dropped);
    // The drop count resets after each drain.
    q.push(ev(1, "05"));
    q.drain(dropped);
    TEST_ASSERT_EQUAL(0, (int)dropped);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_hex_forms);
    RUN_TEST(test_parse_hex_rejects_bad_input);
    RUN_TEST(test_has_property_whole_entries);
    RUN_TEST(test_cccd_values);
    RUN_TEST(test_resolve_by_handle);
    RUN_TEST(test_resolve_by_uuid_is_case_insensitive);
    RUN_TEST(test_resolve_handle_wins_over_uuid);
    RUN_TEST(test_resolve_ambiguous_and_missing);
    RUN_TEST(test_queue_fifo_and_drain);
    RUN_TEST(test_queue_bounded_drops_oldest_and_counts);
    return UNITY_END();
}
