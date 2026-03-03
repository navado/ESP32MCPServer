#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mcp {

// ---------------------------------------------------------------------------
// Raw CAN frame
// ---------------------------------------------------------------------------
struct CANFrame {
    uint32_t id;
    bool     extended; // 29-bit extended ID
    bool     rtr;
    uint8_t  dlc;
    uint8_t  data[8];

    CANFrame() : id(0), extended(false), rtr(false), dlc(0) {
        for (int i = 0; i < 8; i++) data[i] = 0;
    }

    // Human-readable hex dump, e.g. "7E8#0441050300000000"
    std::string toString() const;
};

// ---------------------------------------------------------------------------
// OBD-II (ISO 15765-4 / SAE J1979)
// ---------------------------------------------------------------------------
struct OBDIIData {
    uint8_t     service; // 0x01 = current data
    uint8_t     pid;
    std::string name;
    double      value;
    std::string unit;
    bool        valid;

    OBDIIData() : service(0), pid(0), value(0), valid(false) {}
};

// ---------------------------------------------------------------------------
// NMEA 2000 / J1939 parsed PGN
// ---------------------------------------------------------------------------
struct NMEA2000Data {
    uint32_t pgn;
    uint8_t  priority;
    uint8_t  source;
    std::string name;
    bool     valid;

    // Position (PGN 129025 rapid update, 129029 full)
    bool   hasPosition;
    double latitude;   // decimal degrees
    double longitude;

    // COG / SOG (PGN 129026)
    bool   hasCOGSOG;
    double cogDegrees; // course over ground, true
    double sogKnots;   // speed over ground

    // Vessel heading (PGN 127250)
    bool   hasHeading;
    double headingDegrees;
    double variationDeg; // positive = east

    // Attitude — pitch, roll, yaw (PGN 127257)
    bool   hasAttitude;
    double yawDeg, pitchDeg, rollDeg;

    // Speed through water (PGN 128259)
    bool   hasSTW;
    double stwKnots;

    // Water depth (PGN 128267)
    bool   hasDepth;
    double depthMetres;
    double offsetMetres;

    // Wind (PGN 130306)
    bool   hasWind;
    double windSpeedKnots;
    double windAngleDeg;
    bool   windApparent; // true=apparent, false=true wind

    // Water temperature (PGN 130310)
    bool   hasWaterTemp;
    double waterTempC;

    NMEA2000Data()
        : pgn(0), priority(0), source(0), valid(false),
          hasPosition(false), latitude(0), longitude(0),
          hasCOGSOG(false), cogDegrees(0), sogKnots(0),
          hasHeading(false), headingDegrees(0), variationDeg(0),
          hasAttitude(false), yawDeg(0), pitchDeg(0), rollDeg(0),
          hasSTW(false), stwKnots(0),
          hasDepth(false), depthMetres(0), offsetMetres(0),
          hasWind(false), windSpeedKnots(0), windAngleDeg(0), windApparent(true),
          hasWaterTemp(false), waterTempC(0) {}
};

// ---------------------------------------------------------------------------
// CANParser — stateless, all methods static
// ---------------------------------------------------------------------------
class CANParser {
public:
    // --- OBD-II ---
    // True when the frame looks like an OBD-II response (11-bit 0x7E8..0x7EF)
    static bool isOBDIIResponse(const CANFrame& frame);
    static OBDIIData parseOBDII(const CANFrame& frame);

    // Decode a single OBD-II PID from its raw A/B/C/D bytes
    static OBDIIData decodeOBDPID(uint8_t service, uint8_t pid,
                                   const uint8_t* abcd);

    // --- NMEA 2000 ---
    // True when the frame is a 29-bit extended frame with a known PGN
    static bool isNMEA2000(const CANFrame& frame);
    static NMEA2000Data parseNMEA2000(const CANFrame& frame);

    // Extract the 18-bit PGN from a 29-bit CAN ID
    static uint32_t extractPGN(uint32_t canId);

    // --- Utilities ---
    static std::string frameToHex(const CANFrame& frame);

private:
    static NMEA2000Data decode127250(const CANFrame& f); // Vessel Heading
    static NMEA2000Data decode127257(const CANFrame& f); // Attitude
    static NMEA2000Data decode128259(const CANFrame& f); // Speed through Water
    static NMEA2000Data decode128267(const CANFrame& f); // Water Depth
    static NMEA2000Data decode129025(const CANFrame& f); // Position Rapid Update
    static NMEA2000Data decode129026(const CANFrame& f); // COG & SOG
    static NMEA2000Data decode130306(const CANFrame& f); // Wind Data
    static NMEA2000Data decode130310(const CANFrame& f); // Water Temperature

    // Helper: extract little-endian uint16 from byte array
    static uint16_t le16u(const uint8_t* b) {
        return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
    }
    static int16_t le16s(const uint8_t* b) {
        return static_cast<int16_t>(le16u(b));
    }
    static uint32_t le32u(const uint8_t* b) {
        return static_cast<uint32_t>(b[0]) |
               (static_cast<uint32_t>(b[1]) << 8) |
               (static_cast<uint32_t>(b[2]) << 16) |
               (static_cast<uint32_t>(b[3]) << 24);
    }
    static int32_t le32s(const uint8_t* b) {
        return static_cast<int32_t>(le32u(b));
    }
};

} // namespace mcp
