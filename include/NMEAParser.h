#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mcp {

// Result of parsing one NMEA 0183 sentence.
// Only the fields relevant to the detected sentence type are populated;
// all bool hasXxx flags must be checked before using a field.
struct ParsedNMEA {
    std::string type;    // "GGA","RMC","VTG","GSV","MWV","DBT","DPT","HDG","HDT"
    bool        valid;   // checksum OK and data flagged valid (for RMC: A, not V)
    std::string raw;     // original sentence

    // ---- Position ----
    bool   hasPosition;
    double latitude;     // decimal degrees, negative = south
    double longitude;    // decimal degrees, negative = west

    // ---- Altitude ----
    bool   hasAltitude;
    double altitude;     // metres above MSL
    double geoidSep;     // geoid separation (metres)

    // ---- Speed / course ----
    bool   hasSpeed;
    double speedKnots;
    double speedKmh;

    bool   hasCourse;
    double courseTrue;      // degrees true
    double courseMagnetic;  // degrees magnetic

    // ---- Fix metadata ----
    bool hasFixInfo;
    int  fixQuality;   // 0=invalid,1=GPS,2=DGPS,6=estimated
    int  satellites;
    double hdop;

    // ---- Time (UTC) ----
    bool  hasTime;
    int   hour, minute;
    float second;
    bool  active;       // RMC A=active, V=void

    // ---- Wind (MWV) ----
    bool   hasWind;
    double windAngle;       // degrees
    double windSpeed;
    char   windSpeedUnit;   // K=km/h, M=m/s, N=knots
    bool   windRelative;    // true=relative, false=true

    // ---- Depth (DBT / DPT) ----
    bool   hasDepth;
    double depthMetres;

    // ---- Heading (HDG / HDT) ----
    bool   hasHeading;
    double headingDegrees;
    double variation;     // degrees, positive=east
    bool   variationWest; // true when variation is westerly (subtract)

    // ---- Satellites in view (GSV) ----
    struct SatInfo {
        int prn;
        int elevationDeg;
        int azimuthDeg;
        int snr;          // dB; -1 if not tracked
    };
    bool                totalSatsKnown;
    int                 totalSatellites;
    std::vector<SatInfo> satList;

    ParsedNMEA()
        : valid(false), hasPosition(false), latitude(0), longitude(0),
          hasAltitude(false), altitude(0), geoidSep(0),
          hasSpeed(false), speedKnots(0), speedKmh(0),
          hasCourse(false), courseTrue(0), courseMagnetic(0),
          hasFixInfo(false), fixQuality(0), satellites(0), hdop(0),
          hasTime(false), hour(0), minute(0), second(0), active(false),
          hasWind(false), windAngle(0), windSpeed(0),
          windSpeedUnit('N'), windRelative(true),
          hasDepth(false), depthMetres(0),
          hasHeading(false), headingDegrees(0), variation(0), variationWest(false),
          totalSatsKnown(false), totalSatellites(0) {}
};

// ---------------------------------------------------------------------------
// NMEAParser — stateless, all methods static
// ---------------------------------------------------------------------------
class NMEAParser {
public:
    // Parse a NMEA sentence.  Leading '$' and trailing "*HH\r\n" are handled
    // automatically.  Returns invalid ParsedNMEA on any error.
    static ParsedNMEA parse(const std::string& sentence);

    // Validate XOR checksum.  Returns true if the checksum is correct or
    // there is no checksum in the sentence.
    static bool validateChecksum(const std::string& sentence);

    // Split a sentence into comma-separated tokens (strips $ and *hh).
    static std::vector<std::string> split(const std::string& sentence);

    // Convert NMEA lat/lon format (DDMM.MMMM) + hemisphere ('N','S','E','W')
    // to signed decimal degrees.
    static double parseCoordinate(const std::string& value,
                                   const std::string& dir);

    // Parse "HHMMSS" or "HHMMSS.SS" → stores h/m/s in out params.
    static bool parseTime(const std::string& val,
                           int& h, int& m, float& s);

    // Parse a floating-point field safely; returns def on empty/error.
    static double parseDouble(const std::string& f, double def = 0.0);
    static int    parseInt  (const std::string& f, int    def = 0);

private:
    static ParsedNMEA parseGGA(const std::vector<std::string>& f);
    static ParsedNMEA parseRMC(const std::vector<std::string>& f);
    static ParsedNMEA parseVTG(const std::vector<std::string>& f);
    static ParsedNMEA parseGSV(const std::vector<std::string>& f);
    static ParsedNMEA parseMWV(const std::vector<std::string>& f);
    static ParsedNMEA parseDBT(const std::vector<std::string>& f);
    static ParsedNMEA parseDPT(const std::vector<std::string>& f);
    static ParsedNMEA parseHDG(const std::vector<std::string>& f);
    static ParsedNMEA parseHDT(const std::vector<std::string>& f);
};

} // namespace mcp
