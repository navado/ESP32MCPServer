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
#include <new>

namespace mcp {

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

bool BLEScanner::startScan(uint32_t durationMs, const blecore::CaptureFilter& filter) {
    lock();
    if (scanning_) { unlock(); return false; }
    reports_.clear();
    seenPayloads_.clear();
    payloadRegistry_.clear();
    registryDropped_ = 0;
    devicesEvicted_ = 0;
    filter_ = filter;
    scanDurationMs_ = durationMs;
    scanStartedAt_ = millis();
    scanning_ = true;
    unlock();

    BLEScan* scan = BLEDevice::getScan();
    // wantDuplicates=true: deliver every advertisement to the callback so the
    // per-device payload history can capture payload changes.  The library
    // frees each BLEAdvertisedDevice after onResult() returns, so the heap
    // stays bounded even on a busy radio environment.
    // The library stores the raw pointer and never frees it; allocate the
    // callback once instead of leaking a new one on every scan.
    static ScannerCallback* callback = new ScannerCallback(this);
    scan->setAdvertisedDeviceCallbacks(callback, true);
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

blecore::CaptureFilter BLEScanner::getCaptureFilter() const {
    lock();
    blecore::CaptureFilter f = filter_;
    unlock();
    return f;
}

uint32_t BLEScanner::getDevicesEvicted() const {
    lock();
    uint32_t n = devicesEvicted_;
    unlock();
    return n;
}

void BLEScanner::evictAt(size_t idx) {
    if (idx >= reports_.size()) return;
    seenPayloads_.erase(reports_[idx].mac);
    reports_.erase(reports_.begin() + static_cast<long>(idx));
    ++devicesEvicted_;
}

void BLEScanner::setCaptureLimits(size_t maxDevices, size_t maxBytes) {
    if (maxDevices < 1) maxDevices = 1;
    if (maxDevices > blecore::MAX_DEVICES_LIMIT) maxDevices = blecore::MAX_DEVICES_LIMIT;
    lock();
    maxDevices_ = maxDevices;
    maxCaptureBytes_ = maxBytes;
    unlock();
}

size_t BLEScanner::getMaxDevices() const {
    lock();
    size_t n = maxDevices_;
    unlock();
    return n;
}

void BLEScanner::enforceCaptureBudget(const std::string& keepMac) {
    while (reports_.size() > 1 &&
           blecore::captureBytes(reports_) > maxCaptureBytes_) {
        evictAt(blecore::lruIndex(reports_, keepMac));
    }
}

uint32_t BLEScanner::getRegistryDropped() const {
    lock();
    uint32_t d = registryDropped_;
    unlock();
    return d;
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

    // Fields that may arrive only in the scan response (a separate report for
    // the same MAC): collect them for both the new-device and merge paths.
    std::string name;
    if (adv->haveName()) name = adv->getName().c_str();
    std::string servicesHex;
    for (int i = 0; i < adv->getServiceUUIDCount(); ++i) {
        if (!servicesHex.empty()) servicesHex += ",";
        servicesHex += adv->getServiceUUID(i).toString();
    }
    std::string manufacturer;
    if (adv->haveManufacturerData()) {
        std::string md = adv->getManufacturerData();
        manufacturer = hexEncode(reinterpret_cast<const uint8_t*>(md.data()),
                                 md.length());
    }

    const uint64_t now = millis();
    const int rssi = adv->getRSSI();

    // Merge duplicate reports for the same MAC.
    for (auto& r : reports_) {
        if (r.mac == mac) {
            r.rssi = rssi;
            r.lastSeen = now;
            blecore::mergeReportFields(r, name, servicesHex, manufacturer);
            const size_t before = r.payloadHistory.size();
            recordPayload(r.payloadHistory, r.count, seenPayloads_[r.mac], payloadHex);
            // MAC-independent registry: also record across all broadcasters.
            if (!payloadHex.empty()) {
                blecore::recordRegistryPayload(payloadRegistry_, payloadHex, now,
                                              &registryDropped_);
            }
            // `r` may be invalidated by eviction below, so copy the key first.
            if (r.payloadHistory.size() != before) {
                std::string keep = r.mac;
                enforceCaptureBudget(keep);
            }
            unlock();
            return;
        }
    }

    // New device: apply the capture filter, so ambient traffic cannot crowd
    // the device of interest out of the capture.
    if (!blecore::captureMatches(filter_, mac, rssi, servicesHex, manufacturer)) {
        unlock();
        return;
    }

    // Enforce the device cap: evict the least-recently-heard device.
    while (reports_.size() >= maxDevices_) {
        evictAt(blecore::lruIndex(reports_));
    }

    BLEAdvReport rep;
    rep.mac = mac;
    rep.rssi = rssi;
    rep.isConnectable = payloadIsConnectable(raw, rawLen);
    rep.firstSeen = now;
    rep.lastSeen = now;
    rep.count = 0;
    rep.addrType = static_cast<uint8_t>(adv->getAddressType());
    rep.name = name;
    rep.servicesHex = servicesHex;
    rep.manufacturer = manufacturer;

    reports_.push_back(rep);
    recordPayload(reports_.back().payloadHistory, reports_.back().count,
                  seenPayloads_[rep.mac], payloadHex);
    // MAC-independent registry: record the payload across all broadcasters.
    if (!payloadHex.empty()) {
        blecore::recordRegistryPayload(payloadRegistry_, payloadHex, now,
                                              &registryDropped_);
    }
    enforceCaptureBudget(mac);
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

bool BLEScanner::connect(const std::string& mac, uint32_t timeoutMs, int addrType) {
    if (timeoutMs == 0) timeoutMs = 1;
    esp_bd_addr_t addr;
    int n = sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]);
    if (n != 6) return false;

    lock();
    if (connected_ || gattState_ != GattState::Idle) { unlock(); return false; }
    connectedMac_ = mac;
    memcpy(peerAddr_, addr, 6);
    if (addrType >= 0) {
        peerAddrType_ = static_cast<uint8_t>(addrType);
    } else if (!blecore::lookupAddrType(reports_, mac, peerAddrType_)) {
        peerAddrType_ = blecore::ADDR_TYPE_PUBLIC;  // not seen in a scan
    }
    hasPeer_ = true;
    connectTimeoutMs_ = timeoutMs;
    openIssued_ = false;
    // The GATT app registration survives across connections, and registering
    // the same app id again fails with ESP_GATT_ERROR (REG_EVT not OK), which
    // would leave appRegistered_ false and stall the open until timeout.  Only
    // re-issue the registration if no successful REG_EVT has been seen yet.
    if (!appRegistered_) registerIssued_ = false;
    closeIssued_ = false;
    gattState_ = GattState::Connecting;
    stateStartMillis_ = millis();
    unlock();

    ensureGattTask();
    return true;
}

uint8_t BLEScanner::getPeerAddrType() const {
    lock();
    uint8_t t = peerAddrType_;
    unlock();
    return t;
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
    // A fresh enumeration subscribes to everything notifiable; on-demand
    // subscriptions (and their handles) are re-established by the client.
    liveSubs_.clear();
    unlock();

    ensureGattTask();
    return out;
}

std::vector<BLEServiceInfo> BLEScanner::getServicesResults(uint32_t notifyCaptureMs) {
    // Wait for the enumeration to finish (bounded: 10 s discovery cap, then
    // `notifyCaptureMs` for notified values).  Blocking here is fine — this is
    // the caller's explicit results request.
    notifyCaptureMs = gattcore::clampNotifyCaptureMs(notifyCaptureMs);
    lock();
    uint32_t waited = 0;
    const uint32_t enumLimit = notifyCaptureMs + DISCOVERY_TIMEOUT_MS;
    // Wait only while the enumeration is actually running.  (gattStateConnected
    // is also true in Connected, i.e. AFTER enumeration finished, which made
    // every call wait the full enumLimit while blocking the MCP task.)
    bool busy = gattcore::gattEnumInProgress(gattState_, loadPending_);
    unlock();
    while (busy && waited < enumLimit) {
        delay(50);
        waited += 50;
        lock();
        busy = gattcore::gattEnumInProgress(gattState_, loadPending_);
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
        esp_ble_addr_type_t peerType = static_cast<esp_ble_addr_type_t>(peerAddrType_);
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
            esp_ble_gattc_open(gif, peer, peerType, true);
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
        if (idle) {
            // Clear the handle in the SAME critical section that observed
            // Idle.  Releasing the lock first would let connect()/disconnect()
            // move the state out of Idle, see a non-null handle, skip
            // ensureGattTask(), and leave the state machine with no task to
            // drive it (permanently wedged until reboot).
            gattTaskHandle_ = nullptr;
        }
        unlock();

        if (idle) break;  // all work done — exit the task
        delay(20);
    }

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
                                gattcore::clampGattValueLen(param->read.value_len));
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
            // Only keep values for characteristics we subscribed to, bounded in
            // size: a peer must not be able to grow this map without limit by
            // notifying on arbitrary handles with large payloads.
            if (notifyHandles_.find(param->notify.handle) == notifyHandles_.end()) {
                unlock();
                break;
            }
            std::string val(reinterpret_cast<const char*>(param->notify.value),
                            gattcore::clampGattValueLen(param->notify.value_len));
            std::string hex = hexEncode(reinterpret_cast<const uint8_t*>(val.data()),
                                        val.length());
            notifyValues_[param->notify.handle] = hex;
            auto live = liveSubs_.find(param->notify.handle);
            if (live != liveSubs_.end()) {
                gattops::NotifyEvent ev;
                ev.handle      = param->notify.handle;
                ev.uuid        = live->second;
                ev.valueHex    = hex;
                ev.timestampMs = millis();
                notifyQueue_.push(std::move(ev));
            }
            unlock();
            break;
        }
        case ESP_GATTC_WRITE_CHAR_EVT: {
            lock();
            if (gattcIf == gattcIf_ && param->write.conn_id == connId_ &&
                writeState_ == WriteState::Pending && param->write.handle == writeHandle_) {
                writeStatus_ = param->write.status;
                writeState_  = param->write.status == ESP_GATT_OK ? WriteState::Ok
                                                                  : WriteState::Failed;
            }
            unlock();
            break;
        }
        case ESP_GATTC_DISCONNECT_EVT: {
            lock();
            if (gattcIf == gattcIf_ &&
                (connId_ == 0 || param->disconnect.conn_id == connId_)) {
                gattState_ = GattState::Idle;
                liveSubs_.clear();
                if (writeState_ == WriteState::Pending) {
                    writeState_  = WriteState::Failed;
                    writeStatus_ = ESP_GATT_ERROR;  // link dropped mid-write
                }
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
                liveSubs_.clear();
                if (writeState_ == WriteState::Pending) {
                    writeState_  = WriteState::Failed;
                    writeStatus_ = ESP_GATT_ERROR;  // link dropped mid-write
                }
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
    // `count` is controlled by the peer: bound it, and never let an
    // allocation failure throw on this task (an uncaught bad_alloc aborts).
    count = gattcore::clampGattDbCount(count);
    esp_gattc_db_elem_t* db = new (std::nothrow) esp_gattc_db_elem_t[count];
    if (db == nullptr) {
        lock();
        services_.clear();
        gattFail();
        unlock();
        return;
    }
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
            ci.handle = ch.attribute_handle;
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

// ---------------------------------------------------------------------------
// On-demand GATT operations (issue #14)
//
// Issued from the caller's task (the MCP task) strictly outside the mutex;
// esp_ble_gattc_* calls only post a message to the Bluedroid task.
// ---------------------------------------------------------------------------

std::vector<BLEServiceInfo> BLEScanner::getServices() const {
    lock();
    std::vector<BLEServiceInfo> out = services_;
    unlock();
    return out;
}

bool BLEScanner::writeStart(uint16_t handle, const std::vector<uint8_t>& value,
                            bool withResponse, std::string& err) {
    lock();
    if (gattState_ != GattState::Connected || loadPending_) {
        err = connected_ ? "enumeration in progress" : "not connected";
        unlock();
        return false;
    }
    if (writeState_ == WriteState::Pending) {
        err = "a write is already in progress";
        unlock();
        return false;
    }
    esp_gatt_if_t gif = gattcIf_;
    uint16_t cid = connId_;
    writeHandle_ = handle;
    writeStatus_ = 0;
    writeState_  = withResponse ? WriteState::Pending : WriteState::Ok;
    unlock();

    std::vector<uint8_t> buf(value);  // the API takes a non-const pointer
    esp_err_t rc = esp_ble_gattc_write_char(
        gif, cid, handle, static_cast<uint16_t>(buf.size()), buf.data(),
        withResponse ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
        ESP_GATT_AUTH_REQ_NONE);
    if (rc != ESP_OK) {
        lock();
        writeState_  = WriteState::Failed;
        writeStatus_ = rc;
        unlock();
        err = "controller rejected the write";
        return false;
    }
    return true;
}

BLEScanner::WriteState BLEScanner::getWriteResult(int& gattStatus) const {
    lock();
    WriteState st = writeState_;
    gattStatus = writeStatus_;
    unlock();
    return st;
}

bool BLEScanner::writeCcc(uint16_t handle, uint16_t value) {
    lock();
    esp_gatt_if_t gif = gattcIf_;
    uint16_t cid = connId_;
    unlock();
    esp_bt_uuid_t cccdUuid;
    cccdUuid.len = ESP_UUID_LEN_16;
    cccdUuid.uuid.uuid16 = 0x2902;
    esp_gattc_descr_elem_t descr;
    uint16_t n = 1;
    if (esp_ble_gattc_get_descr_by_char_handle(gif, cid, handle, cccdUuid, &descr, &n) !=
            ESP_OK || n == 0) {
        return false;
    }
    uint8_t val[2] = {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
    return esp_ble_gattc_write_char_descr(gif, cid, descr.handle, sizeof(val), val,
                                          ESP_GATT_WRITE_TYPE_RSP,
                                          ESP_GATT_AUTH_REQ_NONE) == ESP_OK;
}

bool BLEScanner::subscribe(uint16_t handle, const std::string& uuid, bool indicate,
                           std::string& err) {
    lock();
    if (gattState_ != GattState::Connected || loadPending_) {
        err = connected_ ? "enumeration in progress" : "not connected";
        unlock();
        return false;
    }
    if (liveSubs_.find(handle) == liveSubs_.end() &&
        liveSubs_.size() >= gattops::MAX_LIVE_SUBSCRIPTIONS) {
        err = "too many subscriptions";
        unlock();
        return false;
    }
    esp_gatt_if_t gif = gattcIf_;
    esp_bd_addr_t peer;
    memcpy(peer, peerAddr_, 6);
    // Accept NOTIFY_EVT for this handle and queue its values.
    notifyHandles_[handle] = uuid;
    liveSubs_[handle] = uuid;
    unlock();

    // Route notifications for this handle to our app, then enable them on
    // the peer.  A registration failure (Bluedroid caps registrations) is
    // not fatal: the CCCD write is what makes the peer send.
    esp_ble_gattc_register_for_notify(gif, peer, handle);
    if (!writeCcc(handle, gattops::cccdValue(true, indicate))) {
        lock();
        liveSubs_.erase(handle);
        unlock();
        err = "characteristic has no CCCD descriptor";
        return false;
    }
    return true;
}

bool BLEScanner::unsubscribe(uint16_t handle, std::string& err) {
    lock();
    bool known = liveSubs_.erase(handle) > 0;
    bool linkUp = gattState_ == GattState::Connected;
    esp_gatt_if_t gif = gattcIf_;
    esp_bd_addr_t peer;
    memcpy(peer, peerAddr_, 6);
    unlock();
    if (!known) {
        err = "not subscribed";
        return false;
    }
    if (linkUp) {
        writeCcc(handle, gattops::cccdValue(false, false));
        esp_ble_gattc_unregister_for_notify(gif, peer, handle);
    }
    return true;
}

std::vector<uint16_t> BLEScanner::liveSubscriptions() const {
    lock();
    std::vector<uint16_t> out;
    for (const auto& kv : liveSubs_) out.push_back(kv.first);
    unlock();
    return out;
}

std::vector<gattops::NotifyEvent> BLEScanner::drainNotifications(uint32_t& dropped) {
    lock();
    auto out = notifyQueue_.drain(dropped);
    unlock();
    return out;
}

bool BLEScanner::hasNotifications() const {
    lock();
    bool any = notifyQueue_.size() > 0;
    unlock();
    return any;
}

} // namespace mcp

#endif // NATIVE_TEST
