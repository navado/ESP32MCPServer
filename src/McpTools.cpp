// MCP tool catalogue (issue #13): exposes the device's JSON-RPC methods as
// tools so LLM clients discover them through tools/list.
//
// Every tool wraps an existing method (same parameters, same result) and
// inherits its authorization.  Tools whose method is not registered on this
// build (e.g. BLE on 4 MB boards) are hidden from tools/list automatically.
// Schemas are string literals: they stay in flash and are embedded verbatim.

#include "McpTools.h"

namespace mcp {

namespace {

const char kEmpty[] = R"J({"type":"object","properties":{},"additionalProperties":false})J";

const char kSensorsRead[] = R"J({"type":"object","properties":{
"sensorId":{"type":"string","description":"Sensor id from sensors_scan, e.g. bme280_0x76. Omit to read every sensor."}},
"additionalProperties":false})J";

const char kBusHistoryRead[] = R"J({"type":"object","properties":{
"type":{"type":"string","enum":["can","nmea","nmea2000","obdii"],"default":"can","description":"Which bus history buffer to read"},
"limit":{"type":"integer","minimum":0,"description":"Newest N entries; 0 or omitted = all stored"}},
"additionalProperties":false})J";

const char kBusHistoryConfigSet[] = R"J({"type":"object","properties":{
"canFrameCount":{"type":"integer","minimum":0},
"nmeaLineCount":{"type":"integer","minimum":0},
"nmea2000Count":{"type":"integer","minimum":0},
"obdiiCount":{"type":"integer","minimum":0},
"ramBudgetBytes":{"type":"integer","minimum":0,"description":"0 = size automatically from free heap"},
"safetyMarginBytes":{"type":"integer","minimum":0}},
"additionalProperties":false})J";

const char kMetricsList[] = R"J({"type":"object","properties":{
"category":{"type":"string","description":"Only metrics in this category"}},
"additionalProperties":false})J";

const char kMetricsGet[] = R"J({"type":"object","properties":{
"name":{"type":"string","description":"Metric name from metrics_list. Omit for all metrics."}},
"additionalProperties":false})J";

const char kMetricsHistory[] = R"J({"type":"object","properties":{
"name":{"type":"string","description":"Metric name from metrics_list"},
"seconds":{"type":"integer","minimum":1,"default":3600,"description":"Look-back window"}},
"required":["name"],"additionalProperties":false})J";

const char kBleScan[] = R"J({"type":"object","properties":{
"durationMs":{"type":"integer","minimum":0,"default":5000,"description":"Scan length; 0 = continuous until ble_scan_stop"},
"minRssi":{"type":"integer","maximum":0,"description":"Only store devices at least this strong (dBm)"},
"manufacturerId":{"type":"string","description":"Hex prefix of manufacturer data, company id little-endian (0xDEAD -> \"adde\")"},
"serviceUuid":{"type":"string","description":"Only store devices advertising this service UUID"},
"macPrefix":{"type":"string","description":"Only store devices whose MAC starts with this, e.g. \"aa:bb:cc\""}},
"additionalProperties":false})J";

const char kBleScanResults[] = R"J({"type":"object","properties":{
"mac":{"type":"string","description":"Return only this device, with its full payload history"},
"serviceUuid":{"type":"string"},
"manufacturerId":{"type":"string"},
"macPrefix":{"type":"string"}},
"additionalProperties":false})J";

const char kBleConnect[] = R"J({"type":"object","properties":{
"mac":{"type":"string","pattern":"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$","description":"Peer address aa:bb:cc:dd:ee:ff"},
"timeoutMs":{"type":"integer","minimum":1,"default":5000},
"addrType":{"type":"string","enum":["public","random","rpa-public","rpa-random"],"description":"Default: the type seen in the last scan (public if not seen)"}},
"required":["mac"],"additionalProperties":false})J";

const char kBleServicesResults[] = R"J({"type":"object","properties":{
"notifyCaptureMs":{"type":"integer","minimum":0,"maximum":10000,"default":3000,"description":"How long to collect notified values"}},
"additionalProperties":false})J";

const char kBleCharRef[] = R"J({"type":"object","properties":{
"handle":{"type":"integer","minimum":1,"maximum":65535,"description":"Characteristic handle from ble_services_results"},
"uuid":{"type":"string","description":"Characteristic UUID (if unique on the device)"}},
"additionalProperties":false})J";

const char kBleWrite[] = R"J({"type":"object","properties":{
"handle":{"type":"integer","minimum":1,"maximum":65535,"description":"Characteristic handle from ble_services_results"},
"uuid":{"type":"string","description":"Characteristic UUID (if unique on the device)"},
"value":{"type":"string","description":"Bytes to write as hex, e.g. \"01ff\" or \"01:ff\" (max 512 bytes)"},
"withResponse":{"type":"boolean","default":true,"description":"Write request (acknowledged) or write command (no response)"},
"timeoutMs":{"type":"integer","minimum":1,"maximum":5000,"default":2000}},
"required":["value"],"additionalProperties":false})J";

const char kBleSubscribe[] = R"J({"type":"object","properties":{
"handle":{"type":"integer","minimum":1,"maximum":65535,"description":"Characteristic handle from ble_services_results"},
"uuid":{"type":"string","description":"Characteristic UUID (if unique on the device)"},
"indicate":{"type":"boolean","description":"Use indications instead of notifications (default: notify when supported)"}},
"additionalProperties":false})J";

