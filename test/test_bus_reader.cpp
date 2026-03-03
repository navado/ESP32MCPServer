// test_bus_reader.cpp
// Unity tests for SerialBusReader and CANBusReader using mock ports

#include <unity.h>
#include "BusReader.h"
#include "mock/mock_serial.h"
#include "mock/mock_can.h"
#include <cstdint>
#include <cstring>

using namespace mcp;

// Simple fake clock that advances by a fixed step each call
static uint32_t g_clock_ms = 0;
static uint32_t clockFn() { return g_clock_ms; }

void setUp()    { g_clock_ms = 0; }
void tearDown() {}

// ---------------------------------------------------------------------------
// SerialBusReader — RAW mode
// ---------------------------------------------------------------------------

void test_serial_raw_reads_lines() {
    MockSerialPort port;
    port.feedLine("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");
    port.feedLine("$GPRMC,220516,A,5133.82,N,00042.24,W,173.8,231.8,130694,004.2,W*70");

    // Clock: advance past durationMs on first call after data consumed
    uint32_t callCount = 0;
    SerialBusReader reader(port, [&]() -> uint32_t {
        // First call = start time (0), subsequent calls = 200 ms (past 100 ms duration)
        return (callCount++ == 0) ? 0 : 200;
    });

    auto result = reader.readFor(100, ParseMode::RAW);

    TEST_ASSERT_EQUAL(2, (int)result.linesReceived);
    TEST_ASSERT_EQUAL(2, (int)result.rawLines.size());
    TEST_ASSERT_EQUAL_STRING(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
        result.rawLines[0].c_str());
}

void test_serial_raw_empty_port() {
    MockSerialPort port;

    SerialBusReader reader(port, [&]() -> uint32_t {
        static int n = 0; return (n++ == 0) ? 0 : 200;
    });

    auto result = reader.readFor(100, ParseMode::RAW);
    TEST_ASSERT_EQUAL(0, (int)result.linesReceived);
    TEST_ASSERT_EQUAL(0, (int)result.rawLines.size());
}

// ---------------------------------------------------------------------------
// SerialBusReader — PARSED mode
// ---------------------------------------------------------------------------

void test_serial_parsed_gga() {
    MockSerialPort port;
    port.feedLine("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");

    SerialBusReader reader(port, [&]() -> uint32_t {
        static int n = 0; return (n++ == 0) ? 0 : 200;
    });

    auto result = reader.readFor(100, ParseMode::PARSED);

    TEST_ASSERT_EQUAL(1, (int)result.linesReceived);
    TEST_ASSERT_EQUAL(1, (int)result.parsedNMEA.size());

    const auto& p = result.parsedNMEA[0];
    TEST_ASSERT_EQUAL_STRING("GGA", p.type.c_str());
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_TRUE(p.hasPosition);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 48.117, p.latitude);
}

void test_serial_parsed_multiple_sentences() {
    MockSerialPort port;
    port.feedLine("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");
    port.feedLine("$GPRMC,220516,A,5133.82,N,00042.24,W,173.8,231.8,130694,004.2,W*70");
    port.feedLine("$HEHDT,258.6,T");

    SerialBusReader reader(port, [&]() -> uint32_t {
        static int n = 0; return (n++ == 0) ? 0 : 500;
    });

    auto result = reader.readFor(200, ParseMode::PARSED);
    TEST_ASSERT_EQUAL(3, (int)result.linesReceived);
    TEST_ASSERT_EQUAL(3, (int)result.parsedNMEA.size());
}

// ---------------------------------------------------------------------------
// BusReadResult::toJson (RAW mode)
// ---------------------------------------------------------------------------

void test_bus_result_json_raw() {
    BusReadResult result;
    result.durationMs    = 500;
    result.linesReceived = 1;
    result.rawLines.push_back("$GPGGA,...");

    std::string json = result.toJson(ParseMode::RAW);
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"durationMs\":500"));
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"linesReceived\":1"));
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "$GPGGA,..."));
}

void test_bus_result_json_parsed() {
    BusReadResult result;
    result.durationMs    = 100;
    result.linesReceived = 0;
    result.framesReceived = 0;
    // No readings; just check structure compiles and returns valid JSON
    std::string json = result.toJson(ParseMode::PARSED);
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"parsedNMEA\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"parsedNMEA2000\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"parsedOBDII\""));
}

