#include "CANParser.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace mcp {

// ---------------------------------------------------------------------------
// CANFrame helpers
// ---------------------------------------------------------------------------

std::string CANFrame::toString() const {
    char buf[32];
    // Format: "7E8#0441050300000000"
    if (extended) {
        std::snprintf(buf, sizeof(buf), "%08X#", id);
    } else {
        std::snprintf(buf, sizeof(buf), "%03X#", id);
    }
    std::string s = buf;
    for (int i = 0; i < dlc && i < 8; ++i) {
        std::snprintf(buf, sizeof(buf), "%02X", data[i]);
        s += buf;
    }
    return s;
}

// ---------------------------------------------------------------------------
// CANParser utilities
// ---------------------------------------------------------------------------

std::string CANParser::frameToHex(const CANFrame& frame) {
    return frame.toString();
}

// Extract the 18-bit PGN from a 29-bit J1939/NMEA2000 CAN ID.
//
// 29-bit layout:  [3-bit priority | 1-bit R | 1-bit DP | 8-bit PF | 8-bit PS | 8-bit SA]
//
// If PF >= 0xF0 (240) the message is peer-to-peer (PDU2): PGN includes PS.
// If PF <  0xF0 the message is broadcast (PDU1): PS is destination, not part of PGN.
uint32_t CANParser::extractPGN(uint32_t canId) {
    // 29-bit layout: [priority:3][R:1][DP:1][PF:8][PS:8][SA:8]
    //                 bits 28-26   25   24   23-16  15-8   7-0
    uint8_t dp = (canId >> 24) & 0x01;
    uint8_t pf = (canId >> 16) & 0xFF;
    uint8_t ps = (canId >>  8) & 0xFF;

    if (pf >= 240) {
        // PDU2 — PS is group extension, part of PGN
        return (static_cast<uint32_t>(dp) << 16) |
               (static_cast<uint32_t>(pf) << 8)  |
                static_cast<uint32_t>(ps);
    } else {
        // PDU1 — PS is destination address, not part of PGN
        return (static_cast<uint32_t>(dp) << 16) |
               (static_cast<uint32_t>(pf) << 8);
    }
}

// ---------------------------------------------------------------------------
// OBD-II
// ---------------------------------------------------------------------------

bool CANParser::isOBDIIResponse(const CANFrame& frame) {
    // 11-bit standard CAN, response IDs 0x7E8..0x7EF
    return !frame.extended && (frame.id >= 0x7E8 && frame.id <= 0x7EF);
}

OBDIIData CANParser::parseOBDII(const CANFrame& frame) {
    OBDIIData d;
    if (!isOBDIIResponse(frame)) return d;
    if (frame.dlc < 4) return d;

    // data[0] = number of additional bytes
    // data[1] = 0x40 | service (response mode = request mode + 0x40)
    // data[2] = PID
    // data[3..6] = A B C D
    uint8_t mode = frame.data[1];
    if ((mode & 0x40) == 0) return d; // not a positive response

    d.service = mode & 0x3F;
    d.pid     = frame.data[2];
    const uint8_t* abcd = &frame.data[3];
    d = decodeOBDPID(d.service, d.pid, abcd);
    return d;
}

OBDIIData CANParser::decodeOBDPID(uint8_t service, uint8_t pid,
                                    const uint8_t* abcd) {
    OBDIIData d;
    d.service = service;
    d.pid     = pid;
    d.valid   = true;

    // Service 01 — show current data
    if (service == 0x01) {
        switch (pid) {
        case 0x04: // Calculated engine load
            d.name  = "Engine Load";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x05: // Engine coolant temperature
            d.name  = "Coolant Temp";
            d.value = static_cast<int>(abcd[0]) - 40;
            d.unit  = "°C";
            break;
        case 0x0C: // Engine RPM
            d.name  = "Engine RPM";
            d.value = ((abcd[0] << 8) | abcd[1]) / 4.0;
            d.unit  = "rpm";
            break;
        case 0x0D: // Vehicle speed
            d.name  = "Vehicle Speed";
            d.value = abcd[0];
            d.unit  = "km/h";
            break;
        case 0x0F: // Intake air temperature
            d.name  = "Intake Air Temp";
            d.value = static_cast<int>(abcd[0]) - 40;
            d.unit  = "°C";
            break;
        case 0x10: // Mass air flow sensor
            d.name  = "MAF Rate";
            d.value = ((abcd[0] << 8) | abcd[1]) / 100.0;
            d.unit  = "g/s";
            break;
        case 0x11: // Throttle position
            d.name  = "Throttle Position";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x1C: // OBD standards compliance
            d.name  = "OBD Standard";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x2F: // Fuel tank level
            d.name  = "Fuel Level";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x33: // Barometric pressure
            d.name  = "Barometric Pressure";
            d.value = abcd[0];
            d.unit  = "kPa";
            break;
        case 0x46: // Ambient air temperature
            d.name  = "Ambient Temp";
            d.value = static_cast<int>(abcd[0]) - 40;
            d.unit  = "°C";
            break;
        default:
            d.name  = "PID 0x" + [pid]() {
                char buf[8]; std::snprintf(buf, sizeof(buf), "%02X", pid);
                return std::string(buf);
            }();
            d.value = abcd[0];
            d.unit  = "";
            break;
        }
    } else {
        d.valid = false;
    }
    return d;
}

