// test_feature_model.cpp — conflict matrix, ownership and structured errors
// for the feature/state model (issue #15).  Pure logic: no hardware.

#include <unity.h>
#include <ArduinoJson.h>
#include "FeatureModel.h"
#include "BLEGattCore.h"

using namespace mcp::features;

// Simulated device state driven by the tests.
static bool scanning;
static bool otaFlashing;
static std::string gattDetail;  // "" = idle
static int scanStops;
static int gattDrops;

static FeatureRegistry reg;

static void buildRegistry(bool bleAvailable) {
    reg = FeatureRegistry();

    Feature ota;
    ota.id = "ota";
    ota.conflictsWith = {"ble.central.scan", "ble.central.gatt", "ble.peripheral"};
    ota.status = [] { Status s; s.active = otaFlashing; if (s.active) s.detail = "flashing"; return s; };
    reg.add(ota);

    Feature scan;
    scan.id = "ble.central.scan";
    scan.resolve = "ble/scan/stop";
    scan.available = bleAvailable;
    scan.unavailableReason = "not built for 4 MB flash";
    scan.status = [] {
        Status s; s.active = scanning;
        if (s.active) { s.detail = "scanning"; s.retryAfterMs = 1200; }
        return s;
    };
    scan.release = [] { scanning = false; ++scanStops; };
    reg.add(scan);

    Feature gatt;
    gatt.id = "ble.central.gatt";
    gatt.resolve = "ble/disconnect";
    gatt.available = bleAvailable;
    gatt.unavailableReason = "not built for 4 MB flash";
    gatt.status = [] { Status s; s.active = !gattDetail.empty(); s.detail = gattDetail; return s; };
    gatt.release = [] { gattDetail.clear(); ++gattDrops; };
    reg.add(gatt);

    Feature periph;
    periph.id = "ble.peripheral";
    periph.available = false;
    periph.unavailableReason = "not implemented";
    periph.conflictsWith = {"ble.central.scan", "ble.central.gatt"};
    reg.add(periph);
}

void setUp(void) {
    scanning = false;
    otaFlashing = false;
    gattDetail.clear();
    scanStops = 0;
    gattDrops = 0;
    buildRegistry(true);
}
void tearDown(void) {}

static const FeatureView* viewOf(const std::vector<FeatureView>& v, const char* id) {
    for (const auto& f : v) if (f.id == id) return &f;
    return nullptr;
}

// --- conflict matrix ------------------------------------------------------

void test_idle_features_can_start(void) {
    TEST_ASSERT_TRUE(reg.checkStart("ble.central.scan").ok());
    TEST_ASSERT_TRUE(reg.checkStart("ble.central.gatt").ok());
    TEST_ASSERT_TRUE(reg.checkStart("ota").ok());
}

void test_active_feature_is_busy_with_itself(void) {
    scanning = true;
    Decision d = reg.checkStart("ble.central.scan");
    TEST_ASSERT_TRUE(d.kind == Decision::Kind::Busy);
    TEST_ASSERT_EQUAL_STRING("ble.central.scan", d.blockedBy.c_str());
    TEST_ASSERT_EQUAL_STRING("scanning", d.state.c_str());
    TEST_ASSERT_EQUAL_STRING("ble/scan/stop", d.resolve.c_str());
    TEST_ASSERT_EQUAL_UINT32(1200, d.retryAfterMs);
}

void test_scan_and_gatt_do_not_conflict(void) {
    // Bluedroid runs a scan alongside a GATT client link today.
    scanning = true;
    TEST_ASSERT_TRUE(reg.checkStart("ble.central.gatt").ok());
    scanning = false;
    gattDetail = "connected";
    TEST_ASSERT_TRUE(reg.checkStart("ble.central.scan").ok());
}

void test_ota_blocks_ble_both_ways(void) {
    otaFlashing = true;
    Decision d = reg.checkStart("ble.central.gatt");
    TEST_ASSERT_TRUE(d.kind == Decision::Kind::Busy);
    TEST_ASSERT_EQUAL_STRING("ota", d.blockedBy.c_str());
    TEST_ASSERT_EQUAL_STRING("flashing", d.state.c_str());

    otaFlashing = false;
    scanning = true;
    d = reg.checkStart("ota");
    TEST_ASSERT_TRUE(d.kind == Decision::Kind::Busy);
    TEST_ASSERT_EQUAL_STRING("ble.central.scan", d.blockedBy.c_str());
}

