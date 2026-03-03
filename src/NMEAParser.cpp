#include "NMEAParser.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace mcp {

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

bool NMEAParser::validateChecksum(const std::string& sentence) {
    // Find '*'
    auto star = sentence.rfind('*');
    if (star == std::string::npos) return true; // no checksum present — accept

    // Compute XOR over characters between '$' (exclusive) and '*' (exclusive)
    size_t start = 0;
    if (!sentence.empty() && sentence[0] == '$') start = 1;

    uint8_t xorVal = 0;
    for (size_t i = start; i < star; ++i) {
        xorVal ^= static_cast<uint8_t>(sentence[i]);
    }

    // Parse provided checksum (two hex digits after '*')
    if (star + 2 >= sentence.size()) return false;
    char hi = std::toupper(sentence[star + 1]);
    char lo = std::toupper(sentence[star + 2]);
    auto hexDigit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int h = hexDigit(hi), l = hexDigit(lo);
    if (h < 0 || l < 0) return false;

    uint8_t provided = static_cast<uint8_t>((h << 4) | l);
    return xorVal == provided;
}

std::vector<std::string> NMEAParser::split(const std::string& sentence) {
    // Strip leading '$' and everything from '*' onward
    std::string s = sentence;
    if (!s.empty() && s[0] == '$') s = s.substr(1);
    auto star = s.find('*');
    if (star != std::string::npos) s = s.substr(0, star);

    // Split on ','
    std::vector<std::string> fields;
    size_t pos = 0;
    while (true) {
        auto comma = s.find(',', pos);
        if (comma == std::string::npos) {
            fields.push_back(s.substr(pos));
            break;
        }
        fields.push_back(s.substr(pos, comma - pos));
        pos = comma + 1;
    }
    return fields;
}

double NMEAParser::parseDouble(const std::string& f, double def) {
    if (f.empty()) return def;
    try {
        return std::stod(f);
    } catch (...) {
        return def;
    }
}

int NMEAParser::parseInt(const std::string& f, int def) {
    if (f.empty()) return def;
    try {
        return std::stoi(f);
    } catch (...) {
        return def;
    }
}

bool NMEAParser::parseTime(const std::string& val,
                            int& h, int& m, float& s) {
    if (val.size() < 6) return false;
    h = parseInt(val.substr(0, 2));
    m = parseInt(val.substr(2, 2));
    s = static_cast<float>(parseDouble(val.substr(4)));
    return true;
}

double NMEAParser::parseCoordinate(const std::string& value,
                                    const std::string& dir) {
    if (value.empty()) return 0.0;
    // Format: DDDMM.MMMM (longitude) or DDMM.MMMM (latitude)
    auto dot = value.find('.');
    if (dot == std::string::npos || dot < 2) return 0.0;

    // Degrees = everything up to dot-2; minutes = last 2 before dot + decimal
    size_t degLen = dot - 2;
    double degrees = parseDouble(value.substr(0, degLen));
    double minutes  = parseDouble(value.substr(degLen));
    double result   = degrees + minutes / 60.0;

    if (!dir.empty() && (dir[0] == 'S' || dir[0] == 'W')) result = -result;
    return result;
}

// ---------------------------------------------------------------------------
// Type-specific parsers
// ---------------------------------------------------------------------------

// GGA — Global Positioning System Fix Data
// $GPGGA,HHMMSS,LLLL.LL,a,YYYYY.YY,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
ParsedNMEA NMEAParser::parseGGA(const std::vector<std::string>& f) {
    ParsedNMEA p;
    p.type = "GGA";
    if (f.size() < 15) return p;

    // Time
    if (!f[1].empty()) {
        p.hasTime = parseTime(f[1], p.hour, p.minute, p.second);
    }

    // Position
    if (!f[2].empty() && !f[4].empty()) {
        p.latitude  = parseCoordinate(f[2], f[3]);
        p.longitude = parseCoordinate(f[4], f[5]);
        p.hasPosition = true;
    }

    // Fix quality
    p.fixQuality = parseInt(f[6]);
    p.satellites = parseInt(f[7]);
    p.hdop       = parseDouble(f[8]);
    p.hasFixInfo  = true;

    // Altitude
    if (!f[9].empty()) {
        p.altitude   = parseDouble(f[9]);
        p.geoidSep   = parseDouble(f[11]);
        p.hasAltitude = true;
    }

    p.valid = (p.fixQuality > 0);
    return p;
}

