// test_can_parser.cpp
// Unity tests for CANParser (pure C++, runs natively)

#include <unity.h>
#include "CANParser.h"
#include <cstring>
#include <cmath>

using namespace mcp;

void setUp()    {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Helper: build a standard 11-bit OBD-II response frame
// ---------------------------------------------------------------------------
static CANFrame makeOBDFrame(uint8_t pid, uint8_t A, uint8_t B = 0,
                              uint8_t C = 0, uint8_t D = 0) {
    CANFrame f;
    f.id       = 0x7E8;
    f.extended = false;
    f.rtr      = false;
    f.dlc      = 8;
    f.data[0]  = 4;    // length
    f.data[1]  = 0x41; // service 01 response
    f.data[2]  = pid;
    f.data[3]  = A;
    f.data[4]  = B;
    f.data[5]  = C;
    f.data[6]  = D;
    return f;
}

// Helper: build a 29-bit extended CAN frame
static CANFrame makeExtFrame(uint32_t canId, const uint8_t* data, uint8_t dlc) {
    CANFrame f;
    f.id       = canId;
    f.extended = true;
    f.rtr      = false;
    f.dlc      = dlc;
    std::memset(f.data, 0, 8);
    for (int i = 0; i < dlc && i < 8; ++i) f.data[i] = data[i];
    return f;
}

// ---------------------------------------------------------------------------
// CANFrame::toString
// ---------------------------------------------------------------------------

void test_frame_to_string_11bit() {
    CANFrame f = makeOBDFrame(0x0C, 0x0F, 0xA0);
    std::string s = f.toString();
    // "7E8#0441 0C0FA000000000"
    TEST_ASSERT_EQUAL_STRING("7E8#04410C0FA0000000", s.c_str());
}

void test_frame_to_string_29bit() {
    CANFrame f;
    f.id = 0x09F8010A; f.extended = true; f.dlc = 2;
    f.data[0] = 0xAB; f.data[1] = 0xCD;
    std::string s = f.toString();
    // "09F8010A#ABCD"
    TEST_ASSERT_EQUAL_STRING("09F8010A#ABCD", s.c_str());
}

// ---------------------------------------------------------------------------
// extractPGN
// ---------------------------------------------------------------------------

void test_extract_pgn_pdu2() {
    // PGN 129025 (0x1F801) — DP=1, PF=0xF8, PS=0x01
    // canId = priority(3) | R(1) | DP(1) | PF(8) | PS(8) | SA(8)
    // For PGN 129025: DP=1,PF=0xF8,PS=0x01
    // canId = (priority<<26)|(0<<25)|(1<<24)|(0xF8<<8)|0x01|SA
    // Let's build it: priority=6, SA=0x0A
    // = (6<<26)|(1<<24)|(0xF8<<16)|(0x01<<8)|0x0A
    // = 0x19F8010A
    uint32_t pgn = CANParser::extractPGN(0x19F8010A);
    TEST_ASSERT_EQUAL(129025, (int)pgn);
}

void test_extract_pgn_pdu1() {
    // PGN 127250 (0x1F112) — DP=1, PF=0xF1, PS=destination (PDU1: PF < 240 = 0xF0)
    // 0xF1=241 which is >=240, so actually PDU2. Let me recalculate.
    // PGN 127250 = 0x1F112 — let's decode:
    // 0x1F112 = 0001 1111 0001 0001 0010
    // DP=1, PF=0xF1 (241), PS=0x12
    // PF=241 >= 240: PDU2 → PGN = (DP<<16)|(PF<<8)|PS = (1<<16)|(0xF1<<8)|0x12 = 0x1F112 = 127250
    // canId: (priority<<26)|(R<<25)|(DP<<24)|(PF<<16)|(PS<<8)|SA
    // = (6<<26)|(1<<24)|(0xF1<<16)|(0x12<<8)|0x05
    // = 0x19F11205
    uint32_t pgn = CANParser::extractPGN(0x19F11205);
    TEST_ASSERT_EQUAL(127250, (int)pgn);
}

// ---------------------------------------------------------------------------
// OBD-II identification and parsing
// ---------------------------------------------------------------------------

void test_obd_is_response() {
    CANFrame f = makeOBDFrame(0x0C, 0x0F, 0xA0);
    TEST_ASSERT_TRUE(CANParser::isOBDIIResponse(f));
}

void test_obd_not_response_extended() {
    CANFrame f = makeOBDFrame(0x0C, 0x0F);
    f.extended = true;
    TEST_ASSERT_FALSE(CANParser::isOBDIIResponse(f));
}

void test_obd_not_response_wrong_id() {
    CANFrame f = makeOBDFrame(0x0C, 0x0F);
    f.id = 0x7DF; // request, not response
    TEST_ASSERT_FALSE(CANParser::isOBDIIResponse(f));
}

void test_obd_parse_rpm() {
    // RPM = ((A<<8)|B) / 4.0; A=0x0F, B=0xA0 → (0x0FA0)/4 = (4000)/4 = 1000 rpm
    CANFrame f = makeOBDFrame(0x0C, 0x0F, 0xA0);
    OBDIIData d = CANParser::parseOBDII(f);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_EQUAL(0x01, d.service);
    TEST_ASSERT_EQUAL(0x0C, d.pid);
    TEST_ASSERT_FLOAT_WITHIN(1.0, 1000.0, d.value);
    TEST_ASSERT_EQUAL_STRING("rpm", d.unit.c_str());
}

void test_obd_parse_speed() {
    // Speed PID 0x0D: A = km/h directly
    CANFrame f = makeOBDFrame(0x0D, 80);
    OBDIIData d = CANParser::parseOBDII(f);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.5, 80.0, d.value);
    TEST_ASSERT_EQUAL_STRING("km/h", d.unit.c_str());
}

