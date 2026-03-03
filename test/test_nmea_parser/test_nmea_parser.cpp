// test_nmea_parser.cpp
// Unity tests for NMEAParser (pure C++, runs natively)

#ifdef NATIVE_TEST
#  include <unity.h>
#else
#  include <unity.h>
#endif

#include "NMEAParser.h"
#include <cmath>
#include <cstring>

using namespace mcp;

// ---------------------------------------------------------------------------
void setUp()    {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Checksum validation
// ---------------------------------------------------------------------------

void test_checksum_valid() {
    // $GPGGA,...*47 — standard checksum
    TEST_ASSERT_TRUE(NMEAParser::validateChecksum(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
}

void test_checksum_invalid() {
    TEST_ASSERT_FALSE(NMEAParser::validateChecksum(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*99"));
}

void test_checksum_missing_accepted() {
    // No '*': treated as no-checksum, returns true
    TEST_ASSERT_TRUE(NMEAParser::validateChecksum(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
}

// ---------------------------------------------------------------------------
// Split
// ---------------------------------------------------------------------------

void test_split_gga() {
    auto fields = NMEAParser::split(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");
    TEST_ASSERT_EQUAL(15, (int)fields.size());
    TEST_ASSERT_EQUAL_STRING("GPGGA", fields[0].c_str());
    TEST_ASSERT_EQUAL_STRING("123519", fields[1].c_str());
}

void test_split_empty_fields() {
    auto fields = NMEAParser::split("$GPGGA,,,*00");
    TEST_ASSERT_EQUAL(4, (int)fields.size());
    TEST_ASSERT_EQUAL_STRING("", fields[1].c_str());
}

// ---------------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------------

void test_parse_coordinate_north() {
    // 4807.038 N → 48 + 7.038/60 = 48.1173
    double lat = NMEAParser::parseCoordinate("4807.038", "N");
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 48.1173, lat);
}

void test_parse_coordinate_south() {
    double lat = NMEAParser::parseCoordinate("3352.947", "S");
    TEST_ASSERT_TRUE(lat < 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001, -33.882, lat);
}

void test_parse_coordinate_west() {
    double lon = NMEAParser::parseCoordinate("07230.516", "W");
    TEST_ASSERT_TRUE(lon < 0);
}

// ---------------------------------------------------------------------------
// Time parsing
// ---------------------------------------------------------------------------

void test_parse_time() {
    int h, m; float s;
    TEST_ASSERT_TRUE(NMEAParser::parseTime("123519.00", h, m, s));
    TEST_ASSERT_EQUAL(12, h);
    TEST_ASSERT_EQUAL(35, m);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.0f, s);
}

// ---------------------------------------------------------------------------
// GGA sentence
// ---------------------------------------------------------------------------

void test_parse_gga_valid() {
    ParsedNMEA p = NMEAParser::parse(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");

    TEST_ASSERT_EQUAL_STRING("GGA", p.type.c_str());
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_TRUE(p.hasPosition);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 48.117, p.latitude);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 11.517, p.longitude);
    TEST_ASSERT_TRUE(p.hasAltitude);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 545.4, p.altitude);
    TEST_ASSERT_TRUE(p.hasFixInfo);
    TEST_ASSERT_EQUAL(1, p.fixQuality);
    TEST_ASSERT_EQUAL(8, p.satellites);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.9, p.hdop);
    TEST_ASSERT_TRUE(p.hasTime);
    TEST_ASSERT_EQUAL(12, p.hour);
    TEST_ASSERT_EQUAL(35, p.minute);
}

void test_parse_gga_no_fix() {
    ParsedNMEA p = NMEAParser::parse(
        "$GPGGA,000000,,,,,0,00,99.0,,M,,M,,");
    TEST_ASSERT_EQUAL_STRING("GGA", p.type.c_str());
    TEST_ASSERT_FALSE(p.valid); // fixQuality==0
}

// ---------------------------------------------------------------------------
// RMC sentence
// ---------------------------------------------------------------------------

void test_parse_rmc_active() {
    ParsedNMEA p = NMEAParser::parse(
        "$GPRMC,220516,A,5133.82,N,00042.24,W,173.8,231.8,130694,004.2,W*70");

    TEST_ASSERT_EQUAL_STRING("RMC", p.type.c_str());
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_TRUE(p.active);
    TEST_ASSERT_TRUE(p.hasPosition);
    TEST_ASSERT_TRUE(p.hasSpeed);
    TEST_ASSERT_FLOAT_WITHIN(0.5, 173.8, p.speedKnots);
    TEST_ASSERT_TRUE(p.hasCourse);
    TEST_ASSERT_FLOAT_WITHIN(0.5, 231.8, p.courseTrue);
}

void test_parse_rmc_void() {
    ParsedNMEA p = NMEAParser::parse(
        "$GPRMC,220516,V,,,,,,,130694,,*25");
    TEST_ASSERT_FALSE(p.valid);
    TEST_ASSERT_FALSE(p.active);
}

// ---------------------------------------------------------------------------
// VTG sentence
// ---------------------------------------------------------------------------

void test_parse_vtg() {
    ParsedNMEA p = NMEAParser::parse(
        "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48");

    TEST_ASSERT_EQUAL_STRING("VTG", p.type.c_str());
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_TRUE(p.hasCourse);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 54.7, p.courseTrue);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 34.4, p.courseMagnetic);
    TEST_ASSERT_TRUE(p.hasSpeed);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 5.5, p.speedKnots);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 10.2, p.speedKmh);
}

