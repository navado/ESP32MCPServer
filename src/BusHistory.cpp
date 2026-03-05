#include "BusHistory.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstdio>
#include <cstring>

// Platform-specific free-heap query.
#ifdef NATIVE_TEST
static uint32_t platformFreeHeap() {
    // Return a generous fixed value so unit tests exercise the auto-config path.
    return 512u * 1024u;
}
#else
#  include <Arduino.h>
static uint32_t platformFreeHeap() {
    return static_cast<uint32_t>(ESP.getFreeHeap());
}
#endif

namespace mcp {

// ---------------------------------------------------------------------------
// Platform abstraction
// ---------------------------------------------------------------------------
uint32_t BusHistory::freeHeapBytes() {
    return platformFreeHeap();
}

// ---------------------------------------------------------------------------
// Auto-config: split a byte budget equally across the four buffer types.
// ---------------------------------------------------------------------------
BusHistoryConfig BusHistory::autoConfig(uint32_t budgetBytes,
                                         uint32_t safetyMarginBytes) {
    BusHistoryConfig c;
    c.safetyMarginBytes = safetyMarginBytes;
    if (budgetBytes <= safetyMarginBytes) return c; // degenerate: leave all zeroed
    const uint32_t usable  = budgetBytes - safetyMarginBytes;
    const uint32_t quarter = usable / 4;
    c.canFrameCount  = static_cast<uint32_t>(quarter / BYTES_PER_CAN);
    c.nmeaLineCount  = static_cast<uint32_t>(quarter / BYTES_PER_NMEA);
    c.nmea2000Count  = static_cast<uint32_t>(quarter / BYTES_PER_NMEA2000);
    c.obdiiCount     = static_cast<uint32_t>(quarter / BYTES_PER_OBDII);
    // Enforce a minimum of 1 so the buffer is never silently disabled.
    if (c.canFrameCount  == 0) c.canFrameCount  = 1;
    if (c.nmeaLineCount  == 0) c.nmeaLineCount  = 1;
    if (c.nmea2000Count  == 0) c.nmea2000Count  = 1;
    if (c.obdiiCount     == 0) c.obdiiCount     = 1;
    return c;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void BusHistory::begin() {
    loadConfig();
    allocateBuffers(config_);
}

void BusHistory::allocateBuffers(const BusHistoryConfig& cfg) {
    // Determine the byte budget.
    const uint32_t budget = (cfg.ramBudgetBytes > 0)
                            ? cfg.ramBudgetBytes
                            : freeHeapBytes();

    // Compute auto values for all types.
    BusHistoryConfig resolved = autoConfig(budget, cfg.safetyMarginBytes);

    // Per-type explicit overrides: a non-zero count supersedes the auto value.
    if (cfg.canFrameCount > 0) resolved.canFrameCount = cfg.canFrameCount;
    if (cfg.nmeaLineCount > 0) resolved.nmeaLineCount = cfg.nmeaLineCount;
    if (cfg.nmea2000Count > 0) resolved.nmea2000Count = cfg.nmea2000Count;
    if (cfg.obdiiCount    > 0) resolved.obdiiCount    = cfg.obdiiCount;

    canBuf_.resize     (resolved.canFrameCount);
    nmeaBuf_.resize    (resolved.nmeaLineCount);
    nmea2000Buf_.resize(resolved.nmea2000Count);
    obdiiBuf_.resize   (resolved.obdiiCount);
}

// ---------------------------------------------------------------------------
// Config persistence (LittleFS JSON)
// ---------------------------------------------------------------------------
void BusHistory::loadConfig() {
    config_ = BusHistoryConfig{}; // start from defaults
    if (!LittleFS.exists(CONFIG_PATH)) return;

    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) return;

    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        config_.canFrameCount     = doc["canFrameCount"]     | 0u;
        config_.nmeaLineCount     = doc["nmeaLineCount"]     | 0u;
        config_.nmea2000Count     = doc["nmea2000Count"]     | 0u;
        config_.obdiiCount        = doc["obdiiCount"]        | 0u;
        config_.ramBudgetBytes    = doc["ramBudgetBytes"]    | 0u;
        config_.safetyMarginBytes = doc["safetyMarginBytes"] | 65536u;
    }
    f.close();
}