// ---------------------------------------------------------------------------
// NMEA 2000 identification
// ---------------------------------------------------------------------------

bool CANParser::isNMEA2000(const CANFrame& frame) {
    if (!frame.extended) return false;
    uint32_t pgn = extractPGN(frame.id);
    switch (pgn) {
    case 127250: case 127257: case 128259: case 128267:
    case 129025: case 129026: case 130306: case 130310:
        return true;
    default:
        return false;
    }
}

NMEA2000Data CANParser::parseNMEA2000(const CANFrame& frame) {
    if (!frame.extended) return {};
    uint32_t pgn = extractPGN(frame.id);

    switch (pgn) {
    case 127250: return decode127250(frame);
    case 127257: return decode127257(frame);
    case 128259: return decode128259(frame);
    case 128267: return decode128267(frame);
    case 129025: return decode129025(frame);
    case 129026: return decode129026(frame);
    case 130306: return decode130306(frame);
    case 130310: return decode130310(frame);
    default: {
        NMEA2000Data d;
        d.pgn      = pgn;
        d.priority = (frame.id >> 26) & 0x07;
        d.source   = frame.id & 0xFF;
        d.name     = "Unknown PGN";
        return d;
    }
    }
}

// ---------------------------------------------------------------------------
// PGN decoders
// All angles in NMEA2000 are in units of 0.0001 rad; 1 rad = 57.2957795 deg.
// All speeds in m/s * 0.01; water temps in 0.01 K (offset from 0K).
// ---------------------------------------------------------------------------

static constexpr double RAD_TO_DEG = 57.2957795130823;

NMEA2000Data CANParser::decode127250(const CANFrame& f) {
    // Vessel Heading — PGN 127250
    // Bytes: SID(1), Heading[0..1](2), Deviation[2..3](2), Variation[4..5](2), Reference(1)
    NMEA2000Data d;
    d.pgn      = 127250;
    d.name     = "Vessel Heading";
    d.priority = (f.id >> 26) & 0x07;
    d.source   = f.id & 0xFF;
    if (f.dlc < 7) return d;

    int16_t headingRaw   = le16s(&f.data[1]);
    int16_t variationRaw = le16s(&f.data[5]);

    if (headingRaw != static_cast<int16_t>(0x7FFF)) {
        d.headingDegrees = headingRaw * 0.0001 * RAD_TO_DEG;
        d.hasHeading     = true;
    }
    if (variationRaw != static_cast<int16_t>(0x7FFF)) {
        d.variationDeg = variationRaw * 0.0001 * RAD_TO_DEG;
    }
    d.valid = d.hasHeading;
    return d;
}

NMEA2000Data CANParser::decode127257(const CANFrame& f) {
    // Attitude — PGN 127257
    // Bytes: SID(1), Yaw[0..1](2), Pitch[2..3](2), Roll[4..5](2)
    NMEA2000Data d;
    d.pgn      = 127257;
    d.name     = "Attitude";
    d.priority = (f.id >> 26) & 0x07;
    d.source   = f.id & 0xFF;
    if (f.dlc < 7) return d;

    int16_t yaw   = le16s(&f.data[1]);
    int16_t pitch = le16s(&f.data[3]);
    int16_t roll  = le16s(&f.data[5]);

    d.yawDeg   = yaw   * 0.0001 * RAD_TO_DEG;
    d.pitchDeg = pitch * 0.0001 * RAD_TO_DEG;
    d.rollDeg  = roll  * 0.0001 * RAD_TO_DEG;
    d.hasAttitude = true;
    d.valid = true;
    return d;
}

NMEA2000Data CANParser::decode128259(const CANFrame& f) {
    // Speed Through Water — PGN 128259
    // Bytes: SID(1), Speed[0..1] in 0.01 m/s (2 bytes)
    NMEA2000Data d;
    d.pgn      = 128259;
    d.name     = "Speed Through Water";
    d.priority = (f.id >> 26) & 0x07;
    d.source   = f.id & 0xFF;
    if (f.dlc < 3) return d;

    uint16_t raw = le16u(&f.data[1]);
    if (raw != 0xFFFF) {
        d.stwKnots = (raw * 0.01) / 0.514444; // m/s to knots
        d.hasSTW   = true;
        d.valid    = true;
    }
    return d;
}

