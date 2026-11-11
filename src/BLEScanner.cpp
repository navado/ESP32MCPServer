#ifndef NATIVE_TEST

#include "BLEScanner.h"
#include "BLEScannerCore.h"
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <ArduinoJson.h>
#include <esp_gap_bt_api.h>
#include <esp_gatt_defs.h>
#include <esp_gattc_api.h>

namespace mcp {

using blecore::MAX_DEVICES;
using blecore::hexEncode;
using blecore::hexToAscii;
using blecore::payloadIsConnectable;
using blecore::recordPayload;
using gattcore::CLOSE_TIMEOUT_MS;
using gattcore::DISCOVERY_TIMEOUT_MS;
using gattcore::NOTIFY_CAPTURE_TIMEOUT_MS;
using gattcore::READ_OP_TIMEOUT_MS;
using gattcore::deadlineExpired;
using gattcore::GattState;

// Our GATT client application id (arbitrary, must not clash with the BLE
// library's default app id).
static constexpr uint16_t GATTC_APP_ID = 0x5C4E;  // "SCN" little-endian tag

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

BLEScanner::BLEScanner() {}

BLEScanner::~BLEScanner() {
    if (gattTaskHandle_ != nullptr) {
        vTaskDelete(gattTaskHandle_);
        gattTaskHandle_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

void BLEScanner::lock() const {
    // The mutex is created lazily: globals are constructed before the
    // FreeRTOS kernel is up, and the scanner may be used from a static
    // initializer in main.cpp.
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateRecursiveMutex();
    }
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
}

void BLEScanner::unlock() const {
    xSemaphoreGiveRecursive(mutex_);
}

// ---------------------------------------------------------------------------
// Advertising scanner
// ---------------------------------------------------------------------------

class ScannerCallback : public BLEAdvertisedDeviceCallbacks {
public:
    explicit ScannerCallback(BLEScanner* owner) : owner_(owner) {}
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        owner_->onAdv(&advertisedDevice);
    }
private:
    BLEScanner* owner_;
};

// The BLE stack invokes a plain C-style callback on scan completion; route it
// to the singleton instance so the MCP task is never blocked by the scan.
//
// IMPORTANT: this callback runs on the Bluetooth controller task (BTC_TASK).
// Do NOT call esp_ble_gap_stop_scanning() (e.g. via stopScan()) from here —
// it is a re-entrant controller call that deadlocks the task and trips the
// watchdog.  The scan has already finished; clearing the flag is sufficient.
static BLEScanner* g_instance = nullptr;
void BLEScanner::onScanCompleteStatic(BLEScanResults) {
    if (g_instance != nullptr) {
        g_instance->lock();
        g_instance->scanning_ = false;
        g_instance->unlock();
    }
}

// GATT client callback trampoline: the Arduino BLE library registered the
// single Bluedroid gattc handler during BLEDevice::init(); we chain ours via
// its official extension point (setCustomGattcHandler) and forward every event
// it does not know about, so library internals and this scanner can coexist.
// (esp_ble_gattc_register_callback must NOT be called again — the Bluedroid
// registration is one-shot; a second call silently fails.)
static void gattcTrampoline(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf,
                            esp_ble_gattc_cb_param_t* param) {
    if (g_instance != nullptr) {
        g_instance->onGattcEvent(event, gattcIf, param);
    }
}

void BLEScanner::begin() {
    BLEDevice::init("");
    g_instance = this;
    BLEDevice::setCustomGattcHandler(gattcTrampoline);
}

bool BLEScanner::startScan(uint32_t durationMs) {
    lock();
    if (scanning_) { unlock(); return false; }
    reports_.clear();
    seenPayloads_.clear();
    payloadRegistry_.clear();
    scanDurationMs_ = durationMs;
    scanStartedAt_ = millis();
    scanning_ = true;
    unlock();

    BLEScan* scan = BLEDevice::getScan();
    // wantDuplicates=true: deliver every advertisement to the callback so the
    // per-device payload history can capture payload changes.  The library
    // frees each BLEAdvertisedDevice after onResult() returns, so the heap
    // stays bounded even on a busy radio environment.
    scan->setAdvertisedDeviceCallbacks(new ScannerCallback(this), true);
    // Active scan requests scan responses (more data, e.g. names).
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    // Non-blocking start: the completion callback fires when the duration
    // elapses; the MCP task must never block on the scan.  0 = continuous
    // scan until stop() (stopScan/ble/scan/stop); round milliseconds UP to
    // whole seconds for one-shot scans.
    uint32_t seconds = durationMs == 0 ? 0 : (durationMs + 999) / 1000;
    scan->start(seconds, onScanCompleteStatic, false);
    return true;
}

void BLEScanner::stopScan() {
    lock();
    if (!scanning_) { unlock(); return; }
    scanning_ = false;
    unlock();
    BLEDevice::getScan()->stop();
}

bool BLEScanner::isScanning() const {
    lock();
    bool s = scanning_;
    unlock();
    return s;
}

std::vector<BLEAdvReport> BLEScanner::getResults() {
    lock();
    std::vector<BLEAdvReport> out = reports_;
    unlock();
    return out;
}

std::vector<BLEPayloadEntry> BLEScanner::getPayloadRegistry() {
    lock();
    std::vector<BLEPayloadEntry> out = payloadRegistry_;
    unlock();
    return out;
}

uint64_t BLEScanner::getScanStartedAt() const {
    lock();
    uint64_t t = scanStartedAt_;
    unlock();
    return t;
}

uint32_t BLEScanner::getScanDurationMs() const {
    lock();
    uint32_t d = scanDurationMs_;
    unlock();
    return d;
}

size_t BLEScanner::reportCount() const {
    lock();
    size_t n = reports_.size();
    unlock();
    return n;
}

void BLEScanner::clearResults() {
    lock();
    reports_.clear();
    seenPayloads_.clear();
    unlock();
}

void BLEScanner::onAdv(BLEAdvertisedDevice* adv) {
    if (adv == nullptr) return;
    lock();

    std::string mac = adv->getAddress().toString();
    std::string payloadHex;
    uint8_t* raw = adv->getPayload();
    size_t rawLen = adv->getPayloadLength();
    if (raw != nullptr && rawLen > 0) {
        payloadHex = hexEncode(raw, rawLen);
    }

    // Merge duplicate reports for the same MAC.
    for (auto& r : reports_) {
        if (r.mac == mac) {
            r.rssi = adv->getRSSI();
            recordPayload(r.payloadHistory, r.count, seenPayloads_[r.mac], payloadHex);
            // MAC-independent registry: also record across all broadcasters.
            if (!payloadHex.empty()) {
                blecore::recordRegistryPayload(payloadRegistry_, payloadHex, millis());
            }
            unlock();
            return;
        }
    }

    // New device — enforce the device cap (FIFO eviction).
    while (reports_.size() >= MAX_DEVICES) {
        seenPayloads_.erase(reports_.front().mac);
        reports_.erase(reports_.begin());
    }

    BLEAdvReport rep;
    rep.mac = mac;
    rep.rssi = adv->getRSSI();
    rep.isConnectable = payloadIsConnectable(raw, rawLen);
    rep.firstSeen = millis();
    rep.count = 0;

    if (adv->haveName()) rep.name = adv->getName().c_str();

    // Advertised service UUIDs (128-bit / 32-bit / 16-bit, as reported).
    for (int i = 0; i < adv->getServiceUUIDCount(); ++i) {
        std::string s = adv->getServiceUUID(i).toString();
        if (!rep.servicesHex.empty()) rep.servicesHex += ",";
        rep.servicesHex += s;
    }

    // Manufacturer specific data (company id + data).
    if (adv->haveManufacturerData()) {
        std::string md = adv->getManufacturerData();
        rep.manufacturer = hexEncode(reinterpret_cast<const uint8_t*>(md.data()),
                                     md.length());
    }

    reports_.push_back(rep);
    recordPayload(reports_.back().payloadHistory, reports_.back().count,
                  seenPayloads_[rep.mac], payloadHex);
    // MAC-independent registry: record the payload across all broadcasters.
    if (!payloadHex.empty()) {
        blecore::recordRegistryPayload(payloadRegistry_, payloadHex, millis());
    }
    unlock();
}

// ---------------------------------------------------------------------------
// GATT client — raw esp_gattc state machine
//
// The 2.0.17 Arduino BLE library has no timeouts on its discovery/read
// semaphores, so a slow or uncooperative peer blocks the MCP task forever.
// This implementation drives the Bluedroid GATT client API directly, with
// per-operation deadlines:
//   * connect: timeoutMs on the open handshake (async, pollable)
//   * discovery: DISCOVERY_TIMEOUT_MS on search_service
//   * reads: READ_OP_TIMEOUT_MS per characteristic
//   * notify capture: NOTIFY_CAPTURE_TIMEOUT_MS
//   * close: CLOSE_TIMEOUT_MS
//
// Concurrency model: the GATT task is the ONLY task that issues controller
// calls, and it snapshots all intent/state under the mutex BEFORE calling so
// no controller API ever runs while the mutex is held.  Bluedroid callbacks
// (onGattcEvent, chained after the library's handler) run on a stack task and
// only record state under the mutex, scoped to our app id / interface /
// connection id.  The task polls state at 20 ms, enforces every deadline, and
// exits only from the Idle state — the connection is never freed underneath a
// running operation.  Failed lifecycles go through Closing when a connection
// may be open, so a late OPEN_EVT cannot leak the link.
// ---------------------------------------------------------------------------

bool BLEScanner::connect(const std::string& mac, uint32_t timeoutMs) {
    if (timeoutMs == 0) timeoutMs = 1;
    esp_bd_addr_t addr;
    int n = sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]);
    if (n != 6) return false;

    lock();
    if (connected_ || gattState_ != GattState::Idle) { unlock(); return false; }
    connectedMac_ = mac;
    memcpy(peerAddr_, addr, 6);
    hasPeer_ = true;
    connectTimeoutMs_ = timeoutMs;
    openIssued_ = false;
    registerIssued_ = false;
    appRegistered_ = false;
    closeIssued_ = false;
    gattState_ = GattState::Connecting;
    stateStartMillis_ = millis();
    unlock();

    ensureGattTask();
    return true;
}