void test_obd_parse_coolant_temp() {
    // Coolant temp PID 0x05: value = A - 40
    CANFrame f = makeOBDFrame(0x05, 100); // 100 - 40 = 60°C
    OBDIIData d = CANParser::parseOBDII(f);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.5, 60.0, d.value);
    TEST_ASSERT_EQUAL_STRING("°C", d.unit.c_str());
}

void test_obd_parse_engine_load() {
    // Load PID 0x04: value = A / 2.55 → 255/2.55 = 100%
    CANFrame f = makeOBDFrame(0x04, 255);
    OBDIIData d = CANParser::parseOBDII(f);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.5, 100.0, d.value);
    TEST_ASSERT_EQUAL_STRING("%", d.unit.c_str());
}

void test_obd_parse_throttle() {
    CANFrame f = makeOBDFrame(0x11, 128); // 128/2.55 ≈ 50.2%
    OBDIIData d = CANParser::parseOBDII(f);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0, 50.0, d.value);
}

// ---------------------------------------------------------------------------
// NMEA 2000 identification and parsing
// ---------------------------------------------------------------------------

void test_nmea2000_is_identified() {
    // PGN 129025 position rapid update
    uint8_t payload[8] = {0};
    // Build canId for PGN 129025, priority 6, source 5
    // PGN 129025 = 0x1F801: DP=1, PF=0xF8, PS=0x01
    // canId = (6<<26)|(0<<25)|(1<<24)|(0xF8<<16)|(0x01<<8)|0x05
    CANFrame f = makeExtFrame(0x19F80105, payload, 8);
    TEST_ASSERT_TRUE(CANParser::isNMEA2000(f));
}

void test_nmea2000_not_identified_standard() {
    CANFrame f;
    f.id = 0x7E8; f.extended = false; f.dlc = 8;
    TEST_ASSERT_FALSE(CANParser::isNMEA2000(f));
}

