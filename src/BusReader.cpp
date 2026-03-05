#include "BusReader.h"
#include "NMEAParser.h"
#include "CANParser.h"
#include <cstdio>
#include <cstring>

// When building for native tests there is no millis().
// Provide a fallback using clock().
#ifdef NATIVE_TEST
#  include <ctime>
static uint32_t native_millis() {
    return static_cast<uint32_t>(
        static_cast<uint64_t>(clock()) * 1000ULL / CLOCKS_PER_SEC);
}
#  define PLATFORM_MILLIS() native_millis()
#else
// Arduino millis() is already declared globally
extern "C" unsigned long millis();
#  define PLATFORM_MILLIS() static_cast<uint32_t>(millis())
#endif

namespace mcp {

// ---------------------------------------------------------------------------
// BusReadResult::toJson  — hand-written JSON, no ArduinoJson needed
// ---------------------------------------------------------------------------
static void appendJsonString(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    out += '"';
}

std::string BusReadResult::toJson(ParseMode mode) const {
    std::string j = "{";

    // Metadata
    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "\"durationMs\":%u,", durationMs);
    j += tmp;

    if (mode == ParseMode::RAW) {
        // Serial raw lines
        std::snprintf(tmp, sizeof(tmp), "\"linesReceived\":%zu,\"rawLines\":[",
                      linesReceived);
        j += tmp;
        for (size_t i = 0; i < rawLines.size(); ++i) {
            if (i) j += ",";
            appendJsonString(j, rawLines[i]);
        }
        j += "],";

        // CAN raw frames
        std::snprintf(tmp, sizeof(tmp), "\"framesReceived\":%zu,\"rawFrames\":[",
                      framesReceived);
        j += tmp;
        for (size_t i = 0; i < rawFrames.size(); ++i) {
            if (i) j += ",";
            appendJsonString(j, rawFrames[i].toString());
        }
        j += "]";
    } else {
        // Parsed NMEA 0183
        std::snprintf(tmp, sizeof(tmp), "\"linesReceived\":%zu,\"parsedNMEA\":[",
                      linesReceived);
        j += tmp;
        for (size_t i = 0; i < parsedNMEA.size(); ++i) {
            if (i) j += ",";
            const auto& n = parsedNMEA[i];
            j += "{\"type\":";
            appendJsonString(j, n.type);
            j += ",\"valid\":";
            j += (n.valid ? "true" : "false");
            if (n.hasPosition) {
                std::snprintf(tmp, sizeof(tmp),
                    ",\"lat\":%.7f,\"lon\":%.7f", n.latitude, n.longitude);
                j += tmp;
            }
            if (n.hasSpeed) {
                std::snprintf(tmp, sizeof(tmp), ",\"speedKnots\":%.2f", n.speedKnots);
                j += tmp;
            }
            if (n.hasDepth) {
                std::snprintf(tmp, sizeof(tmp), ",\"depthM\":%.2f", n.depthMetres);
                j += tmp;
            }
            if (n.hasHeading) {
                std::snprintf(tmp, sizeof(tmp), ",\"heading\":%.1f", n.headingDegrees);
                j += tmp;
            }
            if (n.hasWind) {
                std::snprintf(tmp, sizeof(tmp), ",\"windAngle\":%.1f,\"windSpeed\":%.1f",
                              n.windAngle, n.windSpeed);
                j += tmp;
            }
            j += ",\"raw\":";
            appendJsonString(j, n.raw);
            j += "}";
        }
        j += "],";

        // Parsed NMEA 2000
        std::snprintf(tmp, sizeof(tmp), "\"framesReceived\":%zu,\"parsedNMEA2000\":[",
                      framesReceived);
        j += tmp;
        for (size_t i = 0; i < parsedNMEA2000.size(); ++i) {
            if (i) j += ",";
            const auto& d = parsedNMEA2000[i];
            std::snprintf(tmp, sizeof(tmp), "{\"pgn\":%u,\"name\":", d.pgn);
            j += tmp;
            appendJsonString(j, d.name);
            j += ",\"valid\":";
            j += (d.valid ? "true" : "false");
            if (d.hasPosition) {
                std::snprintf(tmp, sizeof(tmp), ",\"lat\":%.7f,\"lon\":%.7f",
                              d.latitude, d.longitude);
                j += tmp;
            }
            if (d.hasCOGSOG) {
                std::snprintf(tmp, sizeof(tmp), ",\"cog\":%.1f,\"sog\":%.2f",
                              d.cogDegrees, d.sogKnots);
                j += tmp;
            }
            if (d.hasHeading) {
                std::snprintf(tmp, sizeof(tmp), ",\"heading\":%.1f", d.headingDegrees);
                j += tmp;
            }
            if (d.hasDepth) {
                std::snprintf(tmp, sizeof(tmp), ",\"depthM\":%.2f", d.depthMetres);
                j += tmp;
            }
            if (d.hasWind) {
                std::snprintf(tmp, sizeof(tmp), ",\"windAngle\":%.1f,\"windSpeedKnots\":%.2f",
                              d.windAngleDeg, d.windSpeedKnots);
                j += tmp;
            }
            if (d.hasWaterTemp) {
                std::snprintf(tmp, sizeof(tmp), ",\"waterTempC\":%.2f", d.waterTempC);
                j += tmp;
            }
            j += "}";
        }
        j += "],";

        // Parsed OBD-II
        j += "\"parsedOBDII\":[";
        for (size_t i = 0; i < parsedOBDII.size(); ++i) {
            if (i) j += ",";
            const auto& d = parsedOBDII[i];
            std::snprintf(tmp, sizeof(tmp),
                          "{\"service\":%u,\"pid\":%u,\"name\":",
                          static_cast<unsigned>(d.service),
                          static_cast<unsigned>(d.pid));
            j += tmp;
            appendJsonString(j, d.name);
            std::snprintf(tmp, sizeof(tmp), ",\"value\":%.6g,\"unit\":", d.value);
            j += tmp;
            appendJsonString(j, d.unit);
            if (!d.unit2.empty()) {
                std::snprintf(tmp, sizeof(tmp), ",\"value2\":%.6g,\"unit2\":", d.value2);
                j += tmp;
                appendJsonString(j, d.unit2);
            }
            j += ",\"valid\":";
            j += (d.valid ? "true" : "false");
            j += "}";
        }
        j += "]";
    }

