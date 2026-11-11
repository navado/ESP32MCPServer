#pragma once

#include <Arduino.h>
#include <map>
#include <string>
#include <vector>

#include "BLEGattCore.h"
#include "BLEScannerCore.h"

#ifndef NATIVE_TEST
#include <BLEAdvertisedDevice.h>
#include <BLEScan.h>
#include <esp_gattc_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#else
class BLEAdvertisedDevice;
class BLEScanResults;
struct esp_ble_gattc_cb_param_t;
using esp_gatt_if_t = uint16_t;
using esp_bd_addr_t = uint8_t[6];
using esp_gattc_cb_event_t = int;
#endif

namespace mcp {

// ---------------------------------------------------------------------------
// BLEScanner — BLE advertising capture + optional GATT enumeration.
//
// Purpose: capture what nearby devices broadcast so the payload format of a
// given app/device can be reverse engineered.  All captured data is exposed
// through MCP JSON-RPC methods (ble/scan, ble/scan/results, ble/scan/stop,
// ble/connect, ble/connect/results, ble/disconnect, ble/services/start,
// ble/services/results, ble/services/stop).
//
// Threading notes:
//   * startScan() uses the NON-blocking BLEScan::start(duration, cb, false)
//     overload so the calling (MCP) task never blocks on the scan.
//   * The scan-completion callback runs on the Bluetooth controller task
//     (BTC_TASK); it must NOT call esp_ble_gap_stop_scanning() or any other
//     controller API (deadlock + WDT abort).  It only flips a flag.
//   * GATT operations are event-driven on the raw esp_gattc API, with
//     per-operation deadlines enforced by a dedicated task: open (timeoutMs),
//     discovery (10 s), reads (5 s).  A slow or uncooperative peer can never
//     block the MCP task, and the connection is never freed underneath a
//     running operation (the task only exits from the Idle state).
//   * All state shared with the BLE stack tasks (scan results, GATT results,
//     connection state) is guarded by a recursive mutex; the lock is never
//     held across a blocking wait or a controller API call.
// ---------------------------------------------------------------------------
class BLEScanner {
public:
    BLEScanner();
    ~BLEScanner();

    BLEScanner(const BLEScanner&) = delete;
    BLEScanner& operator=(const BLEScanner&) = delete;

    // Initialise the BLE stack and hook the GATT client callback.  Safe to
    // call once at boot.
    void begin();

    // ---- Advertising scanning ----------------------------------------------

    // Start an advertising scan for `durationMs` (0 = continuous until stop()).
    // Returns true if a scan was started.  Non-blocking.
    bool startScan(uint32_t durationMs);

    // Stop a running scan.  Safe to call from a normal task context.
    void stopScan();

    // True while a scan is running.
    bool isScanning() const;

    // Snapshot of all reports captured since the last scan start.
    std::vector<BLEAdvReport> getResults();

    // MAC-independent payload registry snapshot (first-seen order).
    std::vector<BLEPayloadEntry> getPayloadRegistry();

    // millis() when the current/last scan started.
    uint64_t getScanStartedAt() const;

    // Requested scan duration in ms (0 = continuous).
    uint32_t getScanDurationMs() const;

    // Number of reports in the current snapshot.
    size_t reportCount() const;

    // Reset the captured report list.
    void clearResults();

    // ---- GATT client (raw esp_gattc, two-phase) ----------------------------

    // Begin an asynchronous connection to a peer by MAC
    // ("AA:BB:CC:DD:EE:FF" or "aa:bb:cc:dd:ee:ff").  Returns true if the
    // attempt was started; the open handshake completes (or is aborted) in
    // the background within `timeoutMs`.  Poll isConnected() /
    // isConnecting() or ble/connect/results for the outcome.
    bool connect(const std::string& mac, uint32_t timeoutMs = 5000);

    // True while an async connect attempt is still in flight.
    bool isConnecting() const;

    // True once the async connect has completed (link open).
    bool getConnectResults() const;

    // Drop the GATT link.  Safe to call while a discovery is in flight; the
    // GATT task is told to close and exits from the Idle state.
    void disconnect();