bool BLEScanner::isConnecting() const {
    lock();
    bool c = gattState_ == GattState::Connecting;
    unlock();
    return c;
}

bool BLEScanner::getConnectResults() const {
    lock();
    bool ok = connected_;
    unlock();
    return ok;
}

void BLEScanner::disconnect() {
    lock();
    bool hadConnection = gattStateConnected(gattState_) ||
                         gattState_ == GattState::Connecting;
    if (!hadConnection) { unlock(); return; }
    gattState_ = GattState::Closing;
    stateStartMillis_ = millis();
    closeIssued_ = false;
    unlock();
    ensureGattTask();
}

bool BLEScanner::isConnected() const {
    lock();
    bool c = connected_;
    unlock();
    return c;
}

std::vector<BLEServiceInfo> BLEScanner::getServicesStart() {
    lock();
    std::vector<BLEServiceInfo> out = services_;
    if (!connected_ || gattState_ != GattState::Connected) { unlock(); return out; }
    gattState_ = GattState::Discovering;
    stateStartMillis_ = millis();
    searchIssued_ = false;
    loadPending_ = false;
    services_.clear();
    pendingReads_.clear();
    notifyHandles_.clear();
    notifyValues_.clear();
    unlock();

    ensureGattTask();
    return out;
}

std::vector<BLEServiceInfo> BLEScanner::getServicesResults(uint32_t notifyCaptureMs) {
    // Wait for the enumeration to finish (bounded: 10 s discovery cap, then
    // `notifyCaptureMs` for notified values).  Blocking here is fine — this is
    // the caller's explicit results request.
    lock();
    uint32_t waited = 0;
    const uint32_t enumLimit = notifyCaptureMs + DISCOVERY_TIMEOUT_MS;
    bool busy = gattStateConnected(gattState_);
    unlock();
    while (busy && waited < enumLimit) {
        delay(50);
        waited += 50;
        lock();
        busy = gattStateConnected(gattState_);
        unlock();
    }

    // Then wait for notified/indicated values to arrive.
    lock();
    bool hasNotifies = !notifyHandles_.empty();
    unlock();
    if (hasNotifies) {
        waited = 0;
        while (waited < notifyCaptureMs) {
            delay(50);
            waited += 50;
            lock();
            bool vals = !notifyValues_.empty();
            bool ended = gattState_ == GattState::Connected ||
                         gattState_ == GattState::Idle;
            unlock();
            if (vals || ended) break;
        }
    }

    lock();
    std::vector<BLEServiceInfo> out = services_;
    // Fold any captured notified values into the response.
    for (auto& [handle, uuid] : notifyHandles_) {
        auto it = notifyValues_.find(handle);
        if (it == notifyValues_.end()) continue;
        for (auto& svc : out) {
            for (auto& chr : svc.characteristics) {
                if (chr.uuid == uuid) {
                    chr.valueHex = it->second;
                    chr.valueText = hexToAscii(chr.valueHex);
                }
            }
        }
    }
    unlock();
    return out;
}

