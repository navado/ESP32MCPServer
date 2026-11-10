#include <unity.h>
#include "BLEScannerCore.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace mcp::blecore;
using mcp::BLEPayloadEntry;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// hexEncode / hexToAscii
// ---------------------------------------------------------------------------

void test_hex_encode_basic(void) {
    const uint8_t data[] = {0x00, 0x01, 0xAB, 0xFF};
    TEST_ASSERT_EQUAL_STRING("0001abff", hexEncode(data, 4).c_str());
}

void test_hex_encode_empty(void) {
    TEST_ASSERT_EQUAL_STRING("", hexEncode(nullptr, 0).c_str());
}

void test_hex_encode_single_bytes(void) {
    for (int v = 0; v < 256; ++v) {
        uint8_t b = static_cast<uint8_t>(v);
        std::string h = hexEncode(&b, 1);
        TEST_ASSERT_EQUAL(2, h.length());
    }
}

void test_hex_to_ascii_printable_roundtrip(void) {
    // "ABC" -> 414243 -> "ABC"
    TEST_ASSERT_EQUAL_STRING("ABC", hexToAscii("414243").c_str());
}

void test_hex_to_ascii_non_printables_become_dot(void) {
    // 0x00 0x1F 0x7F -> all non-printable -> "..."
    TEST_ASSERT_EQUAL_STRING("...", hexToAscii("001f7f").c_str());
}

void test_hex_decode_roundtrip(void) {
    const uint8_t data[] = {0x6D, 0xB6, 0x43, 0xCE, 0x97, 0xFE, 0x42, 0x7C, 0xE5, 0x00, 0x00};
    std::string enc = hexEncode(data, sizeof(data));
    std::string dec;
    TEST_ASSERT_TRUE(hexDecode(enc, dec));
    TEST_ASSERT_EQUAL(sizeof(data), dec.length());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, reinterpret_cast<const uint8_t*>(dec.data()), sizeof(data));
}

void test_hex_decode_odd_length_fails(void) {
    std::string out = "keep";
    TEST_ASSERT_FALSE(hexDecode("abc", out));
    TEST_ASSERT_EQUAL_STRING("keep", out.c_str());  // unchanged on failure
}

void test_hex_decode_invalid_char_fails(void) {
    std::string out;
    TEST_ASSERT_FALSE(hexDecode("zz", out));  // 'z' is not hex
}

void test_hex_decode_empty_ok(void) {
    std::string out = "keep";
    TEST_ASSERT_TRUE(hexDecode("", out));
    TEST_ASSERT_EQUAL(0, out.length());
}

// ---------------------------------------------------------------------------
// payloadIsConnectable — flags AD type (0x01)
// ---------------------------------------------------------------------------

void test_connectable_flags_general_discoverable(void) {
    // 02 01 02 : flags AD, General Discoverable (0x02), BR/EDR supported
    const uint8_t payload[] = {0x02, 0x01, 0x02};
    TEST_ASSERT_TRUE(payloadIsConnectable(payload, sizeof(payload)));
}

void test_connectable_flags_br_edr_not_supported(void) {
    // 02 01 04 : flags AD, BR/EDR Not Supported (0x04) -> non-connectable
    const uint8_t payload[] = {0x02, 0x01, 0x04};
    TEST_ASSERT_FALSE(payloadIsConnectable(payload, sizeof(payload)));
}

void test_connectable_flags_both_bits(void) {
    // 02 01 06 : General Discoverable + BR/EDR Not Supported
    const uint8_t payload[] = {0x02, 0x01, 0x06};
    TEST_ASSERT_FALSE(payloadIsConnectable(payload, sizeof(payload)));
}

void test_connectable_flags_not_first_ad(void) {
    // 02 01 06 : flags not first -> still found
    const uint8_t payload[] = {0x02, 0x01, 0x06};
    TEST_ASSERT_FALSE(payloadIsConnectable(payload, sizeof(payload)));
}

void test_connectable_missing_flags_defaults_true(void) {
    // no flags AD — only a name AD
    const uint8_t payload[] = {0x05, 0x09, 'H', 'e', 'l', 'l', 'o'};
    TEST_ASSERT_TRUE(payloadIsConnectable(payload, sizeof(payload)));
}

void test_connectable_empty_payload_defaults_true(void) {
    TEST_ASSERT_TRUE(payloadIsConnectable(nullptr, 0));
}