    bool isConnected() const;

    // Phase 1: enumerate services + characteristics of the connected device,
    // read readable values, and subscribe to all notifiable/indicatable
    // characteristics.  Returns immediately; enumeration runs event-driven
    // with the GATT task enforcing deadlines.  Wait for ready via
    // ble/services/results.
    std::vector<BLEServiceInfo> getServicesStart();

    // Phase 2: wait (bounded: 10 s discovery cap, then `notifyCaptureMs` for
    // notified/indicated values) for the enumeration, then return it with any
    // captured notification values folded in.  Blocks briefly on the MCP task.
    std::vector<BLEServiceInfo> getServicesResults(uint32_t notifyCaptureMs = 3000);

    // Phase 3: unsubscribe from all notifiable characteristics and close the
    // connection once enumeration work is done.
    void getServicesStop();

    // True while the GATT connection has pending enumeration work.
    bool isServicesBusy() const;

    // Advertising report callback (invoked by the BLE stack).
    void onAdv(BLEAdvertisedDevice* adv);

    // Scan completion hook (BLE stack callback, non-blocking start()).
    static void onScanCompleteStatic(BLEScanResults results);

    // Bluedroid GATT client event — invoked from the BLE library's registered
    // handler (chained in begin()).  Must not call controller APIs; it only
    // records state and signals the GATT task.
    void onGattcEvent(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf,
                      esp_ble_gattc_cb_param_t* param);

private:
    // ---- GATT task ---------------------------------------------------------

    void gattTaskBody();
    static void gattTaskStatic(void* param);
    void ensureGattTask();
    void gattLoadServices();
    void gattStartReads();
    void gattSubscribeAll();
    void gattUnsubscribeAll();
    // Called with the mutex held: mark enumeration complete and signal.
    void gattEnumDone();
    void gattFail();

    // ---- state (guarded by mutex_) -----------------------------------------

    mutable SemaphoreHandle_t mutex_ = nullptr;

    bool scanning_ = false;
    bool connected_ = false;
    std::string connectedMac_;
    std::vector<BLEAdvReport> reports_;
    // MAC -> set of payloads already recorded for that device (dedupe).
    std::map<std::string, std::map<std::string, bool>> seenPayloads_;
    // MAC-independent distinct payload registry (first-seen order).
    std::vector<BLEPayloadEntry> payloadRegistry_;
    uint64_t scanStartedAt_ = 0;   // millis() at scan start
    uint32_t scanDurationMs_ = 0;  // requested duration (0 = continuous)
    std::vector<BLEServiceInfo> services_;

#ifndef NATIVE_TEST
    // Raw esp_gattc client state (only valid while the BLE stack is active).
    esp_gatt_if_t gattcIf_ = 0;
    uint16_t connId_ = 0;
    esp_bd_addr_t peerAddr_ = {0};
    bool hasPeer_ = false;
    gattcore::GattState gattState_ = gattcore::GattState::Idle;
    uint32_t stateStartMillis_ = 0;
    uint32_t connectTimeoutMs_ = 5000;
    bool registerIssued_ = false;
    bool appRegistered_ = false;
    bool openIssued_ = false;
    bool searchIssued_ = false;
    bool closeIssued_ = false;
    bool loadPending_ = false;
    // Characteristic reads issued and awaiting READ_CHAR_EVT.
    std::map<uint16_t, std::string> pendingReads_;  // handle -> uuid
    TaskHandle_t gattTaskHandle_ = nullptr;
    SemaphoreHandle_t gattDoneSem_ = nullptr;
#endif

    // Notify-capture state (guarded by mutex_).
    std::map<uint16_t, std::string> notifyHandles_;  // handle -> uuid
    std::map<uint16_t, std::string> notifyValues_;   // handle -> latest hex

    // Helper: lazily create and take/give the shared-state mutex.  The mutex
    // is created on first use because globals may be constructed before the
    // FreeRTOS kernel is up.
    void lock() const;
    void unlock() const;
};

} // namespace mcp