// RMC — Recommended Minimum Navigation Information
// $GPRMC,HHMMSS,A,LLLL.LL,a,YYYYY.YY,a,x.x,x.x,DDMMYY,x.x,a*hh
ParsedNMEA NMEAParser::parseRMC(const std::vector<std::string>& f) {
    ParsedNMEA p;
    p.type = "RMC";
    if (f.size() < 10) return p;

    if (!f[1].empty()) p.hasTime = parseTime(f[1], p.hour, p.minute, p.second);

    p.active = (!f[2].empty() && f[2][0] == 'A');

    if (!f[3].empty() && !f[5].empty()) {
        p.latitude    = parseCoordinate(f[3], f[4]);
        p.longitude   = parseCoordinate(f[5], f[6]);
        p.hasPosition = true;
    }

    if (!f[7].empty()) {
        p.speedKnots = parseDouble(f[7]);
        p.speedKmh   = p.speedKnots * 1.852;
        p.hasSpeed   = true;
    }

    if (!f[8].empty()) {
        p.courseTrue = parseDouble(f[8]);
        p.hasCourse  = true;
    }

    // Magnetic variation (field 10 + 11 if present)
    if (f.size() > 11 && !f[10].empty()) {
        p.courseMagnetic = parseDouble(f[10]);
        if (!f[11].empty() && f[11][0] == 'W') p.courseMagnetic = -p.courseMagnetic;
    }

    p.valid = p.active;
    return p;
}

// VTG — Track Made Good and Ground Speed
// $GPVTG,x.x,T,x.x,M,x.x,N,x.x,K,*hh
ParsedNMEA NMEAParser::parseVTG(const std::vector<std::string>& f) {
    ParsedNMEA p;
    p.type = "VTG";
    if (f.size() < 8) return p;

    if (!f[1].empty()) {
        p.courseTrue  = parseDouble(f[1]);
        p.hasCourse   = true;
    }
    if (!f[3].empty()) {
        p.courseMagnetic = parseDouble(f[3]);
    }
    if (!f[5].empty()) {
        p.speedKnots  = parseDouble(f[5]);
        p.hasSpeed    = true;
    }
    if (!f[7].empty()) {
        p.speedKmh    = parseDouble(f[7]);
    }

    p.valid = true;
    return p;
}

// GSV — Satellites in View
// $GPGSV,x,x,xx,xx,xx,xxx,xx,...*hh  (4 satellites per sentence, up to 4)
ParsedNMEA NMEAParser::parseGSV(const std::vector<std::string>& f) {
    ParsedNMEA p;
    p.type = "GSV";
    if (f.size() < 4) return p;

    p.totalSatellites = parseInt(f[3]);
    p.totalSatsKnown  = true;

    // Each group of 4 fields: PRN, elevation, azimuth, SNR
    for (size_t i = 4; i + 3 < f.size(); i += 4) {
        ParsedNMEA::SatInfo si;
        si.prn          = parseInt(f[i]);
        si.elevationDeg = parseInt(f[i + 1]);
        si.azimuthDeg   = parseInt(f[i + 2]);
        si.snr          = f[i + 3].empty() ? -1 : parseInt(f[i + 3]);
        p.satList.push_back(si);
    }

    p.valid = true;
    return p;
}