void test_conflicts_are_symmetric(void) {
    // Declared only on the peripheral side, but true from the central side.
    TEST_ASSERT_TRUE(reg.conflicts("ble.central.scan", "ble.peripheral"));
    TEST_ASSERT_TRUE(reg.conflicts("ble.peripheral", "ble.central.scan"));
    TEST_ASSERT_FALSE(reg.conflicts("ble.central.scan", "ble.central.gatt"));
    TEST_ASSERT_FALSE(reg.conflicts("ota", "ota"));
}

void test_unavailable_beats_busy(void) {
    buildRegistry(false);
    otaFlashing = true;
    Decision d = reg.checkStart("ble.central.scan");
    TEST_ASSERT_TRUE(d.kind == Decision::Kind::Unavailable);
    TEST_ASSERT_EQUAL_STRING("not built for 4 MB flash", d.reason.c_str());
}

void test_unknown_feature_is_unavailable(void) {
    Decision d = reg.checkStart("bus.flux-capacitor");
    TEST_ASSERT_TRUE(d.kind == Decision::Kind::Unavailable);
}

void test_unavailable_feature_never_reports_active(void) {
    buildRegistry(false);
    scanning = true;  // status callback must not be consulted
    auto v = reg.snapshot();
    const FeatureView* scan = viewOf(v, "ble.central.scan");
    TEST_ASSERT_NOT_NULL(scan);
    TEST_ASSERT_FALSE(scan->available);
    TEST_ASSERT_TRUE(scan->state == State::Idle);
}

// --- snapshot ---------------------------------------------------------------

void test_snapshot_reports_blocked_features(void) {
    otaFlashing = true;
    auto v = reg.snapshot();
    TEST_ASSERT_TRUE(viewOf(v, "ota")->state == State::Active);
    const FeatureView* scan = viewOf(v, "ble.central.scan");
    TEST_ASSERT_TRUE(scan->state == State::Blocked);
    TEST_ASSERT_EQUAL_STRING("ota", scan->blockedBy.c_str());
    // Unavailable features are not "blocked", just unavailable.
    TEST_ASSERT_TRUE(viewOf(v, "ble.peripheral")->state == State::Idle);
}

void test_snapshot_conflict_lists_are_symmetric_closure(void) {
    auto v = reg.snapshot();
    const FeatureView* scan = viewOf(v, "ble.central.scan");
    // scan declares nothing itself, but ota and the peripheral name it.
    TEST_ASSERT_EQUAL(2, (int)scan->conflictsWith.size());
}

void test_to_json_shape(void) {
    scanning = true;
    reg.setOwner("ble.central.scan", 2);
    JsonDocument doc;
    reg.toJson(doc.to<JsonObject>());
    JsonArray arr = doc["features"].as<JsonArray>();
    TEST_ASSERT_EQUAL(4, (int)arr.size());
    JsonObject scan = arr[1];
    TEST_ASSERT_EQUAL_STRING("ble.central.scan", scan["id"]);
    TEST_ASSERT_EQUAL_STRING("active", scan["state"]);
    TEST_ASSERT_EQUAL_STRING("scanning", scan["detail"]);
    TEST_ASSERT_EQUAL_STRING("client#2", scan["owner"]);
    JsonObject periph = arr[3];
    TEST_ASSERT_FALSE(periph["available"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("not implemented", periph["reason"]);
}

// --- ownership --------------------------------------------------------------

void test_owner_reported_only_while_active(void) {
    scanning = true;
    reg.setOwner("ble.central.scan", 3);
    TEST_ASSERT_EQUAL(3, reg.owner("ble.central.scan"));
    Decision d = reg.checkStart("ble.central.scan");
    TEST_ASSERT_EQUAL(3, d.owner);
    scanning = false;  // scan ended on its own
    TEST_ASSERT_EQUAL(NO_OWNER, reg.owner("ble.central.scan"));
}

void test_release_client_stops_only_its_active_features(void) {
    scanning = true;
    gattDetail = "connected";
    reg.setOwner("ble.central.scan", 1);
    reg.setOwner("ble.central.gatt", 2);

    auto released = reg.releaseClient(1);
    TEST_ASSERT_EQUAL(1, (int)released.size());
    TEST_ASSERT_EQUAL_STRING("ble.central.scan", released[0].c_str());
    TEST_ASSERT_EQUAL(1, scanStops);
    TEST_ASSERT_EQUAL(0, gattDrops);
    TEST_ASSERT_EQUAL_STRING("connected", gattDetail.c_str());
}

void test_release_client_skips_features_that_already_ended(void) {
    reg.setOwner("ble.central.scan", 1);  // scan finished before disconnect
    auto released = reg.releaseClient(1);
    TEST_ASSERT_EQUAL(0, (int)released.size());
    TEST_ASSERT_EQUAL(0, scanStops);
}

void test_new_owner_replaces_old_one(void) {
    gattDetail = "connected";
    reg.setOwner("ble.central.gatt", 1);
    reg.setOwner("ble.central.gatt", 4);
    TEST_ASSERT_EQUAL(0, (int)reg.releaseClient(1).size());
    TEST_ASSERT_EQUAL(1, (int)reg.releaseClient(4).size());
    TEST_ASSERT_EQUAL(1, gattDrops);
}

// --- structured errors ------------------------------------------------------

void test_busy_error_shape(void) {
    gattDetail = "discovering";
    reg.setOwner("ble.central.gatt", 2);
    std::string out = decisionError(7, reg.checkStart("ble.central.gatt"));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, out));
    TEST_ASSERT_EQUAL(7, doc["id"].as<int>());
    TEST_ASSERT_EQUAL(ERR_FEATURE_BUSY, doc["error"]["code"].as<int>());
    TEST_ASSERT_EQUAL_STRING("Feature busy", doc["error"]["message"]);
    JsonObject data = doc["error"]["data"];
    TEST_ASSERT_EQUAL_STRING("ble.central.gatt", data["feature"]);
    TEST_ASSERT_EQUAL_STRING("ble.central.gatt", data["blockedBy"]);
    TEST_ASSERT_EQUAL_STRING("discovering", data["state"]);
    TEST_ASSERT_EQUAL_STRING("client#2", data["owner"]);
    TEST_ASSERT_EQUAL_STRING("ble/disconnect", data["resolve"]);
    TEST_ASSERT_TRUE(data["retryAfterMs"].is<unsigned>());
}