void test_connectable_truncated_ad_defaults_true(void) {
    // AD claims length 5 but only 3 bytes remain -> malformed, stop parsing
    const uint8_t payload[] = {0x05, 0x01, 0x02};
    TEST_ASSERT_TRUE(payloadIsConnectable(payload, sizeof(payload)));
}

void test_connectable_flags_after_service_ad(void) {
    // 02 01 06 first, then a service UUID AD, then flags again
    const uint8_t payload[] = {0x02, 0x01, 0x06, 0x03, 0x03, 0x8F, 0xAE, 0x02, 0x01, 0x02};
    // First flags AD (BR/EDR not supported) wins
    TEST_ASSERT_FALSE(payloadIsConnectable(payload, sizeof(payload)));
}

void test_connectable_zero_length_ad_terminates(void) {
    const uint8_t payload[] = {0x00, 0x01, 0x02};
    TEST_ASSERT_TRUE(payloadIsConnectable(payload, sizeof(payload)));
}

// ---------------------------------------------------------------------------
// recordPayload — per-device distinct-payload history
// ---------------------------------------------------------------------------

void test_record_payload_appends_distinct_payloads(void) {
    std::vector<std::string> hist;
    uint32_t count = 0;
    std::map<std::string, bool> seen;
    recordPayload(hist, count, seen, "0102");
    recordPayload(hist, count, seen, "0103");
    TEST_ASSERT_EQUAL_UINT32(2, count);
    TEST_ASSERT_EQUAL(2, hist.size());
    TEST_ASSERT_EQUAL_STRING("0102", hist[0].c_str());
    TEST_ASSERT_EQUAL_STRING("0103", hist[1].c_str());
}

void test_record_payload_skips_duplicates(void) {
    std::vector<std::string> hist;
    uint32_t count = 0;
    std::map<std::string, bool> seen;
    recordPayload(hist, count, seen, "0102");
    recordPayload(hist, count, seen, "0102");
    recordPayload(hist, count, seen, "0102");
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL(1, hist.size());
}

void test_record_payload_empty_ignored(void) {
    std::vector<std::string> hist;
    uint32_t count = 0;
    std::map<std::string, bool> seen;
    recordPayload(hist, count, seen, "");
    TEST_ASSERT_EQUAL_UINT32(0, count);
    TEST_ASSERT_EQUAL(0, hist.size());
}

void test_record_payload_bounds_at_deep_cap_and_evicts_oldest(void) {
    std::vector<std::string> hist;
    uint32_t count = 0;
    std::map<std::string, bool> seen;
    // Exceed the deep cap (MAX_PAYLOADS=64) by a comfortable margin.
    for (int i = 0; i < 100; ++i) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "%02x%02x", i, i + 1);
        recordPayload(hist, count, seen, hex);
    }
    TEST_ASSERT_EQUAL_UINT32(MAX_PAYLOADS, count);
    TEST_ASSERT_EQUAL(MAX_PAYLOADS, hist.size());
    // Oldest retained is "2425" (i=36); "0001" was evicted.
    TEST_ASSERT_EQUAL_STRING("2425", hist[0].c_str());
    TEST_ASSERT_EQUAL_STRING("6364", hist[MAX_PAYLOADS - 1].c_str());
}

void test_record_payload_evicted_payload_can_reappear(void) {
    std::vector<std::string> hist;
    uint32_t count = 0;
    std::map<std::string, bool> seen;
    for (int i = 0; i <= MAX_PAYLOADS; ++i) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "%02x%02x", i, i + 1);
        recordPayload(hist, count, seen, hex);
    }
    // The very first payload was evicted from the history, so a re-broadcast
    // of it is a distinct change again and must be recorded.
    recordPayload(hist, count, seen, "0001");
    TEST_ASSERT_EQUAL_UINT32(MAX_PAYLOADS, count);
    TEST_ASSERT_EQUAL_STRING("0001", hist[MAX_PAYLOADS - 1].c_str());
}

// The unfiltered ble/scan/results endpoint serialises only the newest
// MAX_PAYLOADS_VIEW payloads (oldest -> newest among the serialised ones)
// while count reports the true distinct total.  This helper returns that
// slice so the serialisation rule is tested exactly as the firmware applies
// it.
static void viewSlice(const std::vector<std::string>& history,
                      std::vector<std::string>& out) {
    out.clear();
    size_t start = history.size() > MAX_PAYLOADS_VIEW
                       ? history.size() - MAX_PAYLOADS_VIEW
                       : 0;
    for (size_t i = start; i < history.size(); ++i) out.push_back(history[i]);
}

