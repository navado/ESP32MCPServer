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

    // -----------------------------------------------------------------------
    // Service 01 — show current powertrain data  (SAE J1979 / ISO 15031-5)
    // abcd[0..4] = bytes A..E from the CAN frame (up to 5 data bytes).
    // -----------------------------------------------------------------------
    if (service == 0x01) {
        switch (pid) {
        // ── Supported PID bitmasks ─────────────────────────────────────────
        case 0x00:
            d.name  = "Supported PIDs 01-20";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[0]) << 24) |
                (static_cast<uint32_t>(abcd[1]) << 16) |
                (static_cast<uint32_t>(abcd[2]) <<  8) |
                 static_cast<uint32_t>(abcd[3]));
            d.unit  = "bitmask";
            break;
        case 0x20:
            d.name  = "Supported PIDs 21-40";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[0]) << 24) |
                (static_cast<uint32_t>(abcd[1]) << 16) |
                (static_cast<uint32_t>(abcd[2]) <<  8) |
                 static_cast<uint32_t>(abcd[3]));
            d.unit  = "bitmask";
            break;
        case 0x40:
            d.name  = "Supported PIDs 41-60";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[0]) << 24) |
                (static_cast<uint32_t>(abcd[1]) << 16) |
                (static_cast<uint32_t>(abcd[2]) <<  8) |
                 static_cast<uint32_t>(abcd[3]));
            d.unit  = "bitmask";
            break;
        case 0x60:
            d.name  = "Supported PIDs 61-80";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[0]) << 24) |
                (static_cast<uint32_t>(abcd[1]) << 16) |
                (static_cast<uint32_t>(abcd[2]) <<  8) |
                 static_cast<uint32_t>(abcd[3]));
            d.unit  = "bitmask";
            break;
        case 0x80:
            d.name  = "Supported PIDs 81-A0";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[0]) << 24) |
                (static_cast<uint32_t>(abcd[1]) << 16) |
                (static_cast<uint32_t>(abcd[2]) <<  8) |
                 static_cast<uint32_t>(abcd[3]));
            d.unit  = "bitmask";
            break;
        case 0xA0:
            d.name  = "Supported PIDs A1-C0";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[0]) << 24) |
                (static_cast<uint32_t>(abcd[1]) << 16) |
                (static_cast<uint32_t>(abcd[2]) <<  8) |
                 static_cast<uint32_t>(abcd[3]));
            d.unit  = "bitmask";
            break;
        case 0xC0:
            d.name  = "Supported PIDs C1-E0";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[0]) << 24) |
                (static_cast<uint32_t>(abcd[1]) << 16) |
                (static_cast<uint32_t>(abcd[2]) <<  8) |
                 static_cast<uint32_t>(abcd[3]));
            d.unit  = "bitmask";
            break;

        // ── Monitor / status ───────────────────────────────────────────────
        case 0x01:
            d.name  = "Monitor Status";   // bit 7 of A = MIL on/off
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x41:
            d.name  = "Monitor Status Drive Cycle";
            d.value = abcd[0];
            d.unit  = "";
            break;

        // ── Fuel system ────────────────────────────────────────────────────
        case 0x03:
            d.name  = "Fuel System Status";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x06: // Short term fuel trim — bank 1  (A/1.28 - 100)%
            d.name  = "STFT Bank 1";
            d.value = (abcd[0] / 1.28) - 100.0;
            d.unit  = "%";
            break;
        case 0x07: // Long term fuel trim — bank 1
            d.name  = "LTFT Bank 1";
            d.value = (abcd[0] / 1.28) - 100.0;
            d.unit  = "%";
            break;
        case 0x08: // Short term fuel trim — bank 2
            d.name  = "STFT Bank 2";
            d.value = (abcd[0] / 1.28) - 100.0;
            d.unit  = "%";
            break;
        case 0x09: // Long term fuel trim — bank 2
            d.name  = "LTFT Bank 2";
            d.value = (abcd[0] / 1.28) - 100.0;
            d.unit  = "%";
            break;
        case 0x0A: // Fuel pressure (gauge, relative to atmospheric)  A*3 kPa
            d.name  = "Fuel Pressure";
            d.value = abcd[0] * 3.0;
            d.unit  = "kPa";
            break;
        case 0x22: // Fuel rail pressure relative to manifold vacuum  (AB)*0.079 kPa
            d.name  = "Fuel Rail Pressure";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) * 0.079;
            d.unit  = "kPa";
            break;
        case 0x23: // Fuel rail gauge pressure  (AB)*10 kPa
            d.name  = "Fuel Rail Gauge Pressure";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) * 10.0;
            d.unit  = "kPa";
            break;
        case 0x2F: // Fuel tank level  A/2.55 %
            d.name  = "Fuel Level";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x51:
            d.name  = "Fuel Type";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x52: // Ethanol fuel %  A/2.55
            d.name  = "Ethanol %";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x59: // Fuel rail absolute pressure  (AB)*10 kPa
            d.name  = "Fuel Rail Abs Pressure";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) * 10.0;
            d.unit  = "kPa";
            break;
        case 0x5E: // Engine fuel rate  (AB)*0.05 L/h
            d.name  = "Fuel Rate";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) * 0.05;
            d.unit  = "L/h";
            break;

        // ── Engine ─────────────────────────────────────────────────────────
        case 0x04: // Calculated engine load  A/2.55 %
            d.name  = "Engine Load";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x05: // Engine coolant temperature  A-40 °C
            d.name  = "Coolant Temp";
            d.value = static_cast<int>(abcd[0]) - 40;
            d.unit  = "\xc2\xb0""C";
            break;
        case 0x0B: // Intake manifold absolute pressure  A kPa
            d.name  = "MAP";
            d.value = abcd[0];
            d.unit  = "kPa";
            break;
        case 0x0C: // Engine RPM  (AB)/4 rpm
            d.name  = "Engine RPM";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 4.0;
            d.unit  = "rpm";
            break;
        case 0x0E: // Timing advance  A/2-64 °
            d.name  = "Timing Advance";
            d.value = (abcd[0] / 2.0) - 64.0;
            d.unit  = "\xc2\xb0";
            break;
        case 0x0F: // Intake air temperature  A-40 °C
            d.name  = "Intake Air Temp";
            d.value = static_cast<int>(abcd[0]) - 40;
            d.unit  = "\xc2\xb0""C";
            break;
        case 0x10: // Mass air flow rate  (AB)/100 g/s
            d.name  = "MAF Rate";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 100.0;
            d.unit  = "g/s";
            break;
        case 0x11: // Throttle position  A/2.55 %
            d.name  = "Throttle Position";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x43: // Absolute load  (AB)/2.55 %
            d.name  = "Absolute Load";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 2.55;
            d.unit  = "%";
            break;
        case 0x44: // Commanded air-fuel equivalence ratio  (AB)/32768
            d.name  = "Commanded Lambda";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 32768.0;
            d.unit  = "lambda";
            break;
        case 0x5C: // Engine oil temperature  A-40 °C
            d.name  = "Oil Temp";
            d.value = static_cast<int>(abcd[0]) - 40;
            d.unit  = "\xc2\xb0""C";
            break;
        case 0x5D: // Fuel injection timing  (AB)/128-210 °
            d.name  = "Injection Timing";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 128.0 - 210.0;
            d.unit  = "\xc2\xb0";
            break;

        // ── Torque (service 01 0x61-0x64) ─────────────────────────────────
        case 0x61: // Driver's demand engine % torque  A-125 %
            d.name  = "Driver Demand Torque";
            d.value = static_cast<int>(abcd[0]) - 125;
            d.unit  = "%";
            break;
        case 0x62: // Actual engine % torque  A-125 %
            d.name  = "Actual Engine Torque";
            d.value = static_cast<int>(abcd[0]) - 125;
            d.unit  = "%";
            break;
        case 0x63: // Engine reference torque  (AB) Nm
            d.name  = "Reference Torque";
            d.value = (static_cast<uint16_t>(abcd[0]) << 8) | abcd[1];
            d.unit  = "Nm";
            break;
        case 0x64: // Engine % torque data (5 points; A = idle)  A-125 %
            d.name  = "Engine Torque Data";
            d.value = static_cast<int>(abcd[0]) - 125;
            d.unit  = "%";
            break;

        // ── Vehicle dynamics ───────────────────────────────────────────────
        case 0x0D: // Vehicle speed  A km/h
            d.name  = "Vehicle Speed";
            d.value = abcd[0];
            d.unit  = "km/h";
            break;

        // ── Throttle / pedal positions ─────────────────────────────────────
        case 0x45: // Relative throttle position  A/2.55 %
            d.name  = "Relative Throttle";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x47: // Absolute throttle position B
            d.name  = "Throttle Position B";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x48: // Absolute throttle position C
            d.name  = "Throttle Position C";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x49: // Accelerator pedal position D
            d.name  = "Accel Pedal D";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x4A: // Accelerator pedal position E
            d.name  = "Accel Pedal E";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x4B: // Accelerator pedal position F
            d.name  = "Accel Pedal F";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x4C: // Commanded throttle actuator
            d.name  = "Commanded Throttle";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x5A: // Relative accelerator pedal position
            d.name  = "Rel Accel Pedal";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;

        // ── Temperatures ───────────────────────────────────────────────────
        case 0x46: // Ambient air temperature  A-40 °C
            d.name  = "Ambient Temp";
            d.value = static_cast<int>(abcd[0]) - 40;
            d.unit  = "\xc2\xb0""C";
            break;
        case 0x33: // Absolute barometric pressure  A kPa
            d.name  = "Barometric Pressure";
            d.value = abcd[0];
            d.unit  = "kPa";
            break;
        case 0x67: // Engine coolant temperature — two sensors (B,C are sensors 1,2)
            d.name   = "Coolant Temp 2";
            d.value  = static_cast<int>(abcd[1]) - 40;
            d.unit   = "\xc2\xb0""C";
            d.value2 = static_cast<int>(abcd[2]) - 40;
            d.unit2  = "\xc2\xb0""C S2";
            break;
        case 0x68: // Intake air temperature sensor (B = sensor 1)
            d.name  = "Intake Air Temp 2";
            d.value = static_cast<int>(abcd[1]) - 40;
            d.unit  = "\xc2\xb0""C";
            break;
        case 0x77: // Charge air cooler temperature (B = sensor 1)
            d.name  = "Charge Air Cooler Temp";
            d.value = static_cast<int>(abcd[1]) - 40;
            d.unit  = "\xc2\xb0""C";
            break;

        // ── Catalyst temperatures 0x3C-0x3F  (AB)/10-40 °C ───────────────
        case 0x3C:
            d.name  = "Catalyst Temp B1S1";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 10.0 - 40.0;
            d.unit  = "\xc2\xb0""C";
            break;
        case 0x3D:
            d.name  = "Catalyst Temp B2S1";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 10.0 - 40.0;
            d.unit  = "\xc2\xb0""C";
            break;
        case 0x3E:
            d.name  = "Catalyst Temp B1S2";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 10.0 - 40.0;
            d.unit  = "\xc2\xb0""C";
            break;
        case 0x3F:
            d.name  = "Catalyst Temp B2S2";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 10.0 - 40.0;
            d.unit  = "\xc2\xb0""C";
            break;

        // ── Exhaust gas temperature banks (B<<8|C)/10-40 for sensor 1,
        //    (D<<8|E)/10-40 for sensor 2 ─────────────────────────────────
        case 0x78: // EGT bank 1
            d.name   = "EGT Bank 1 S1";
            d.value  = ((static_cast<uint16_t>(abcd[1]) << 8) | abcd[2]) / 10.0 - 40.0;
            d.unit   = "\xc2\xb0""C";
            d.value2 = ((static_cast<uint16_t>(abcd[3]) << 8) | abcd[4]) / 10.0 - 40.0;
            d.unit2  = "\xc2\xb0""C S2";
            break;
        case 0x79: // EGT bank 2
            d.name   = "EGT Bank 2 S1";
            d.value  = ((static_cast<uint16_t>(abcd[1]) << 8) | abcd[2]) / 10.0 - 40.0;
            d.unit   = "\xc2\xb0""C";
            d.value2 = ((static_cast<uint16_t>(abcd[3]) << 8) | abcd[4]) / 10.0 - 40.0;
            d.unit2  = "\xc2\xb0""C S2";
            break;
        case 0x7C: // DPF temperature bank 1
            d.name  = "DPF Temp B1";
            d.value = ((static_cast<uint16_t>(abcd[1]) << 8) | abcd[2]) / 10.0 - 40.0;
            d.unit  = "\xc2\xb0""C";
            break;

        // ── O2 sensors 0x14-0x1B: voltage (A/200 V) + STFT (B/1.28-100%)
        //    0xFF in B means fuel trim not supported ─────────────────────
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B: {
            static const char* o2n[] = {
                "O2 S1B1","O2 S2B1","O2 S3B1","O2 S4B1",
                "O2 S1B2","O2 S2B2","O2 S3B2","O2 S4B2"
            };
            d.name  = o2n[pid - 0x14];
            d.value = abcd[0] / 200.0;
            d.unit  = "V";
            if (abcd[1] != 0xFF) {
                d.value2 = (abcd[1] / 1.28) - 100.0;
                d.unit2  = "% STFT";
            }
            break;
        }
        // ── O2 sensors 0x24-0x2B: equivalence ratio (2/65536*(AB)) + voltage (8/65536*(CD))
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x2A:
        case 0x2B: {
            static const char* o2n[] = {
                "O2 Lambda S1B1","O2 Lambda S2B1","O2 Lambda S3B1","O2 Lambda S4B1",
                "O2 Lambda S1B2","O2 Lambda S2B2","O2 Lambda S3B2","O2 Lambda S4B2"
            };
            d.name   = o2n[pid - 0x24];
            d.value  = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) * (2.0 / 65536.0);
            d.unit   = "lambda";
            d.value2 = ((static_cast<uint16_t>(abcd[2]) << 8) | abcd[3]) * (8.0 / 65536.0);
            d.unit2  = "V";
            break;
        }
        // ── O2 sensors 0x34-0x3B: equivalence ratio + current (CD/256-128 mA)
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
        case 0x3A:
        case 0x3B: {
            static const char* o2n[] = {
                "O2 mA S1B1","O2 mA S2B1","O2 mA S3B1","O2 mA S4B1",
                "O2 mA S1B2","O2 mA S2B2","O2 mA S3B2","O2 mA S4B2"
            };
            d.name   = o2n[pid - 0x34];
            d.value  = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) * (2.0 / 65536.0);
            d.unit   = "lambda";
            d.value2 = static_cast<int16_t>(
                           (static_cast<uint16_t>(abcd[2]) << 8) | abcd[3]) / 256.0;
            d.unit2  = "mA";
            break;
        }

        // ── Emissions / evap ───────────────────────────────────────────────
        case 0x12: // Commanded secondary air status
            d.name  = "Secondary Air Status";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x13: // O2 sensors present (2-bank encoding)
            d.name  = "O2 Sensors Present";
            d.value = abcd[0];
            d.unit  = "bitmask";
            break;
        case 0x1C: // OBD standards compliance
            d.name  = "OBD Standard";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x1D: // O2 sensors present (4-bank encoding)
            d.name  = "O2 Sensors Present (4-bank)";
            d.value = abcd[0];
            d.unit  = "bitmask";
            break;
        case 0x1E: // Auxiliary input status (bit 0 = PTO active)
            d.name  = "Aux Input Status";
            d.value = abcd[0] & 0x01;
            d.unit  = "";
            break;
        case 0x2C: // Commanded EGR  A/2.55 %
            d.name  = "Commanded EGR";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x2D: // EGR error  A/1.28-100 %
            d.name  = "EGR Error";
            d.value = (abcd[0] / 1.28) - 100.0;
            d.unit  = "%";
            break;
        case 0x2E: // Commanded evaporative purge  A/2.55 %
            d.name  = "Evaporative Purge";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x32: // Evap system vapor pressure (signed)  (AB signed)/4 Pa
            d.name  = "Evap Vapor Pressure";
            d.value = static_cast<int16_t>(
                          (static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 4.0;
            d.unit  = "Pa";
            break;
        case 0x53: // Absolute evap system vapor pressure  (AB)/200 kPa
            d.name  = "Abs Evap Vapor Pressure";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 200.0;
            d.unit  = "kPa";
            break;
        case 0x54: // Evap vapor pressure 2 (signed)  (AB signed)-32767 Pa
            d.name  = "Evap Vapor Pressure 2";
            d.value = static_cast<int16_t>(
                          (static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) - 32767.0;
            d.unit  = "Pa";
            break;
        case 0x5F: // Emission requirements to which the vehicle is designed
            d.name  = "Emission Requirements";
            d.value = abcd[0];
            d.unit  = "";
            break;

        // ── Secondary O2 fuel trims 0x55-0x58: A=bank1or3, B=bank2or4 ───
        case 0x55:
            d.name   = "STFT B1&3";
            d.value  = (abcd[0] / 1.28) - 100.0;
            d.unit   = "% B1";
            d.value2 = (abcd[1] / 1.28) - 100.0;
            d.unit2  = "% B3";
            break;
        case 0x56:
            d.name   = "LTFT B1&3";
            d.value  = (abcd[0] / 1.28) - 100.0;
            d.unit   = "% B1";
            d.value2 = (abcd[1] / 1.28) - 100.0;
            d.unit2  = "% B3";
            break;
        case 0x57:
            d.name   = "STFT B2&4";
            d.value  = (abcd[0] / 1.28) - 100.0;
            d.unit   = "% B2";
            d.value2 = (abcd[1] / 1.28) - 100.0;
            d.unit2  = "% B4";
            break;
        case 0x58:
            d.name   = "LTFT B2&4";
            d.value  = (abcd[0] / 1.28) - 100.0;
            d.unit   = "% B2";
            d.value2 = (abcd[1] / 1.28) - 100.0;
            d.unit2  = "% B4";
            break;

        // ── DPF / diesel ───────────────────────────────────────────────────
        case 0x7A: // DPF differential pressure  (AB)*0.01 kPa
            d.name  = "DPF Diff Pressure";
            d.value = ((static_cast<uint16_t>(abcd[1]) << 8) | abcd[2]) * 0.01;
            d.unit  = "kPa";
            break;

        // ── Boost pressure (B<<8|C)*0.03125 kPa (with bitmask in A) ──────
        case 0x70:
            d.name  = "Boost Pressure";
            d.value = ((static_cast<uint16_t>(abcd[1]) << 8) | abcd[2]) * 0.03125;
            d.unit  = "kPa";
            break;

        // ── Miscellaneous ──────────────────────────────────────────────────
        case 0x1F: // Run time since engine start  (AB) s
            d.name  = "Engine Run Time";
            d.value = (static_cast<uint16_t>(abcd[0]) << 8) | abcd[1];
            d.unit  = "s";
            break;
        case 0x21: // Distance traveled with MIL on  (AB) km
            d.name  = "MIL Distance";
            d.value = (static_cast<uint16_t>(abcd[0]) << 8) | abcd[1];
            d.unit  = "km";
            break;
        case 0x30: // Warm-ups since codes cleared  A count
            d.name  = "Warm-ups Since Clear";
            d.value = abcd[0];
            d.unit  = "count";
            break;
        case 0x31: // Distance since codes cleared  (AB) km
            d.name  = "Distance Since Clear";
            d.value = (static_cast<uint16_t>(abcd[0]) << 8) | abcd[1];
            d.unit  = "km";
            break;
        case 0x42: // Control module voltage  (AB)/1000 V
            d.name  = "Module Voltage";
            d.value = ((static_cast<uint16_t>(abcd[0]) << 8) | abcd[1]) / 1000.0;
            d.unit  = "V";
            break;
        case 0x4D: // Time run with MIL on  (AB) min
            d.name  = "MIL Run Time";
            d.value = (static_cast<uint16_t>(abcd[0]) << 8) | abcd[1];
            d.unit  = "min";
            break;
        case 0x4E: // Time since trouble codes cleared  (AB) min
            d.name  = "Time Since Clear";
            d.value = (static_cast<uint16_t>(abcd[0]) << 8) | abcd[1];
            d.unit  = "min";
            break;
        case 0x4F: // Maximum values (lambda, O2 voltage, O2 current, MAP)
            d.name  = "Max Values";
            d.value = abcd[0]; // max equivalence ratio * 1
            d.unit  = "";
            break;
        case 0x50: // Maximum air flow rate  A*10 g/s
            d.name  = "Max MAF Rate";
            d.value = abcd[0] * 10.0;
            d.unit  = "g/s";
            break;
        case 0x5B: // Hybrid battery pack remaining life  A/2.55 %
            d.name  = "Hybrid Battery";
            d.value = abcd[0] / 2.55;
            d.unit  = "%";
            break;
        case 0x6B: // EGR temperature sensor (B = sensor 1)  B-40 °C
            d.name  = "EGR Temp";
            d.value = static_cast<int>(abcd[1]) - 40;
            d.unit  = "\xc2\xb0""C";
            break;
        case 0x7F: // Engine run time for AECD  (BCDE) s
            d.name  = "Engine Run Time AECD";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[1]) << 24) |
                (static_cast<uint32_t>(abcd[2]) << 16) |
                (static_cast<uint32_t>(abcd[3]) <<  8) |
                 static_cast<uint32_t>(abcd[4]));
            d.unit  = "s";
            break;

        default: {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02X", pid);
            d.name  = std::string("PID 0x") + buf;
            d.value = abcd[0];
            d.unit  = "";
            break;
        }
        } // switch(pid) service 01

    // -----------------------------------------------------------------------
    // Service 09 — vehicle information
    // Most infotypes (VIN, Cal ID, ECU name) require ISO 15765-2 multi-frame
    // reassembly.  We decode only the single-frame infotypes here.
    // -----------------------------------------------------------------------
    } else if (service == 0x09) {
        switch (pid) {
        case 0x00: // Supported infotypes bitmask
            d.name  = "Supported Infotypes";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[0]) << 24) |
                (static_cast<uint32_t>(abcd[1]) << 16) |
                (static_cast<uint32_t>(abcd[2]) <<  8) |
                 static_cast<uint32_t>(abcd[3]));
            d.unit  = "bitmask";
            break;
        case 0x01: // VIN message count
            d.name  = "VIN Msg Count";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x03: // Calibration ID message count
            d.name  = "Cal ID Msg Count";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x05: // CVN message count
            d.name  = "CVN Msg Count";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x06: // Calibration verification number (32-bit, single frame)
            d.name  = "CVN";
            d.value = static_cast<double>(
                (static_cast<uint32_t>(abcd[0]) << 24) |
                (static_cast<uint32_t>(abcd[1]) << 16) |
                (static_cast<uint32_t>(abcd[2]) <<  8) |
                 static_cast<uint32_t>(abcd[3]));
            d.unit  = "";
            break;
        case 0x07: // In-use performance tracking message count
            d.name  = "IUPT Msg Count";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x09: // ECU name message count
            d.name  = "ECU Name Msg Count";
            d.value = abcd[0];
            d.unit  = "";
            break;
        case 0x02: // VIN (multi-frame)
        case 0x04: // Calibration ID (multi-frame)
        case 0x08: // In-use performance tracking (multi-frame)
        case 0x0A: // ECU name (multi-frame)
            d.name  = (pid == 0x02) ? "VIN" :
                      (pid == 0x04) ? "Calibration ID" :
                      (pid == 0x08) ? "IUPT Data" : "ECU Name";
            d.value = 0;
            d.unit  = "multi-frame";
            break;
        default: {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02X", pid);
            d.name  = std::string("Infotype 0x") + buf;
            d.value = abcd[0];
            d.unit  = "";
            break;
        }
        } // switch(pid) service 09

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