void test_unavailable_error_shape(void) {
    buildRegistry(false);
    std::string out = decisionError(9, reg.checkStart("ble.central.scan"));
    JsonDocument doc;
    deserializeJson(doc, out);
    TEST_ASSERT_EQUAL(ERR_FEATURE_UNAVAILABLE, doc["error"]["code"].as<int>());
    TEST_ASSERT_EQUAL_STRING("not built for 4 MB flash", doc["error"]["data"]["reason"]);
}

void test_not_ready_error_shape(void) {
    std::string out = notReadyError(3, "ble.central.gatt", "not connected", "ble/connect");
    JsonDocument doc;
    deserializeJson(doc, out);
    TEST_ASSERT_EQUAL(ERR_FEATURE_NOT_READY, doc["error"]["code"].as<int>());
    TEST_ASSERT_EQUAL_STRING("not connected", doc["error"]["data"]["reason"]);
    TEST_ASSERT_EQUAL_STRING("ble/connect", doc["error"]["data"]["resolve"]);
}

void test_owner_omitted_when_none(void) {
    scanning = true;
    std::string out = decisionError(1, reg.checkStart("ble.central.scan"));
    TEST_ASSERT_TRUE(out.find("\"owner\"") == std::string::npos);
}

void test_gatt_state_names(void) {
    using mcp::gattcore::GattState;
    TEST_ASSERT_EQUAL_STRING("idle", mcp::gattcore::gattStateName(GattState::Idle));
    TEST_ASSERT_EQUAL_STRING("notify-wait", mcp::gattcore::gattStateName(GattState::NotifyWait));
    TEST_ASSERT_EQUAL_STRING("closing", mcp::gattcore::gattStateName(GattState::Closing));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_features_can_start);
    RUN_TEST(test_active_feature_is_busy_with_itself);
    RUN_TEST(test_scan_and_gatt_do_not_conflict);
    RUN_TEST(test_ota_blocks_ble_both_ways);
    RUN_TEST(test_conflicts_are_symmetric);
    RUN_TEST(test_unavailable_beats_busy);
    RUN_TEST(test_unknown_feature_is_unavailable);
    RUN_TEST(test_unavailable_feature_never_reports_active);
    RUN_TEST(test_snapshot_reports_blocked_features);
    RUN_TEST(test_snapshot_conflict_lists_are_symmetric_closure);
    RUN_TEST(test_to_json_shape);
    RUN_TEST(test_owner_reported_only_while_active);
    RUN_TEST(test_release_client_stops_only_its_active_features);
    RUN_TEST(test_release_client_skips_features_that_already_ended);
    RUN_TEST(test_new_owner_replaces_old_one);
    RUN_TEST(test_busy_error_shape);
    RUN_TEST(test_unavailable_error_shape);
    RUN_TEST(test_not_ready_error_shape);
    RUN_TEST(test_owner_omitted_when_none);
    RUN_TEST(test_gatt_state_names);
    return UNITY_END();
}