void test_view_slice_returns_newest_four_when_history_deep(void) {
    std::vector<std::string> hist;
    uint32_t count = 0;
    std::map<std::string, bool> seen;
    for (int i = 0; i < 10; ++i) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "%02x%02x", i, i + 1);
        recordPayload(hist, count, seen, hex);
    }
    std::vector<std::string> view;
    viewSlice(hist, view);
    TEST_ASSERT_EQUAL(MAX_PAYLOADS_VIEW, view.size());
    // count is the true total; the view is the newest slice.
    TEST_ASSERT_EQUAL_UINT32(10, count);
    TEST_ASSERT_EQUAL_STRING("0607", view[0].c_str());
    TEST_ASSERT_EQUAL_STRING("090a", view[MAX_PAYLOADS_VIEW - 1].c_str());
}

void test_view_slice_returns_whole_history_when_shallow(void) {
    std::vector<std::string> hist;
    uint32_t count = 0;
    std::map<std::string, bool> seen;
    recordPayload(hist, count, seen, "0102");
    recordPayload(hist, count, seen, "0103");
    std::vector<std::string> view;
    viewSlice(hist, view);
    TEST_ASSERT_EQUAL(2, view.size());
    TEST_ASSERT_EQUAL_STRING("0102", view[0].c_str());
    TEST_ASSERT_EQUAL_STRING("0103", view[1].c_str());
}

// ---------------------------------------------------------------------------
// MAC-independent payload registry
// ---------------------------------------------------------------------------

void test_registry_accumulates_count_and_timestamps(void) {
    std::vector<BLEPayloadEntry> reg;
    recordRegistryPayload(reg, "0102", 1000);
    recordRegistryPayload(reg, "0102", 1500);
    recordRegistryPayload(reg, "0102", 2000);
    TEST_ASSERT_EQUAL(1, reg.size());
    TEST_ASSERT_EQUAL_STRING("0102", reg[0].payload.c_str());
    TEST_ASSERT_EQUAL_UINT32(3, reg[0].count);
    TEST_ASSERT_EQUAL_UINT64(1000, reg[0].firstSeen);
    TEST_ASSERT_EQUAL_UINT64(2000, reg[0].lastSeen);
}

void test_registry_appends_distinct_in_first_seen_order(void) {
    std::vector<BLEPayloadEntry> reg;
    recordRegistryPayload(reg, "0102", 1000);
    recordRegistryPayload(reg, "0304", 1100);
    recordRegistryPayload(reg, "0102", 1200);
    TEST_ASSERT_EQUAL(2, reg.size());
    TEST_ASSERT_EQUAL_STRING("0102", reg[0].payload.c_str());
    TEST_ASSERT_EQUAL_STRING("0304", reg[1].payload.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, reg[0].count);
}

void test_registry_truncates_and_dedupes_on_truncated_form(void) {
    std::vector<BLEPayloadEntry> reg;
    std::string longA, longB;
    for (int i = 0; i < 100; ++i) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", i & 0xFF);
        longA += hex;
    }
    longB = longA.substr(0, MAX_PAYLOAD_HEX) + "deadbeef";
    recordRegistryPayload(reg, longA, 1000);
    recordRegistryPayload(reg, longB, 2000);
    // Both truncate to the identical stored string -> one entry, count 2.
    TEST_ASSERT_EQUAL(1, reg.size());
    TEST_ASSERT_EQUAL(MAX_PAYLOAD_HEX, reg[0].payload.length());
    TEST_ASSERT_EQUAL_UINT32(2, reg[0].count);
    TEST_ASSERT_EQUAL_UINT64(1000, reg[0].firstSeen);
    TEST_ASSERT_EQUAL_UINT64(2000, reg[0].lastSeen);
}

void test_registry_bounds_at_max_and_ignores_new_distinct(void) {
    std::vector<BLEPayloadEntry> reg;
    for (int i = 0; i < MAX_REGISTRY_PAYLOADS + 10; ++i) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "%02x%02x", i, i + 1);
        recordRegistryPayload(reg, hex, 1000 + i);
    }
    TEST_ASSERT_EQUAL(MAX_REGISTRY_PAYLOADS, reg.size());
    // Re-broadcasts of an existing payload still count while full.
    recordRegistryPayload(reg, "0001", 99999);
    TEST_ASSERT_EQUAL(MAX_REGISTRY_PAYLOADS, reg.size());
    TEST_ASSERT_EQUAL_UINT32(2, reg[0].count);
}