// MWV — Wind Speed and Angle
// $IIMWV,x.x,R,x.x,N,A*hh   (R=relative, T=true; N=knots)
ParsedNMEA NMEAParser::parseMWV(const std::vector<std::string>& f) {
    ParsedNMEA p;
    p.type = "MWV";
    if (f.size() < 6) return p;

    p.windAngle       = parseDouble(f[1]);
    p.windRelative    = (!f[2].empty() && f[2][0] == 'R');
    p.windSpeed       = parseDouble(f[3]);
    p.windSpeedUnit   = f[4].empty() ? 'N' : f[4][0];
    p.hasWind         = true;
    p.valid           = (!f[5].empty() && f[5][0] == 'A');
    return p;
}

// DBT — Depth Below Transducer
// $SDDBT,x.x,f,x.x,M,x.x,F*hh
ParsedNMEA NMEAParser::parseDBT(const std::vector<std::string>& f) {
    ParsedNMEA p;
    p.type = "DBT";
    if (f.size() < 7) return p;

    // Field 3 = metres
    if (!f[3].empty()) {
        p.depthMetres = parseDouble(f[3]);
        p.hasDepth    = true;
    }
    p.valid = p.hasDepth;
    return p;
}

// DPT — Depth of Water
// $SDDPT,x.x,x.x*hh
ParsedNMEA NMEAParser::parseDPT(const std::vector<std::string>& f) {
    ParsedNMEA p;
    p.type = "DPT";
    if (f.size() < 2) return p;

    p.depthMetres = parseDouble(f[1]);
    p.hasDepth    = true;
    p.valid       = true;
    return p;
}

// HDG — Heading - Deviation & Variation
// $HCHDG,x.x,x.x,a,x.x,a*hh
ParsedNMEA NMEAParser::parseHDG(const std::vector<std::string>& f) {
    ParsedNMEA p;
    p.type = "HDG";
    if (f.size() < 5) return p;

    p.headingDegrees = parseDouble(f[1]);
    p.hasHeading     = true;

    // Variation
    if (!f[4].empty()) {
        p.variation     = parseDouble(f[4]);
        p.variationWest = (!f[5].empty() && f[5][0] == 'W');
    }
    p.valid = true;
    return p;
}

// HDT — Heading - True
// $HEHDT,x.x,T*hh
ParsedNMEA NMEAParser::parseHDT(const std::vector<std::string>& f) {
    ParsedNMEA p;
    p.type = "HDT";
    if (f.size() < 2) return p;

    p.headingDegrees = parseDouble(f[1]);
    p.hasHeading     = true;
    p.valid          = true;
    return p;
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

ParsedNMEA NMEAParser::parse(const std::string& sentence) {
    ParsedNMEA result;
    result.raw = sentence;

    if (!validateChecksum(sentence)) {
        return result; // valid stays false
    }

    auto fields = split(sentence);
    if (fields.empty()) return result;

    // The first field is the talker+sentence ID, e.g. "GPGGA", "HEHDT"
    // The sentence type is the last 3 characters.
    const std::string& id = fields[0];
    std::string type;
    if (id.size() >= 3) {
        type = id.substr(id.size() - 3); // last 3 chars
    } else {
        type = id;
    }
    // Convert to upper case
    for (char& c : type) c = static_cast<char>(std::toupper(c));

    if      (type == "GGA") result = parseGGA(fields);
    else if (type == "RMC") result = parseRMC(fields);
    else if (type == "VTG") result = parseVTG(fields);
    else if (type == "GSV") result = parseGSV(fields);
    else if (type == "MWV") result = parseMWV(fields);
    else if (type == "DBT") result = parseDBT(fields);
    else if (type == "DPT") result = parseDPT(fields);
    else if (type == "HDG") result = parseHDG(fields);
    else if (type == "HDT") result = parseHDT(fields);
    else {
        result.type  = type;
        result.valid = false;
    }

    result.raw = sentence;
    return result;
}

} // namespace mcp