void test_nmea2000_parse_position_rapid() {
    // PGN 129025: Latitude[0..3] in 1e-7 deg, Longitude[4..7] in 1e-7 deg
    // lat = 48.1173°N → raw = 481173000 = 0x1CB7BF08 (little-endian)
    // lon = 11.5167°E → raw = 115167000 = 0x06DF3F58 (little-endian)
    int32_t latRaw = 481173000;
    int32_t lonRaw = 115167000;
    uint8_t payload[8];
    payload[0] = latRaw & 0xFF;
    payload[1] = (latRaw >> 8)  & 0xFF;
    payload[2] = (latRaw >> 16) & 0xFF;
    payload[3] = (latRaw >> 24) & 0xFF;
    payload[4] = lonRaw & 0xFF;
    payload[5] = (lonRaw >> 8)  & 0xFF;
    payload[6] = (lonRaw >> 16) & 0xFF;
    payload[7] = (lonRaw >> 24) & 0xFF;

    // canId for PGN 129025, SA=5
    CANFrame f = makeExtFrame(0x19F80105, payload, 8);
    NMEA2000Data d = CANParser::parseNMEA2000(f);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_TRUE(d.hasPosition);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 48.117, d.latitude);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 11.517, d.longitude);
}

void test_nmea2000_parse_water_temp() {
    // PGN 130310: SID(1), WaterTemp in 0.01K
    // 25°C = 298.15K → raw = 29815 = 0x7477
    uint16_t tempRaw = 29815;
    uint8_t payload[3];
    payload[0] = 0x00; // SID
    payload[1] = tempRaw & 0xFF;
    payload[2] = (tempRaw >> 8) & 0xFF;

    // canId for PGN 130310 = 0x1F80E → DP=1, PF=0xF8, PS=0x0E
    // canId = (6<<26)|(1<<24)|(0xF8<<16)|(0x0E<<8)|SA
    CANFrame f = makeExtFrame(0x19FD0605, payload, 3);
    NMEA2000Data d = CANParser::parseNMEA2000(f);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_TRUE(d.hasWaterTemp);
    // 29815 * 0.01 - 273.15 = 298.15 - 273.15 = 25.0°C
    TEST_ASSERT_FLOAT_WITHIN(0.1, 25.0, d.waterTempC);
}

void test_nmea2000_parse_heading() {
    // PGN 127250: SID(1), Heading[0..1] in 0.0001 rad
    // heading = 90° = π/2 rad → raw = (π/2 / 0.0001) = 15708 = 0x3D4C
    uint16_t headRaw = 15708;
    uint8_t payload[7] = {};
    payload[0] = 0x00; // SID
    payload[1] = headRaw & 0xFF;
    payload[2] = (headRaw >> 8) & 0xFF;
    // deviation[3..4] = 0x7FFF (not available)
    payload[3] = 0xFF; payload[4] = 0x7F;
    // variation[5..6] = 0x7FFF (not available)
    payload[5] = 0xFF; payload[6] = 0x7F;

    // canId for PGN 127250: DP=1, PF=0xF1, PS=0x12
    // = (6<<26)|(1<<24)|(0xF1<<16)|(0x12<<8)|0x05
    CANFrame f = makeExtFrame(0x19F11205, payload, 7);
    NMEA2000Data d = CANParser::parseNMEA2000(f);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_TRUE(d.hasHeading);
    // 15708 * 0.0001 * 57.2958 ≈ 90.0°
    TEST_ASSERT_FLOAT_WITHIN(1.0, 90.0, d.headingDegrees);
}

// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_frame_to_string_11bit);
    RUN_TEST(test_frame_to_string_29bit);
    RUN_TEST(test_extract_pgn_pdu2);
    RUN_TEST(test_extract_pgn_pdu1);
    RUN_TEST(test_obd_is_response);
    RUN_TEST(test_obd_not_response_extended);
    RUN_TEST(test_obd_not_response_wrong_id);
    RUN_TEST(test_obd_parse_rpm);
    RUN_TEST(test_obd_parse_speed);
    RUN_TEST(test_obd_parse_coolant_temp);
    RUN_TEST(test_obd_parse_engine_load);
    RUN_TEST(test_obd_parse_throttle);
    RUN_TEST(test_nmea2000_is_identified);
    RUN_TEST(test_nmea2000_not_identified_standard);
    RUN_TEST(test_nmea2000_parse_position_rapid);
    RUN_TEST(test_nmea2000_parse_water_temp);
    RUN_TEST(test_nmea2000_parse_heading);

    return UNITY_END();
}