void test_registry_empty_ignored(void) {
    std::vector<BLEPayloadEntry> reg;
    recordRegistryPayload(reg, "", 1000);
    TEST_ASSERT_EQUAL(0, reg.size());
}

void test_record_payload_truncates_long_payloads(void) {
    std::vector<std::string> hist;
    uint32_t count = 0;
    std::map<std::string, bool> seen;
    // 100 bytes -> 200 hex chars, longer than MAX_PAYLOAD_HEX (128).
    std::string longPayload;
    for (int i = 0; i < 100; ++i) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", i & 0xFF);
        longPayload += hex;
    }
    recordPayload(hist, count, seen, longPayload);
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL(MAX_PAYLOAD_HEX, hist[0].length());
    // Truncated content equals the first 128 chars of the original.
    TEST_ASSERT_EQUAL_STRING(longPayload.substr(0, MAX_PAYLOAD_HEX).c_str(), hist[0].c_str());
}

void test_record_payload_truncation_deduplicates_to_common_prefix(void) {
    std::vector<std::string> hist;
    uint32_t count = 0;
    std::map<std::string, bool> seen;
    // Two distinct long payloads that share their first 128 chars.
    std::string a, b;
    for (int i = 0; i < 100; ++i) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", i & 0xFF);
        a += hex;
    }
    b = a.substr(0, MAX_PAYLOAD_HEX) + "deadbeef";
    recordPayload(hist, count, seen, a);
    recordPayload(hist, count, seen, b);
    // Both truncate to the identical stored string -> recorded once.
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL(1, hist.size());
}

// ---------------------------------------------------------------------------

// ---- follow-ups (#10) ------------------------------------------------------

void test_addr_type_names_roundtrip(void) {
    for (uint8_t t = ADDR_TYPE_PUBLIC; t <= ADDR_TYPE_RPA_RANDOM; ++t) {
        uint8_t out = 0xFF;
        TEST_ASSERT_TRUE(parseAddrType(addrTypeName(t), out));
        TEST_ASSERT_EQUAL_UINT8(t, out);
    }
    uint8_t out = 0;
    TEST_ASSERT_TRUE(parseAddrType("RANDOM", out));
    TEST_ASSERT_EQUAL_UINT8(ADDR_TYPE_RANDOM, out);
    TEST_ASSERT_FALSE(parseAddrType("static", out));
    TEST_ASSERT_FALSE(parseAddrType("", out));
}

void test_lookup_addr_type_case_insensitive(void) {
    std::vector<mcp::BLEAdvReport> reports(2);
    reports[0].mac = "aa:bb:cc:dd:ee:01"; reports[0].addrType = ADDR_TYPE_PUBLIC;
    reports[1].mac = "d7:2f:77:38:f1:eb"; reports[1].addrType = ADDR_TYPE_RANDOM;
    uint8_t t = 0xFF;
    TEST_ASSERT_TRUE(lookupAddrType(reports, "D7:2F:77:38:F1:EB", t));
    TEST_ASSERT_EQUAL_UINT8(ADDR_TYPE_RANDOM, t);
    TEST_ASSERT_FALSE(lookupAddrType(reports, "11:22:33:44:55:66", t));
}

void test_merge_fills_name_from_scan_response(void) {
    mcp::BLEAdvReport r;
    r.mac = "aa:bb:cc:dd:ee:ff";
    mergeReportFields(r, "", "", "");                     // first adv: nothing
    TEST_ASSERT_TRUE(r.name.empty());
    mergeReportFields(r, "MX Master 4", "180f", "4c00");  // scan response
    TEST_ASSERT_EQUAL_STRING("MX Master 4", r.name.c_str());
    TEST_ASSERT_EQUAL_STRING("180f", r.servicesHex.c_str());
    TEST_ASSERT_EQUAL_STRING("4c00", r.manufacturer.c_str());
}

void test_merge_does_not_overwrite_known_fields(void) {
    mcp::BLEAdvReport r;
    r.name = "first"; r.servicesHex = "aaaa"; r.manufacturer = "0001";
    mergeReportFields(r, "second", "bbbb", "0002");
    TEST_ASSERT_EQUAL_STRING("first", r.name.c_str());
    TEST_ASSERT_EQUAL_STRING("aaaa", r.servicesHex.c_str());
    TEST_ASSERT_EQUAL_STRING("0001", r.manufacturer.c_str());
}