// ---------------------------------------------------------------------------
// MWV — wind
// ---------------------------------------------------------------------------

void test_parse_mwv_relative() {
    ParsedNMEA p = NMEAParser::parse(
        "$IIMWV,045.0,R,12.5,N,A*00");
    // Note: checksum may not match; validateChecksum returns true for
    // wrong checksum, but our validateChecksum returns false.
    // Provide a correct sentence instead.
    // Actually let's construct a sentence without checksum:
    p = NMEAParser::parse("$IIMWV,045.0,R,12.5,N,A");
    TEST_ASSERT_EQUAL_STRING("MWV", p.type.c_str());
    TEST_ASSERT_TRUE(p.hasWind);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 45.0, p.windAngle);
    TEST_ASSERT_TRUE(p.windRelative);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 12.5, p.windSpeed);
    TEST_ASSERT_EQUAL('N', p.windSpeedUnit);
    TEST_ASSERT_TRUE(p.valid);
}

// ---------------------------------------------------------------------------
// DBT — depth
// ---------------------------------------------------------------------------

void test_parse_dbt() {
    ParsedNMEA p = NMEAParser::parse(
        "$SDDBT,025.1,f,007.7,M,004.2,F");
    TEST_ASSERT_EQUAL_STRING("DBT", p.type.c_str());
    TEST_ASSERT_TRUE(p.hasDepth);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 7.7, p.depthMetres);
    TEST_ASSERT_TRUE(p.valid);
}

// ---------------------------------------------------------------------------
// HDG / HDT — heading
// ---------------------------------------------------------------------------

void test_parse_hdg() {
    ParsedNMEA p = NMEAParser::parse(
        "$HCHDG,215.3,,,3.5,E");
    TEST_ASSERT_EQUAL_STRING("HDG", p.type.c_str());
    TEST_ASSERT_TRUE(p.hasHeading);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 215.3, p.headingDegrees);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 3.5, p.variation);
    TEST_ASSERT_FALSE(p.variationWest);
    TEST_ASSERT_TRUE(p.valid);
}

void test_parse_hdt() {
    ParsedNMEA p = NMEAParser::parse("$HEHDT,258.6,T");
    TEST_ASSERT_EQUAL_STRING("HDT", p.type.c_str());
    TEST_ASSERT_TRUE(p.hasHeading);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 258.6, p.headingDegrees);
    TEST_ASSERT_TRUE(p.valid);
}

// ---------------------------------------------------------------------------
// Bad checksum → invalid result
// ---------------------------------------------------------------------------

void test_bad_checksum_returns_invalid() {
    ParsedNMEA p = NMEAParser::parse(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*FF");
    TEST_ASSERT_FALSE(p.valid);
}

// ---------------------------------------------------------------------------
// Unknown sentence type
// ---------------------------------------------------------------------------

void test_unknown_sentence_type() {
    ParsedNMEA p = NMEAParser::parse("$GPXXX,1,2,3");
    TEST_ASSERT_EQUAL_STRING("XXX", p.type.c_str());
    TEST_ASSERT_FALSE(p.valid);
}

// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_checksum_valid);
    RUN_TEST(test_checksum_invalid);
    RUN_TEST(test_checksum_missing_accepted);
    RUN_TEST(test_split_gga);
    RUN_TEST(test_split_empty_fields);
    RUN_TEST(test_parse_coordinate_north);
    RUN_TEST(test_parse_coordinate_south);
    RUN_TEST(test_parse_coordinate_west);
    RUN_TEST(test_parse_time);
    RUN_TEST(test_parse_gga_valid);
    RUN_TEST(test_parse_gga_no_fix);
    RUN_TEST(test_parse_rmc_active);
    RUN_TEST(test_parse_rmc_void);
    RUN_TEST(test_parse_vtg);
    RUN_TEST(test_parse_mwv_relative);
    RUN_TEST(test_parse_dbt);
    RUN_TEST(test_parse_hdg);
    RUN_TEST(test_parse_hdt);
    RUN_TEST(test_bad_checksum_returns_invalid);
    RUN_TEST(test_unknown_sentence_type);

    return UNITY_END();
}