    j += "}";
    return j;
}

// ---------------------------------------------------------------------------
// SerialBusReader
// ---------------------------------------------------------------------------

SerialBusReader::SerialBusReader(IBusPort& port,
                                  std::function<uint32_t()> clock)
    : port_(port), clock_(clock) {}

/* static */ uint32_t SerialBusReader::defaultClock() {
    return PLATFORM_MILLIS();
}

BusReadResult SerialBusReader::readFor(uint32_t durationMs, ParseMode mode) {
    BusReadResult result;
    result.durationMs = durationMs;

    uint32_t start = now();

    // Use do-while so the inner drain loop runs at least once before the
    // time check, ensuring all buffered bytes are consumed by mock ports.
    do {
        while (port_.available()) {
            int c = port_.read();
            if (c < 0) break;

            if (c == '\n') {
                // Remove trailing '\r' if present
                if (!lineBuffer_.empty() && lineBuffer_.back() == '\r') {
                    lineBuffer_.pop_back();
                }
                if (!lineBuffer_.empty()) {
                    if (mode == ParseMode::RAW) {
                        result.rawLines.push_back(lineBuffer_);
                    } else {
                        ParsedNMEA parsed = NMEAParser::parse(lineBuffer_);
                        result.parsedNMEA.push_back(std::move(parsed));
                    }
                    ++result.linesReceived;
                }
                lineBuffer_.clear();
            } else {
                lineBuffer_ += static_cast<char>(c);
            }
        }
    } while ((now() - start) < durationMs);

    return result;
}

// ---------------------------------------------------------------------------
// CANBusReader
// ---------------------------------------------------------------------------

CANBusReader::CANBusReader(ICANPort& port,
                            std::function<uint32_t()> clock)
    : port_(port), clock_(clock) {}

/* static */ uint32_t CANBusReader::defaultClock() {
    return PLATFORM_MILLIS();
}

BusReadResult CANBusReader::readFor(uint32_t durationMs, ParseMode mode) {
    BusReadResult result;
    result.durationMs = durationMs;

    uint32_t start     = now();
    uint32_t remaining = durationMs;

    while (remaining > 0) {
        CANFrameRaw raw;
        uint32_t timeout = (remaining < 10) ? remaining : 10; // poll in 10 ms slices
        if (port_.receive(raw, timeout)) {
            ++result.framesReceived;

            // Convert CANFrameRaw → CANFrame
            CANFrame frame;
            frame.id       = raw.id;
            frame.extended = raw.extended;
            frame.rtr      = raw.rtr;
            frame.dlc      = raw.dlc;
            std::memcpy(frame.data, raw.data, 8);

            if (mode == ParseMode::RAW) {
                result.rawFrames.push_back(frame);
            } else {
                if (CANParser::isOBDIIResponse(frame)) {
                    result.parsedOBDII.push_back(CANParser::parseOBDII(frame));
                } else if (CANParser::isNMEA2000(frame)) {
                    result.parsedNMEA2000.push_back(CANParser::parseNMEA2000(frame));
                } else {
                    result.rawFrames.push_back(frame); // unrecognised → keep raw
                }
            }
        }

        uint32_t elapsed = now() - start;
        remaining = (elapsed < durationMs) ? (durationMs - elapsed) : 0;
    }

    return result;
}

} // namespace mcp