void test_registry_counts_dropped_when_full(void) {
    std::vector<BLEPayloadEntry> reg;
    uint32_t dropped = 0;
    char buf[16];
    for (size_t i = 0; i < MAX_REGISTRY_PAYLOADS + 5; ++i) {
        snprintf(buf, sizeof(buf), "%04zx", i);
        recordRegistryPayload(reg, buf, 1000 + i, &dropped);
    }
    TEST_ASSERT_EQUAL(MAX_REGISTRY_PAYLOADS, reg.size());
    TEST_ASSERT_EQUAL_UINT32(5, dropped);
    // A repeat of a retained payload is not a drop.
    recordRegistryPayload(reg, "0000", 5000, &dropped);
    TEST_ASSERT_EQUAL_UINT32(5, dropped);
}

// ---- capture buffer: LRU eviction, byte budget, capture filter (#10) --------

static mcp::BLEAdvReport makeReport(const char* mac, uint64_t lastSeen) {
    mcp::BLEAdvReport r;
    r.mac = mac;
    r.firstSeen = 1;
    r.lastSeen = lastSeen;
    return r;
}

void test_lru_evicts_least_recently_heard_not_first_seen(void) {
    std::vector<mcp::BLEAdvReport> reports;
    reports.push_back(makeReport("aa:00:00:00:00:01", 900));  // seen first, still active
    reports.push_back(makeReport("aa:00:00:00:00:02", 100));  // gone quiet
    reports.push_back(makeReport("aa:00:00:00:00:03", 500));
    TEST_ASSERT_EQUAL(1, lruIndex(reports));
}

void test_lru_respects_exclude(void) {
    std::vector<mcp::BLEAdvReport> reports;
    reports.push_back(makeReport("aa:00:00:00:00:01", 100));
    reports.push_back(makeReport("aa:00:00:00:00:02", 200));
    TEST_ASSERT_EQUAL(1, lruIndex(reports, "aa:00:00:00:00:01"));
    std::vector<mcp::BLEAdvReport> empty;
    TEST_ASSERT_EQUAL(0, lruIndex(empty));
}

void test_capture_bytes_grows_with_history(void) {
    mcp::BLEAdvReport r = makeReport("aa:00:00:00:00:01", 1);
    size_t base = reportBytes(r);
    r.payloadHistory.push_back(std::string(MAX_PAYLOAD_HEX, 'a'));
    TEST_ASSERT_TRUE(reportBytes(r) >= base + MAX_PAYLOAD_HEX);
    std::vector<mcp::BLEAdvReport> reports{r, r};
    TEST_ASSERT_EQUAL(2 * reportBytes(r), captureBytes(reports));
}

void test_budget_bounds_worst_case_capture(void) {
    // A device holding a full deep history must fit the budget on its own,
    // otherwise enforcement could never get under it.
    mcp::BLEAdvReport r = makeReport("aa:00:00:00:00:01", 1);
    for (size_t i = 0; i < MAX_PAYLOADS; ++i) r.payloadHistory.push_back(std::string(MAX_PAYLOAD_HEX, 'a'));
    TEST_ASSERT_TRUE(reportBytes(r) < MAX_CAPTURE_BYTES);
    // ...but MAX_DEVICES such devices would not, which is why the budget exists.
    TEST_ASSERT_TRUE(reportBytes(r) * MAX_DEVICES > MAX_CAPTURE_BYTES);
}

void test_capture_filter_default_matches_everything(void) {
    CaptureFilter f;
    TEST_ASSERT_FALSE(f.active());
    TEST_ASSERT_TRUE(captureMatches(f, "AA:BB:CC:DD:EE:FF", -100, "", ""));
}

void test_capture_filter_min_rssi(void) {
    CaptureFilter f; f.minRssi = -60;
    TEST_ASSERT_TRUE(f.active());
    TEST_ASSERT_TRUE(captureMatches(f, "aa:bb:cc:dd:ee:ff", -60, "", ""));
    TEST_ASSERT_FALSE(captureMatches(f, "aa:bb:cc:dd:ee:ff", -61, "", ""));
}