void BusHistory::saveConfig() const {
    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) return;

    JsonDocument doc;
    doc["canFrameCount"]     = config_.canFrameCount;
    doc["nmeaLineCount"]     = config_.nmeaLineCount;
    doc["nmea2000Count"]     = config_.nmea2000Count;
    doc["obdiiCount"]        = config_.obdiiCount;
    doc["ramBudgetBytes"]    = config_.ramBudgetBytes;
    doc["safetyMarginBytes"] = config_.safetyMarginBytes;
    serializeJson(doc, f);
    f.close();
}

// ---------------------------------------------------------------------------
// Config get / set
// ---------------------------------------------------------------------------
BusHistoryConfig BusHistory::getConfig() const {
    return config_;
}

void BusHistory::setConfig(const BusHistoryConfig& cfg) {
    config_ = cfg;
    saveConfig();
    canBuf_.clear();
    nmeaBuf_.clear();
    nmea2000Buf_.clear();
    obdiiBuf_.clear();
    allocateBuffers(cfg);
    if (announceCb_) announceCb_();
}

// ---------------------------------------------------------------------------
// Feed
// ---------------------------------------------------------------------------
void BusHistory::feed(const BusReadResult& result) {
    for (const auto& f : result.rawFrames)      pushCAN(f);
    for (const auto& l : result.rawLines)       pushNMEA(l);
    for (const auto& d : result.parsedNMEA2000) pushNMEA2000(d);
    for (const auto& d : result.parsedOBDII)    pushOBDII(d);
}

void BusHistory::pushCAN     (const CANFrame&     f) { canBuf_.push(f); }
void BusHistory::pushNMEA    (const std::string&  l) { nmeaBuf_.push(l); }
void BusHistory::pushNMEA2000(const NMEA2000Data& d) { nmea2000Buf_.push(d); }
void BusHistory::pushOBDII   (const OBDIIData&    d) { obdiiBuf_.push(d); }

// ---------------------------------------------------------------------------
// Snapshots (oldest first)
// ---------------------------------------------------------------------------
std::vector<CANFrame>     BusHistory::snapshotCAN()      const { return canBuf_.snapshot(); }
std::vector<std::string>  BusHistory::snapshotNMEA()     const { return nmeaBuf_.snapshot(); }
std::vector<NMEA2000Data> BusHistory::snapshotNMEA2000() const { return nmea2000Buf_.snapshot(); }
std::vector<OBDIIData>    BusHistory::snapshotOBDII()    const { return obdiiBuf_.snapshot(); }

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

// Escape a string for embedding inside a JSON string literal.
std::string BusHistory::escapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

void BusHistory::appendSnapshotHeader(std::string& out, uint32_t requestId,
                                       const char* type, size_t capacity,
                                       size_t count) {
    char tmp[128];
    out += "{\"jsonrpc\":\"2.0\",\"id\":";
    std::snprintf(tmp, sizeof(tmp), "%u", (unsigned)requestId);
    out += tmp;
    out += ",\"result\":{\"type\":\"";
    out += type;
    out += "\",\"capacity\":";
    std::snprintf(tmp, sizeof(tmp), "%u", (unsigned)capacity);
    out += tmp;
    out += ",\"count\":";
    std::snprintf(tmp, sizeof(tmp), "%u", (unsigned)count);
    out += tmp;
    out += ",\"entries\":[";
}

void BusHistory::appendSnapshotFooter(std::string& out) {
    out += "]}}";
}