// ---------------------------------------------------------------------------
// CANBusReader — RAW mode
// ---------------------------------------------------------------------------

void test_can_raw_receives_frames() {
    MockCANPort port;
    CANFrameRaw f{};
    f.id = 0x7E8; f.dlc = 8;
    f.data[1] = 0x41; f.data[2] = 0x0C;
    port.feedFrame(f);

    CANBusReader reader(port, [&]() -> uint32_t {
        static int n = 0; return (n++ == 0) ? 0 : 200;
    });

    auto result = reader.readFor(100, ParseMode::RAW);
    TEST_ASSERT_EQUAL(1, (int)result.framesReceived);
    TEST_ASSERT_EQUAL(1, (int)result.rawFrames.size());
    TEST_ASSERT_EQUAL(0x7E8u, result.rawFrames[0].id);
}

void test_can_raw_no_frames() {
    MockCANPort port;

    CANBusReader reader(port, [&]() -> uint32_t {
        static int n = 0; return (n++ == 0) ? 0 : 200;
    });

    auto result = reader.readFor(100, ParseMode::RAW);
    TEST_ASSERT_EQUAL(0, (int)result.framesReceived);
}

// ---------------------------------------------------------------------------
// CANBusReader — PARSED mode (OBD-II)
// ---------------------------------------------------------------------------

void test_can_parsed_obd_rpm() {
    MockCANPort port;
    port.feedOBDResponse(0x0C, 0x0F, 0xA0); // RPM = 1000

    CANBusReader reader(port, [&]() -> uint32_t {
        static int n = 0; return (n++ == 0) ? 0 : 200;
    });

    auto result = reader.readFor(100, ParseMode::PARSED);
    TEST_ASSERT_EQUAL(1, (int)result.framesReceived);
    TEST_ASSERT_EQUAL(1, (int)result.parsedOBDII.size());
    TEST_ASSERT_FLOAT_WITHIN(5.0, 1000.0, result.parsedOBDII[0].value);
    TEST_ASSERT_EQUAL_STRING("rpm", result.parsedOBDII[0].unit.c_str());
}

// ---------------------------------------------------------------------------
// CANBusReader — PARSED mode (NMEA 2000 water temp)
// ---------------------------------------------------------------------------

void test_can_parsed_nmea2000_water_temp() {
    MockCANPort port;

    // PGN 130310: SID(1), WaterTemp in 0.01K
    // 25°C = 298.15K → raw = 29815
    uint16_t tempRaw = 29815;
    uint8_t payload[3];
    payload[0] = 0x00;
    payload[1] = tempRaw & 0xFF;
    payload[2] = (tempRaw >> 8) & 0xFF;

    // canId for PGN 130310: (6<<26)|(1<<24)|(0xF8<<16)|(0x0E<<8)|SA=5
    port.feedNMEA2000Frame(0x19FD0605, payload, 3);

    CANBusReader reader(port, [&]() -> uint32_t {
        static int n = 0; return (n++ == 0) ? 0 : 200;
    });

    auto result = reader.readFor(100, ParseMode::PARSED);
    TEST_ASSERT_EQUAL(1, (int)result.framesReceived);
    TEST_ASSERT_EQUAL(1, (int)result.parsedNMEA2000.size());
    TEST_ASSERT_FLOAT_WITHIN(0.5, 25.0, result.parsedNMEA2000[0].waterTempC);
}

// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_serial_raw_reads_lines);
    RUN_TEST(test_serial_raw_empty_port);
    RUN_TEST(test_serial_parsed_gga);
    RUN_TEST(test_serial_parsed_multiple_sentences);
    RUN_TEST(test_bus_result_json_raw);
    RUN_TEST(test_bus_result_json_parsed);
    RUN_TEST(test_can_raw_receives_frames);
    RUN_TEST(test_can_raw_no_frames);
    RUN_TEST(test_can_parsed_obd_rpm);
    RUN_TEST(test_can_parsed_nmea2000_water_temp);

    return UNITY_END();
}