void test_capture_filter_manufacturer_prefix_little_endian(void) {
    CaptureFilter f; f.manufacturerPrefix = "adde";   // company id 0xDEAD
    TEST_ASSERT_TRUE(captureMatches(f, "aa:bb:cc:dd:ee:ff", -50, "", "ADDEBEAF"));
    TEST_ASSERT_FALSE(captureMatches(f, "aa:bb:cc:dd:ee:ff", -50, "", "4c001006"));
    TEST_ASSERT_FALSE(captureMatches(f, "aa:bb:cc:dd:ee:ff", -50, "", ""));
}

void test_capture_filter_service_and_mac_prefix(void) {
    CaptureFilter f;
    f.serviceUuid = "6e400001";
    f.macPrefix = "f0:68";
    TEST_ASSERT_TRUE(captureMatches(f, "F0:68:E3:B3:40:B7", -50,
                                    "6E400001-b5a3-f393-e0a9-e50e24dcca9e", ""));
    TEST_ASSERT_FALSE(captureMatches(f, "d7:2f:77:38:f1:eb", -50,
                                     "6e400001-b5a3-f393-e0a9-e50e24dcca9e", ""));
    TEST_ASSERT_FALSE(captureMatches(f, "f0:68:e3:b3:40:b7", -50, "180f", ""));
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hex_encode_basic);
    RUN_TEST(test_hex_encode_empty);
    RUN_TEST(test_hex_encode_single_bytes);
    RUN_TEST(test_hex_to_ascii_printable_roundtrip);
    RUN_TEST(test_hex_to_ascii_non_printables_become_dot);
    RUN_TEST(test_hex_decode_roundtrip);
    RUN_TEST(test_hex_decode_odd_length_fails);
    RUN_TEST(test_hex_decode_invalid_char_fails);
    RUN_TEST(test_hex_decode_empty_ok);
    RUN_TEST(test_connectable_flags_general_discoverable);
    RUN_TEST(test_connectable_flags_br_edr_not_supported);
    RUN_TEST(test_connectable_flags_both_bits);
    RUN_TEST(test_connectable_flags_not_first_ad);
    RUN_TEST(test_connectable_missing_flags_defaults_true);
    RUN_TEST(test_connectable_empty_payload_defaults_true);
    RUN_TEST(test_connectable_truncated_ad_defaults_true);
    RUN_TEST(test_connectable_flags_after_service_ad);
    RUN_TEST(test_connectable_zero_length_ad_terminates);
    RUN_TEST(test_record_payload_appends_distinct_payloads);
    RUN_TEST(test_record_payload_skips_duplicates);
    RUN_TEST(test_record_payload_empty_ignored);
    RUN_TEST(test_record_payload_bounds_at_deep_cap_and_evicts_oldest);
    RUN_TEST(test_record_payload_evicted_payload_can_reappear);
    RUN_TEST(test_record_payload_truncates_long_payloads);
    RUN_TEST(test_record_payload_truncation_deduplicates_to_common_prefix);
    RUN_TEST(test_view_slice_returns_newest_four_when_history_deep);
    RUN_TEST(test_view_slice_returns_whole_history_when_shallow);
    RUN_TEST(test_registry_accumulates_count_and_timestamps);
    RUN_TEST(test_registry_appends_distinct_in_first_seen_order);
    RUN_TEST(test_registry_truncates_and_dedupes_on_truncated_form);
    RUN_TEST(test_registry_bounds_at_max_and_ignores_new_distinct);
    RUN_TEST(test_registry_empty_ignored);
    RUN_TEST(test_addr_type_names_roundtrip);
    RUN_TEST(test_lookup_addr_type_case_insensitive);
    RUN_TEST(test_merge_fills_name_from_scan_response);
    RUN_TEST(test_merge_does_not_overwrite_known_fields);
    RUN_TEST(test_registry_counts_dropped_when_full);
    RUN_TEST(test_lru_evicts_least_recently_heard_not_first_seen);
    RUN_TEST(test_lru_respects_exclude);
    RUN_TEST(test_capture_bytes_grows_with_history);
    RUN_TEST(test_budget_bounds_worst_case_capture);
    RUN_TEST(test_capture_filter_default_matches_everything);
    RUN_TEST(test_capture_filter_min_rssi);
    RUN_TEST(test_capture_filter_manufacturer_prefix_little_endian);
    RUN_TEST(test_capture_filter_service_and_mac_prefix);
    return UNITY_END();
}

int main(void) {
    return runUnityTests();
}