void BLEScanner::getServicesStop() {
    disconnect();
}

bool BLEScanner::isServicesBusy() const {
    lock();
    bool b = gattStateConnected(gattState_);
    unlock();
    return b;
}

// ---------------------------------------------------------------------------
// GATT task
// ---------------------------------------------------------------------------

void BLEScanner::ensureGattTask() {
    lock();
    if (gattTaskHandle_ == nullptr) {
        xTaskCreatePinnedToCore(gattTaskStatic, "BLEGattTask", 8192, this, 1,
                                &gattTaskHandle_, 0);
    }
    unlock();
}

void BLEScanner::gattTaskStatic(void* param) {
    static_cast<BLEScanner*>(param)->gattTaskBody();
}

void BLEScanner::gattTaskBody() {
    for (;;) {
        lock();
        GattState st = gattState_;
        uint32_t start = stateStartMillis_;
        bool doRegister = false, doOpen = false, doSearch = false, doClose = false;
        esp_gatt_if_t gif = 0;
        uint16_t cid = 0;
        esp_bd_addr_t peer;
        memcpy(peer, peerAddr_, 6);
        switch (st) {
            case GattState::Connecting: {
                if (!appRegistered_ && !registerIssued_) {
                    registerIssued_ = true;
                    doRegister = true;
                } else if (appRegistered_ && !openIssued_ && gattcIf_ != 0) {
                    openIssued_ = true;
                    gif = gattcIf_;
                    doOpen = true;
                }
                break;
            }
            case GattState::Discovering: {
                // conn_id may legitimately be 0 on this stack (first GATT
                // connection); the state itself guarantees a connection.
                if (!searchIssued_) {
                    searchIssued_ = true;
                    gif = gattcIf_;
                    cid = connId_;
                    doSearch = true;
                }
                break;
            }
            case GattState::Closing: {
                if (!closeIssued_) {
                    closeIssued_ = true;
                    gif = gattcIf_;
                    cid = connId_;
                    doClose = true;
                }
                break;
            }
            default:
                break;
        }
        unlock();

        // Controller calls happen strictly outside the mutex.
        if (doRegister) {
            esp_ble_gattc_app_register(GATTC_APP_ID);
        } else if (doOpen) {
            esp_ble_gattc_open(gif, peer, BLE_ADDR_TYPE_PUBLIC, true);
        } else if (doSearch) {
            esp_ble_gattc_search_service(gif, cid, nullptr);
        } else if (doClose) {
            gattUnsubscribeAll();
            // conn_id may be 0 (first GATT connection on this stack); the
            // Closing state itself guarantees a connection exists.
            esp_ble_gattc_close(gif, cid);
        }

        // Deadline enforcement.
        lock();
        st = gattState_;
        start = stateStartMillis_;
        switch (st) {
            case GattState::Connecting:
                if (deadlineExpired(start, connectTimeoutMs_, millis())) gattFail();
                break;
            case GattState::Discovering:
                if (deadlineExpired(start, DISCOVERY_TIMEOUT_MS, millis())) gattFail();
                break;
            case GattState::Reading:
                if (deadlineExpired(start, READ_OP_TIMEOUT_MS, millis())) gattFail();
                break;
            case GattState::NotifyWait:
                if (deadlineExpired(start, NOTIFY_CAPTURE_TIMEOUT_MS, millis())) {
                    gattEnumDone();
                }
                break;
            case GattState::Closing:
                if (deadlineExpired(start, CLOSE_TIMEOUT_MS, millis())) gattFail();
                break;
            case GattState::Connected: {
                // A discovery completed (loadPending_); load services now.
                bool load = loadPending_;
                if (load) loadPending_ = false;
                unlock();
                if (load) {
                    gattLoadServices();
                    continue;  // re-lock at the top of the loop
                }
                lock();  // re-acquire for the deadline switch below
                break;
            }
            default:
                break;
        }
        bool idle = (st == GattState::Idle);
        unlock();

        if (idle) break;  // all work done — exit the task
        delay(20);
    }

    lock();
    gattTaskHandle_ = nullptr;
    unlock();
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// GATT event handling (Bluedroid callback context)
//
// All events are scoped to our app id / interface / connection id so events
// belonging to the Arduino library's own clients never touch our state.
// ---------------------------------------------------------------------------

void BLEScanner::onGattcEvent(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf,
                              esp_ble_gattc_cb_param_t* param) {
    if (param == nullptr) return;

    switch (event) {
        case ESP_GATTC_REG_EVT: {
            lock();
            if (param->reg.app_id == GATTC_APP_ID && param->reg.status == ESP_GATT_OK) {
                gattcIf_ = gattcIf;
                appRegistered_ = true;
            }
            unlock();
            break;
        }
        case ESP_GATTC_OPEN_EVT: {
            lock();
            if (gattcIf != gattcIf_) { unlock(); break; }
            if (gattState_ == GattState::Connecting) {
                if (param->open.status != ESP_GATT_OK) {
                    gattFail();
                } else {
                    connId_ = param->open.conn_id;
                    connected_ = true;
                    gattState_ = GattState::Connected;
                    stateStartMillis_ = millis();
                }
                unlock();
            } else if (gattState_ == GattState::Idle) {
                // Late open after a failed/aborted attempt — close it
                // immediately so the link does not leak.
                uint16_t cid = param->open.conn_id;
                unlock();
                esp_ble_gattc_close(gattcIf, cid);
            } else {
                unlock();
            }
            break;
        }
        case ESP_GATTC_SEARCH_CMPL_EVT: {
            lock();
            if (gattcIf == gattcIf_ && param->search_cmpl.conn_id == connId_ &&
                gattState_ == GattState::Discovering) {
                loadPending_ = true;
                // Discovery succeeded: move to Connected so the task loop's
                // Connected case picks up the load request.
                gattState_ = GattState::Connected;
                stateStartMillis_ = millis();
            }
            unlock();
            break;
        }
        case ESP_GATTC_READ_CHAR_EVT: {
            lock();
            if (gattcIf != gattcIf_ || param->read.conn_id != connId_) { unlock(); break; }
            auto it = pendingReads_.find(param->read.handle);
            if (it == pendingReads_.end()) { unlock(); break; }
            std::string uuid = it->second;
            pendingReads_.erase(it);
            if (param->read.status == ESP_GATT_OK && param->read.value_len > 0) {
                std::string val(reinterpret_cast<const char*>(param->read.value),
                                param->read.value_len);
                std::string hex = hexEncode(reinterpret_cast<const uint8_t*>(val.data()),
                                            val.length());
                for (auto& svc : services_) {
                    for (auto& chr : svc.characteristics) {
                        if (chr.uuid == uuid) {
                            chr.valueHex = hex;
                            chr.valueText = hexToAscii(hex);
                        }
                    }
                }
            }
            if (pendingReads_.empty() && gattState_ == GattState::Reading) {
                if (notifyHandles_.empty()) {
                    gattEnumDone();
                    unlock();
                } else {
                    gattState_ = GattState::NotifyWait;
                    stateStartMillis_ = millis();
                    unlock();
                    gattSubscribeAll();
                }
            } else {
                unlock();
            }
            break;
        }
        case ESP_GATTC_NOTIFY_EVT: {
            lock();
            if (gattcIf != gattcIf_ || param->notify.conn_id != connId_) { unlock(); break; }
            std::string val(reinterpret_cast<const char*>(param->notify.value),
                            param->notify.value_len);
            std::string hex = hexEncode(reinterpret_cast<const uint8_t*>(val.data()),
                                        val.length());
            notifyValues_[param->notify.handle] = hex;
            unlock();
            break;
        }
        case ESP_GATTC_DISCONNECT_EVT: {
            lock();
            if (gattcIf == gattcIf_ &&
                (connId_ == 0 || param->disconnect.conn_id == connId_)) {
                gattState_ = GattState::Idle;
                connected_ = false;
                hasPeer_ = false;
                connId_ = 0;
            }
            unlock();
            break;
        }
        case ESP_GATTC_CLOSE_EVT: {
            lock();
            if (gattcIf == gattcIf_ && param->close.conn_id == connId_) {
                gattState_ = GattState::Idle;
                connected_ = false;
                hasPeer_ = false;
                connId_ = 0;
            }
            unlock();
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// GATT helpers
// ---------------------------------------------------------------------------

static gattcore::RawUuid rawUuidOf(const esp_bt_uuid_t& u) {
    gattcore::RawUuid out;
    out.len = u.len;
    if (u.len == ESP_UUID_LEN_16) {
        out.u16 = u.uuid.uuid16;
    } else if (u.len == ESP_UUID_LEN_32) {
        out.u32 = u.uuid.uuid32;
    } else if (u.len == ESP_UUID_LEN_128) {
        memcpy(out.u128, u.uuid.uuid128, ESP_UUID_LEN_128);
    }
    return out;
}

void BLEScanner::gattLoadServices() {
    // Snapshot the discovered database from the Bluedroid cache WITHOUT the
    // mutex held (controller calls must never run under the lock).  The
    // database is owned by the stack and only valid for the duration of this
    // call, so copy the elements we need into local structures.
    lock();
    esp_gatt_if_t gif = gattcIf_;
    uint16_t cid = connId_;
    unlock();

    uint16_t count = 0;
    esp_gatt_status_t rcCount = esp_ble_gattc_get_attr_count(gif, cid, ESP_GATT_DB_ALL,
                                                             1, 0xFFFF, 0, &count);
    if (rcCount != ESP_OK || count == 0) {
        lock();
        services_.clear();
        gattEnumDone();
        unlock();
        return;
    }
    esp_gattc_db_elem_t* db = new esp_gattc_db_elem_t[count];
    uint16_t got = count;
    if (esp_ble_gattc_get_db(gif, cid, 1, 0xFFFF, db, &got) != ESP_OK) {
        delete[] db;
        lock();
        services_.clear();
        gattEnumDone();
        unlock();
        return;
    }

    // Group characteristics under their owning service by handle range.
    struct SvcRange {
        uint16_t start, end;
        gattcore::RawUuid uuid;
    };
    std::vector<SvcRange> svcs;
    std::vector<esp_gattc_db_elem_t> chars;
    for (uint16_t i = 0; i < got; ++i) {
        if (db[i].type == ESP_GATT_DB_PRIMARY_SERVICE ||
            db[i].type == ESP_GATT_DB_SECONDARY_SERVICE) {
            SvcRange r;
            r.start = db[i].start_handle;
            r.end = db[i].end_handle;
            r.uuid = rawUuidOf(db[i].uuid);
            svcs.push_back(r);
        } else if (db[i].type == ESP_GATT_DB_CHARACTERISTIC) {
            chars.push_back(db[i]);
        }
    }
    delete[] db;

    lock();
    services_.clear();
    for (auto& r : svcs) {
        BLEServiceInfo si;
        si.uuid = gattcore::uuidToString(r.uuid);
        for (auto& ch : chars) {
            if (ch.attribute_handle < r.start || ch.attribute_handle > r.end) {
                continue;
            }
            BLECharInfo ci;
            ci.uuid = gattcore::uuidToString(rawUuidOf(ch.uuid));
            ci.properties = gattcore::propertiesString(ch.properties);
            si.characteristics.push_back(ci);
            if (ch.properties & ESP_GATT_CHAR_PROP_BIT_READ) {
                pendingReads_[ch.attribute_handle] = ci.uuid;
            }
            if (ch.properties & (ESP_GATT_CHAR_PROP_BIT_NOTIFY |
                                 ESP_GATT_CHAR_PROP_BIT_INDICATE)) {
                notifyHandles_[ch.attribute_handle] = ci.uuid;
            }
        }
        services_.push_back(si);
    }
    unlock();

    gattStartReads();
}

void BLEScanner::gattStartReads() {
    lock();
    if (pendingReads_.empty()) {
        if (notifyHandles_.empty()) {
            gattEnumDone();
        } else {
            gattState_ = GattState::NotifyWait;
            stateStartMillis_ = millis();
            unlock();
            gattSubscribeAll();
            return;
        }
        unlock();
        return;
    }
    gattState_ = GattState::Reading;
    stateStartMillis_ = millis();
    esp_gatt_if_t gif = gattcIf_;
    uint16_t cid = connId_;
    auto reads = pendingReads_;
    unlock();

    // Fire all reads; each is answered by READ_CHAR_EVT.
    for (auto& [handle, uuid] : reads) {
        esp_ble_gattc_read_char(gif, cid, handle, ESP_GATT_AUTH_REQ_NONE);
    }
}

void BLEScanner::gattEnumDone() {
    // Mutex held by the caller.  The enumeration is complete; the connection
    // stays open so the caller can poll results or stop it explicitly.
    if (gattState_ == GattState::Reading || gattState_ == GattState::NotifyWait) {
        gattState_ = GattState::Connected;
        stateStartMillis_ = millis();
    }
}

void BLEScanner::gattFail() {
    // Mutex held by the caller.  If a connection may be open, route through
    // Closing so the link is torn down (and a late OPEN_EVT cannot leak it);
    // otherwise drop straight to Idle.
    bool connMayBeOpen = connId_ != 0 ||
                         gattState_ == GattState::Connected ||
                         gattState_ == GattState::Discovering ||
                         gattState_ == GattState::Reading ||
                         gattState_ == GattState::NotifyWait;
    if (connMayBeOpen) {
        gattState_ = GattState::Closing;
        stateStartMillis_ = millis();
        closeIssued_ = false;
    } else {
        gattState_ = GattState::Idle;
        connected_ = false;
        hasPeer_ = false;
        connId_ = 0;
        pendingReads_.clear();
        notifyHandles_.clear();
    }
}

void BLEScanner::gattSubscribeAll() {
    // Enable notifications: write 0x0001 to the CCCD of each notifiable
    // characteristic.  Cache lookups run without the lock (they are fast
    // local reads); the write is issued outside the lock too.  The write
    // response (WRITE_DESCR_EVT) is deliberately ignored; notifications start
    // as soon as the peer enables them.
    lock();
    esp_gatt_if_t gif = gattcIf_;
    uint16_t cid = connId_;
    auto handles = notifyHandles_;
    unlock();

    std::vector<uint16_t> cccdHandles;
    esp_bt_uuid_t cccdUuid;
    cccdUuid.len = ESP_UUID_LEN_16;
    cccdUuid.uuid.uuid16 = 0x2902;
    for (auto& [handle, uuid] : handles) {
        esp_gattc_descr_elem_t descr;
        uint16_t n = 1;
        if (esp_ble_gattc_get_descr_by_char_handle(gif, cid, handle,
                                                   cccdUuid, &descr, &n) == ESP_OK && n > 0) {
            cccdHandles.push_back(descr.handle);
        }
    }
    for (uint16_t dh : cccdHandles) {
        uint8_t val[2] = {0x01, 0x00};
        esp_ble_gattc_write_char_descr(gif, cid, dh, sizeof(val), val,
                                       ESP_GATT_WRITE_TYPE_RSP,
                                       ESP_GATT_AUTH_REQ_NONE);
    }
}

void BLEScanner::gattUnsubscribeAll() {
    // Disable notifications by writing 0x0000 to each CCCD.  Same structure
    // as gattSubscribeAll; the connection is usually closed right after.
    lock();
    esp_gatt_if_t gif = gattcIf_;
    uint16_t cid = connId_;
    auto handles = notifyHandles_;
    unlock();

    std::vector<uint16_t> cccdHandles;
    esp_bt_uuid_t cccdUuid;
    cccdUuid.len = ESP_UUID_LEN_16;
    cccdUuid.uuid.uuid16 = 0x2902;
    for (auto& [handle, uuid] : handles) {
        esp_gattc_descr_elem_t descr;
        uint16_t n = 1;
        if (esp_ble_gattc_get_descr_by_char_handle(gif, cid, handle,
                                                   cccdUuid, &descr, &n) == ESP_OK && n > 0) {
            cccdHandles.push_back(descr.handle);
        }
    }
    for (uint16_t dh : cccdHandles) {
        uint8_t val[2] = {0x00, 0x00};
        esp_ble_gattc_write_char_descr(gif, cid, dh, sizeof(val), val,
                                       ESP_GATT_WRITE_TYPE_RSP,
                                       ESP_GATT_AUTH_REQ_NONE);
    }
    lock();
    notifyHandles_.clear();
    unlock();
}

} // namespace mcp

#endif // NATIVE_TEST