NMEA2000Data CANParser::decode128267(const CANFrame& f) {
    // Water Depth — PGN 128267
    // Bytes: SID(1), Depth[0..3] in 0.01 m (4 bytes), Offset[4..5] in 0.001 m (2 bytes)
    NMEA2000Data d;
    d.pgn      = 128267;
    d.name     = "Water Depth";
    d.priority = (f.id >> 26) & 0x07;
    d.source   = f.id & 0xFF;
    if (f.dlc < 6) return d;

    uint32_t depthRaw = le32u(&f.data[1]);
    if (depthRaw != 0xFFFFFFFF) {
        d.depthMetres = depthRaw * 0.01;
        d.hasDepth    = true;
        d.valid       = true;
    }
    int16_t offsetRaw = le16s(&f.data[5]);
    d.offsetMetres = offsetRaw * 0.001;
    return d;
}

NMEA2000Data CANParser::decode129025(const CANFrame& f) {
    // Position Rapid Update — PGN 129025
    // Bytes: Latitude[0..3] in 1e-7 deg, Longitude[4..7] in 1e-7 deg
    NMEA2000Data d;
    d.pgn      = 129025;
    d.name     = "Position Rapid Update";
    d.priority = (f.id >> 26) & 0x07;
    d.source   = f.id & 0xFF;
    if (f.dlc < 8) return d;

    int32_t latRaw = le32s(&f.data[0]);
    int32_t lonRaw = le32s(&f.data[4]);

    if (latRaw != static_cast<int32_t>(0x7FFFFFFF)) {
        d.latitude    = latRaw * 1e-7;
        d.longitude   = lonRaw * 1e-7;
        d.hasPosition = true;
        d.valid       = true;
    }
    return d;
}

NMEA2000Data CANParser::decode129026(const CANFrame& f) {
    // COG & SOG Rapid Update — PGN 129026
    // Bytes: SID(1), COG ref(1 nibble), COG[1..2] in 0.0001 rad, SOG[3..4] in 0.01 m/s
    NMEA2000Data d;
    d.pgn      = 129026;
    d.name     = "COG SOG Rapid Update";
    d.priority = (f.id >> 26) & 0x07;
    d.source   = f.id & 0xFF;
    if (f.dlc < 5) return d;

    // f.data[0] = SID
    // f.data[1] = COG reference (bits 0-1)
    // f.data[2..3] = COG in units of 0.0001 rad
    // f.data[4..5] = SOG in units of 0.01 m/s
    uint16_t cogRaw = le16u(&f.data[2]);
    uint16_t sogRaw = le16u(&f.data[4]);

    if (cogRaw != 0xFFFF) {
        d.cogDegrees = cogRaw * 0.0001 * RAD_TO_DEG;
        d.hasCOGSOG  = true;
    }
    if (sogRaw != 0xFFFF) {
        d.sogKnots  = (sogRaw * 0.01) / 0.514444;
        d.hasCOGSOG = true;
    }
    d.valid = d.hasCOGSOG;
    return d;
}

NMEA2000Data CANParser::decode130306(const CANFrame& f) {
    // Wind Data — PGN 130306
    // Bytes: SID(1), WindSpeed[0..1] in 0.01 m/s, WindAngle[2..3] in 0.0001 rad, Reference(1)
    NMEA2000Data d;
    d.pgn      = 130306;
    d.name     = "Wind Data";
    d.priority = (f.id >> 26) & 0x07;
    d.source   = f.id & 0xFF;
    if (f.dlc < 6) return d;

    uint16_t speedRaw = le16u(&f.data[1]);
    uint16_t angleRaw = le16u(&f.data[3]);
    uint8_t  ref      = f.data[5] & 0x07;

    if (speedRaw != 0xFFFF) {
        d.windSpeedKnots = (speedRaw * 0.01) / 0.514444;
        d.hasWind = true;
    }
    if (angleRaw != 0xFFFF) {
        d.windAngleDeg = angleRaw * 0.0001 * RAD_TO_DEG;
        d.hasWind = true;
    }
    // ref: 0=true wind relative to water, 2=apparent
    d.windApparent = (ref == 2);
    d.valid = d.hasWind;
    return d;
}

NMEA2000Data CANParser::decode130310(const CANFrame& f) {
    // Water Temperature — PGN 130310
    // Bytes: SID(1), WaterTemp[0..1] in 0.01 K, AtmosphericTemp[2..3] in 0.01 K, AtmPressure[4..5]
    NMEA2000Data d;
    d.pgn      = 130310;
    d.name     = "Water Temperature";
    d.priority = (f.id >> 26) & 0x07;
    d.source   = f.id & 0xFF;
    if (f.dlc < 3) return d;

    uint16_t tempRaw = le16u(&f.data[1]);
    if (tempRaw != 0xFFFF) {
        // Temperature in 0.01 K; subtract 273.15 K to get °C
        d.waterTempC   = (tempRaw * 0.01) - 273.15;
        d.hasWaterTemp = true;
        d.valid        = true;
    }
    return d;
}

} // namespace mcp