// ---------------------------------------------------------------------------
// CAN snapshot
// ---------------------------------------------------------------------------
std::string BusHistory::canSnapshotJson(uint32_t requestId, uint32_t limit) const {
    auto frames = snapshotCAN();
    if (limit > 0 && frames.size() > limit) frames.resize(limit);

    std::string out;
    out.reserve(frames.size() * 24 + 128);
    appendSnapshotHeader(out, requestId, "can", canBuf_.capacity(), frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i) out += ',';
        out += '"';
        out += frames[i].toString(); // e.g. "7E8#0441050300000000"
        out += '"';
    }
    appendSnapshotFooter(out);
    return out;
}

// ---------------------------------------------------------------------------
// NMEA 0183 snapshot
// ---------------------------------------------------------------------------
std::string BusHistory::nmeaSnapshotJson(uint32_t requestId, uint32_t limit) const {
    auto lines = snapshotNMEA();
    if (limit > 0 && lines.size() > limit) lines.resize(limit);

    std::string out;
    out.reserve(lines.size() * 80 + 128);
    appendSnapshotHeader(out, requestId, "nmea", nmeaBuf_.capacity(), lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += ',';
        out += '"';
        out += escapeJsonString(lines[i]);
        out += '"';
    }
    appendSnapshotFooter(out);
    return out;
}

// ---------------------------------------------------------------------------
// NMEA 2000 snapshot
// ---------------------------------------------------------------------------
std::string BusHistory::nmea2000SnapshotJson(uint32_t requestId, uint32_t limit) const {
    auto msgs = snapshotNMEA2000();
    if (limit > 0 && msgs.size() > limit) msgs.resize(limit);

    std::string out;
    out.reserve(msgs.size() * 80 + 128);
    appendSnapshotHeader(out, requestId, "nmea2000", nmea2000Buf_.capacity(), msgs.size());

    char tmp[128];
    for (size_t i = 0; i < msgs.size(); ++i) {
        if (i) out += ',';
        const auto& d = msgs[i];
        std::snprintf(tmp, sizeof(tmp),
                      "{\"pgn\":%u,\"priority\":%u,\"source\":%u,\"valid\":%s,\"name\":\"",
                      (unsigned)d.pgn, (unsigned)d.priority, (unsigned)d.source,
                      d.valid ? "true" : "false");
        out += tmp;
        out += escapeJsonString(d.name);
        out += '"';
        if (d.hasPosition) {
            std::snprintf(tmp, sizeof(tmp), ",\"lat\":%.7f,\"lon\":%.7f",
                          d.latitude, d.longitude);
            out += tmp;
        }
        if (d.hasCOGSOG) {
            std::snprintf(tmp, sizeof(tmp), ",\"cog\":%.2f,\"sogKnots\":%.2f",
                          d.cogDegrees, d.sogKnots);
            out += tmp;
        }
        if (d.hasHeading) {
            std::snprintf(tmp, sizeof(tmp), ",\"heading\":%.2f", d.headingDegrees);
            out += tmp;
        }
        if (d.hasAttitude) {
            std::snprintf(tmp, sizeof(tmp), ",\"yaw\":%.2f,\"pitch\":%.2f,\"roll\":%.2f",
                          d.yawDeg, d.pitchDeg, d.rollDeg);
            out += tmp;
        }
        if (d.hasSTW) {
            std::snprintf(tmp, sizeof(tmp), ",\"stwKnots\":%.2f", d.stwKnots);
            out += tmp;
        }
        if (d.hasDepth) {
            std::snprintf(tmp, sizeof(tmp), ",\"depthM\":%.2f", d.depthMetres);
            out += tmp;
        }
        if (d.hasWind) {
            std::snprintf(tmp, sizeof(tmp),
                          ",\"windSpeedKnots\":%.2f,\"windAngle\":%.1f,\"windApparent\":%s",
                          d.windSpeedKnots, d.windAngleDeg,
                          d.windApparent ? "true" : "false");
            out += tmp;
        }
        if (d.hasWaterTemp) {
            std::snprintf(tmp, sizeof(tmp), ",\"waterTempC\":%.2f", d.waterTempC);
            out += tmp;
        }
        out += '}';
    }
    appendSnapshotFooter(out);
    return out;
}