ToolDef tool(const char* name, const char* title, const char* method, const char* schema,
             const char* description, bool readOnly, bool destructive, bool idempotent,
             bool openWorld) {
    ToolDef t;
    t.name        = name;
    t.title       = title;
    t.method      = method;
    t.inputSchema = schema;
    t.description = description;
    t.readOnly    = readOnly;
    t.destructive = destructive;
    t.idempotent  = idempotent;
    t.openWorld   = openWorld;
    return t;
}

}  // namespace

void registerMcpTools(MCPServer& s) {
    //                 name                     title                         method                    schema
    s.registerTool(tool("sensors_scan", "Scan I2C sensors", "sensors/i2c/scan", kEmpty,
        "Probe the I2C bus and list the sensors found, with their ids and parameters. "
        "Re-initialises detected sensors.", false, false, true, false));
    s.registerTool(tool("sensors_read", "Read sensors", "sensors/read", kSensorsRead,
        "Take a reading from one sensor, or from all sensors.", true, false, false, false));
    s.registerTool(tool("bus_history_read", "Read bus history", "bus/history/read", kBusHistoryRead,
        "Recent CAN frames, NMEA 0183 sentences, NMEA 2000 PGNs or OBD-II values "
        "captured from the vehicle/boat buses.", true, false, false, false));
    s.registerTool(tool("bus_history_config_get", "Get bus history config", "bus/history/config/get",
        kEmpty, "Current bus history buffer sizes and memory budget.", true, false, true, false));
    s.registerTool(tool("bus_history_config_set", "Set bus history config", "bus/history/config/set",
        kBusHistoryConfigSet,
        "Resize the bus history buffers and persist the configuration. Clears the captured "
        "history.", false, true, true, false));
    s.registerTool(tool("metrics_list", "List metrics", "metrics/list", kMetricsList,
        "Names, types and categories of the device metrics.", true, false, true, false));
    s.registerTool(tool("metrics_get", "Get metrics", "metrics/get", kMetricsGet,
        "Current value of one metric, or of all metrics.", true, false, false, false));
    s.registerTool(tool("metrics_history", "Metric history", "metrics/history", kMetricsHistory,
        "Recorded values of a metric over a time window.", true, false, false, false));
    s.registerTool(tool("logs_clear", "Clear metric history", "logs/clear", kEmpty,
        "Delete all recorded metric history.", false, true, true, false));

    // BLE (hidden on builds without BLE: the methods are not registered).
    s.registerTool(tool("ble_scan", "Start BLE scan", "ble/scan", kBleScan,
        "Start a Bluetooth LE advertising scan in the background. Fetch results with "
        "ble_scan_results or ble_scan_poll. Fails with 'Feature busy' if a scan is running.",
        false, false, false, true));
    s.registerTool(tool("ble_scan_poll", "Poll BLE payloads", "ble/scan/poll", kEmpty,
        "Distinct advertising payloads seen so far, without stopping the scan.",
        true, false, false, true));
    s.registerTool(tool("ble_scan_results", "BLE scan results", "ble/scan/results", kBleScanResults,
        "Stop the scan and return the devices found (MAC, name, RSSI, services, "
        "manufacturer data, payload history).", false, false, false, true));
    s.registerTool(tool("ble_scan_payloads", "BLE payload registry", "ble/scan/payloads", kEmpty,
        "Stop the scan and return every distinct advertising payload across all devices, "
        "independent of MAC address rotation.", false, false, false, true));
    s.registerTool(tool("ble_scan_stop", "Stop BLE scan", "ble/scan/stop", kEmpty,
        "Stop a running scan.", false, false, true, false));
    s.registerTool(tool("ble_connect", "Connect to BLE device", "ble/connect", kBleConnect,
        "Open a GATT connection to a peripheral (asynchronous; check with "
        "ble_connect_results).", false, false, false, true));
    s.registerTool(tool("ble_connect_results", "BLE connection state", "ble/connect/results", kEmpty,
        "Whether the last connect is still in progress or connected.", true, false, true, false));
    s.registerTool(tool("ble_services_start", "Enumerate GATT services", "ble/services/start", kEmpty,
        "Discover the connected device's services and characteristics, read readable values "
        "and subscribe to notifications (background; fetch with ble_services_results).",
        false, false, false, true));
    s.registerTool(tool("ble_services_results", "GATT enumeration results", "ble/services/results",
        kBleServicesResults,
        "Wait for the enumeration and notified values, then return services, characteristics "
        "and values.", true, false, false, true));
    s.registerTool(tool("ble_services_stop", "Stop GATT session", "ble/services/stop", kEmpty,
        "Unsubscribe from notifications and close the link.", false, false, true, true));
    s.registerTool(tool("ble_write", "Write BLE characteristic", "ble/write", kBleWrite,
        "Write bytes to a characteristic of the connected device (by handle or UUID from "
        "ble_services_results). Changes the state of a third-party device.",
        false, true, false, true));
    s.registerTool(tool("ble_subscribe", "Subscribe to BLE characteristic", "ble/subscribe",
        kBleSubscribe,
        "Enable notifications or indications on a characteristic. Collect values with "
        "ble_notifications.", false, false, true, true));
    s.registerTool(tool("ble_unsubscribe", "Unsubscribe from BLE characteristic",
        "ble/unsubscribe", kBleCharRef, "Disable notifications on a characteristic.",
        false, false, true, true));
    s.registerTool(tool("ble_notifications", "Read BLE notifications", "ble/notifications",
        kEmpty, "Values notified on subscribed characteristics since the last call.",
        false, false, false, true));
    s.registerTool(tool("ble_disconnect", "Disconnect BLE device", "ble/disconnect", kEmpty,
        "Drop the GATT connection.", false, false, true, true));
}

} // namespace mcp