// ---------------------------------------------------------------------------
// OBD-II snapshot
// ---------------------------------------------------------------------------
std::string BusHistory::obdiiSnapshotJson(uint32_t requestId, uint32_t limit) const {
    auto msgs = snapshotOBDII();
    if (limit > 0 && msgs.size() > limit) msgs.resize(limit);

    std::string out;
    out.reserve(msgs.size() * 80 + 128);
    appendSnapshotHeader(out, requestId, "obdii", obdiiBuf_.capacity(), msgs.size());

    char tmp[128];
    for (size_t i = 0; i < msgs.size(); ++i) {
        if (i) out += ',';
        const auto& d = msgs[i];
        std::snprintf(tmp, sizeof(tmp),
                      "{\"service\":%u,\"pid\":%u,\"valid\":%s,\"name\":\"",
                      (unsigned)d.service, (unsigned)d.pid,
                      d.valid ? "true" : "false");
        out += tmp;
        out += escapeJsonString(d.name);
        std::snprintf(tmp, sizeof(tmp), "\",\"value\":%.6g,\"unit\":\"", d.value);
        out += tmp;
        out += escapeJsonString(d.unit);
        out += '"';
        if (!d.unit2.empty()) {
            std::snprintf(tmp, sizeof(tmp), ",\"value2\":%.6g,\"unit2\":\"", d.value2);
            out += tmp;
            out += escapeJsonString(d.unit2);
            out += '"';
        }
        out += '}';
    }
    appendSnapshotFooter(out);
    return out;
}

// ---------------------------------------------------------------------------
// configToJson — full status for HTTP GET and the discovery broadcast
// ---------------------------------------------------------------------------
std::string BusHistory::configToJson() const {
    const uint32_t freeHeap = freeHeapBytes();
    char tmp[256];
    std::string out = "{";

    // --- configured (what the user set; 0 = auto) -------------------------
    out += "\"configured\":{";
    std::snprintf(tmp, sizeof(tmp),
                  "\"ramBudgetBytes\":%u,\"safetyMarginBytes\":%u,"
                  "\"canFrameCount\":%u,\"nmeaLineCount\":%u,"
                  "\"nmea2000Count\":%u,\"obdiiCount\":%u",
                  (unsigned)config_.ramBudgetBytes,
                  (unsigned)config_.safetyMarginBytes,
                  (unsigned)config_.canFrameCount,
                  (unsigned)config_.nmeaLineCount,
                  (unsigned)config_.nmea2000Count,
                  (unsigned)config_.obdiiCount);
    out += tmp;
    out += "},";

    // --- allocated (actual ring-buffer sizes) -----------------------------
    out += "\"allocated\":{";
    std::snprintf(tmp, sizeof(tmp),
                  "\"canFrameCount\":%u,\"nmeaLineCount\":%u,"
                  "\"nmea2000Count\":%u,\"obdiiCount\":%u",
                  (unsigned)canBuf_.capacity(),
                  (unsigned)nmeaBuf_.capacity(),
                  (unsigned)nmea2000Buf_.capacity(),
                  (unsigned)obdiiBuf_.capacity());
    out += tmp;
    out += "},";

    // --- sizes (bytes per item, for UI to compute byte cost) --------------
    std::snprintf(tmp, sizeof(tmp),
                  "\"bytesPerCan\":%u,\"bytesPerNmea\":%u,"
                  "\"bytesPerNmea2000\":%u,\"bytesPerObdii\":%u,"
                  "\"freeHeapBytes\":%u}",
                  (unsigned)BYTES_PER_CAN, (unsigned)BYTES_PER_NMEA,
                  (unsigned)BYTES_PER_NMEA2000, (unsigned)BYTES_PER_OBDII,
                  (unsigned)freeHeap);
    out += tmp;
    return out;
}

} // namespace mcp
