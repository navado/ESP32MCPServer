// test_i2c_sensors.cpp
// Unity tests for I2C sensor drivers using MockI2C

#include <unity.h>
#include "I2CSensors.h"
#include "mock/mock_i2c.h"
#include <cmath>
#include <cstring>

using namespace mcp;

static MockI2C bus;

void setUp()    { bus = MockI2C(); }
void tearDown() {}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

void test_factory_bme280() {
    bus.addDevice(0x76);
    auto drv = createI2CDriver(bus, 0x76);
    TEST_ASSERT_NOT_NULL(drv.get());
    TEST_ASSERT_EQUAL_STRING("BME280", drv->name());
}

void test_factory_mpu6050() {
    bus.addDevice(0x68);
    auto drv = createI2CDriver(bus, 0x68);
    TEST_ASSERT_NOT_NULL(drv.get());
    TEST_ASSERT_EQUAL_STRING("MPU6050", drv->name());
}

void test_factory_ads1115() {
    bus.addDevice(0x48);
    auto drv = createI2CDriver(bus, 0x48);
    TEST_ASSERT_NOT_NULL(drv.get());
    TEST_ASSERT_EQUAL_STRING("ADS1115", drv->name());
}

void test_factory_sht31() {
    bus.addDevice(0x44);
    auto drv = createI2CDriver(bus, 0x44);
    TEST_ASSERT_NOT_NULL(drv.get());
    TEST_ASSERT_EQUAL_STRING("SHT31", drv->name());
}

void test_factory_bh1750() {
    bus.addDevice(0x23);
    auto drv = createI2CDriver(bus, 0x23);
    TEST_ASSERT_NOT_NULL(drv.get());
    TEST_ASSERT_EQUAL_STRING("BH1750", drv->name());
}

void test_factory_ina219() {
    bus.addDevice(0x40);
    auto drv = createI2CDriver(bus, 0x40);
    TEST_ASSERT_NOT_NULL(drv.get());
    TEST_ASSERT_EQUAL_STRING("INA219", drv->name());
}

void test_factory_unknown() {
    auto drv = createI2CDriver(bus, 0x12);
    TEST_ASSERT_NULL(drv.get());
}

// ---------------------------------------------------------------------------
// URI generation
// ---------------------------------------------------------------------------

void test_uri_format() {
    bus.addDevice(0x76);
    bus.setReg(0x76, 0xD0, 0x60);
    auto drv = createI2CDriver(bus, 0x76);
    std::string uri = drv->uri();
    TEST_ASSERT_EQUAL_STRING("sensor://i2c/bme280_0x76", uri.c_str());
}

// ---------------------------------------------------------------------------
// BME280 probe
// ---------------------------------------------------------------------------

void test_bme280_probe_chip60() {
    bus.addDevice(0x76);
    bus.setReg(0x76, 0xD0, 0x60); // WHO_AM_I = BME280
    BME280Driver drv(bus, 0x76);
    TEST_ASSERT_TRUE(drv.probe());
}

void test_bme280_probe_chip58_bmp280() {
    bus.addDevice(0x76);
    bus.setReg(0x76, 0xD0, 0x58); // BMP280
    BME280Driver drv(bus, 0x76);
    TEST_ASSERT_TRUE(drv.probe());
}

void test_bme280_probe_fail_wrong_id() {
    bus.addDevice(0x76);
    bus.setReg(0x76, 0xD0, 0xAB); // unknown chip
    BME280Driver drv(bus, 0x76);
    TEST_ASSERT_FALSE(drv.probe());
}

// ---------------------------------------------------------------------------
// BME280 read (with pre-loaded compensation data)
// ---------------------------------------------------------------------------

void test_bme280_read_temperature() {
    bus.addDevice(0x76);
    bus.setReg(0x76, 0xD0, 0x60);

    // Set up minimal calibration for temperature only:
    // T1=27504, T2=26435, T3=-1000
    uint8_t calib[24] = {};
    uint16_t T1 = 27504;
    int16_t  T2 = 26435;
    int16_t  T3 = -1000;
    calib[0] = T1 & 0xFF;  calib[1] = T1 >> 8;
    calib[2] = T2 & 0xFF;  calib[3] = T2 >> 8;
    calib[4] = T3 & 0xFF;  calib[5] = T3 >> 8;
    // Pressure calibration (P1..P9) defaults to zero — compensatePressure will
    // short-circuit on zero var1 and return 0.
    bus.setRegs(0x76, 0x88, calib, 24);

    // H1
    bus.setReg(0x76, 0xA1, 75);
    // H2..H6 (E1..E7): H2=370, H3=0, H4=3072, H5=50, H6=30
    uint8_t hcal[7] = {0x72, 0x01, 0x00, 0xC0, 0x32, 0x00, 0x1E};
    bus.setRegs(0x76, 0xE1, hcal, 7);

    // Set measurement registers (0xF7..0xFE):
    // Use a raw temperature that should result in ~25°C.
    // With T1=27504, T2=26435, T3=-1000 and adc_T chosen to give ~25°C:
    // Use adc_T=519888 (a typical example from the Bosch datasheet).
    int32_t adc_T = 519888;
    int32_t adc_P = 0;  // pressure will be 0 (P1=0 → short circuit)
    uint8_t meas[8] = {};
    meas[0] = (adc_P >> 12) & 0xFF; // press_msb
    meas[1] = (adc_P >>  4) & 0xFF; // press_lsb
    meas[2] = (adc_P <<  4) & 0xF0; // press_xlsb
    meas[3] = (adc_T >> 12) & 0xFF; // temp_msb
    meas[4] = (adc_T >>  4) & 0xFF; // temp_lsb
    meas[5] = (adc_T <<  4) & 0xF0; // temp_xlsb
    // hum: adc_H=0 → humidity=0
    meas[6] = 0; meas[7] = 0;
    bus.setRegs(0x76, 0xF7, meas, 8);

    BME280Driver drv(bus, 0x76);
    TEST_ASSERT_TRUE(drv.init());

    auto readings = drv.read();
    TEST_ASSERT_GREATER_OR_EQUAL(2, (int)readings.size());

    // Find temperature reading
    bool foundTemp = false;
    for (auto& r : readings) {
        if (r.parameter == "temperature") {
            foundTemp = true;
            // The Bosch datasheet example gives T = 25.08°C for adc_T=519888
            TEST_ASSERT_FLOAT_WITHIN(2.0, 25.0, r.value);
            TEST_ASSERT_EQUAL_STRING("°C", r.unit.c_str());
            TEST_ASSERT_TRUE(r.valid);
        }
    }
    TEST_ASSERT_TRUE(foundTemp);
}

// ---------------------------------------------------------------------------
// MPU6050 probe
// ---------------------------------------------------------------------------

void test_mpu6050_probe() {
    bus.addDevice(0x68);
    bus.setReg(0x68, 0x75, 0x68); // WHO_AM_I
    MPU6050Driver drv(bus, 0x68);
    TEST_ASSERT_TRUE(drv.probe());
}

void test_mpu6050_probe_fail() {
    bus.addDevice(0x68);
    bus.setReg(0x68, 0x75, 0x00);
    MPU6050Driver drv(bus, 0x68);
    TEST_ASSERT_FALSE(drv.probe());
}

// ---------------------------------------------------------------------------
// MPU6050 read
// ---------------------------------------------------------------------------

void test_mpu6050_read_accel_gyro() {
    bus.addDevice(0x68);
    bus.setReg(0x68, 0x75, 0x68);

    // Set raw data registers (0x3B..0x48): 14 bytes
    // accel_x=1g=16384, accel_y=0, accel_z=0
    // temp=0, gyro_x=y=z=0
    uint8_t rawData[14] = {};
    int16_t ax = 16384; // 1g
    rawData[0] = ax >> 8; rawData[1] = ax & 0xFF;
    bus.setRegs(0x68, 0x3B, rawData, 14);

    MPU6050Driver drv(bus, 0x68);
    TEST_ASSERT_TRUE(drv.init());

    auto readings = drv.read();
    TEST_ASSERT_EQUAL(7, (int)readings.size());

    bool foundAccelX = false;
    for (auto& r : readings) {
        if (r.parameter == "accel_x") {
            foundAccelX = true;
            TEST_ASSERT_FLOAT_WITHIN(0.05, 1.0, r.value);
            TEST_ASSERT_EQUAL_STRING("g", r.unit.c_str());
        }
    }
    TEST_ASSERT_TRUE(foundAccelX);
}

// ---------------------------------------------------------------------------
// ADS1115 probe and read
// ---------------------------------------------------------------------------

void test_ads1115_probe() {
    bus.addDevice(0x48);
    auto drv = createI2CDriver(bus, 0x48);
    TEST_ASSERT_TRUE(drv->probe());
}

void test_ads1115_read_channels() {
    bus.addDevice(0x48);
    // Set conversion register (0x00) to return 16384 for every channel
    // 16384 * 4.096 / 32768 = 2.048 V
    int16_t raw = 16384;
    bus.setReg(0x48, 0x00, raw >> 8);
    bus.setReg(0x48, 0x01, raw & 0xFF);

    ADS1115Driver drv(bus, 0x48);
    TEST_ASSERT_TRUE(drv.init());

    auto readings = drv.read();
    TEST_ASSERT_EQUAL(4, (int)readings.size());
    for (auto& r : readings) {
        TEST_ASSERT_EQUAL_STRING("V", r.unit.c_str());
        TEST_ASSERT_TRUE(r.valid);
    }
}

// ---------------------------------------------------------------------------
// BH1750 read
// ---------------------------------------------------------------------------

void test_bh1750_read_lux() {
    bus.addDevice(0x23);
    // raw = 1200 → lux = 1200/1.2 = 1000 lux
    uint8_t rawData[2] = {0x04, 0xB0}; // 0x04B0 = 1200
    bus.setRawRead(0x23, rawData, 2);

    BH1750Driver drv(bus, 0x23);
    TEST_ASSERT_TRUE(drv.init());

    auto readings = drv.read();
    TEST_ASSERT_EQUAL(1, (int)readings.size());
    TEST_ASSERT_FLOAT_WITHIN(1.0, 1000.0, readings[0].value);
    TEST_ASSERT_EQUAL_STRING("lux", readings[0].unit.c_str());
}

// ---------------------------------------------------------------------------
// SHT31 read with CRC
// ---------------------------------------------------------------------------

static uint8_t sht31_crc(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}

void test_sht31_read_temp_hum() {
    bus.addDevice(0x44);

    // Prepare 6-byte response: tempRaw[2], CRC, humRaw[2], CRC
    // temp = 25°C → raw = (25+45)/175 * 65535 = 70/175 * 65535 = 26214 = 0x6666
    // hum  = 50% → raw = 50/100 * 65535 = 32768 = 0x8000
    uint16_t tempRaw = static_cast<uint16_t>((25.0 + 45.0) / 175.0 * 65535.0);
    uint16_t humRaw  = static_cast<uint16_t>(50.0 / 100.0 * 65535.0);

    uint8_t rawData[6];
    rawData[0] = tempRaw >> 8;
    rawData[1] = tempRaw & 0xFF;
    rawData[2] = sht31_crc(rawData, 2);
    rawData[3] = humRaw >> 8;
    rawData[4] = humRaw & 0xFF;
    rawData[5] = sht31_crc(rawData + 3, 2);

    bus.setRawRead(0x44, rawData, 6);

    SHT31Driver drv(bus, 0x44);
    TEST_ASSERT_TRUE(drv.init());

    auto readings = drv.read();
    TEST_ASSERT_EQUAL(2, (int)readings.size());

    bool foundTemp = false, foundHum = false;
    for (auto& r : readings) {
        if (r.parameter == "temperature") {
            foundTemp = true;
            TEST_ASSERT_FLOAT_WITHIN(1.0, 25.0, r.value);
        }
        if (r.parameter == "humidity") {
            foundHum = true;
            TEST_ASSERT_FLOAT_WITHIN(2.0, 50.0, r.value);
        }
    }
    TEST_ASSERT_TRUE(foundTemp);
    TEST_ASSERT_TRUE(foundHum);
}

// ---------------------------------------------------------------------------
// INA219 read
// ---------------------------------------------------------------------------

void test_ina219_read_voltage_current() {
    bus.addDevice(0x40);

    // Configuration register (0x00) must be non-zero for probe()
    bus.setReg(0x40, 0x00, 0x39);
    bus.setReg(0x40, 0x01, 0x9F);

    // Bus voltage register (0x02): bits[15:3]=raw, raw*4mV = 12.0V → raw=3000 → value=0xBB8
    // Register[15:3] = 3000 → register = (3000 << 3) = 24000 = 0x5DC0
    uint16_t busReg = (3000 << 3);
    bus.setReg(0x40, 0x04, busReg >> 8);   // reg 0x04 is actually bus voltage
    bus.setReg(0x40, 0x05, busReg & 0xFF);

    // Shunt voltage (0x01): raw in 10µV LSBs → 100mA * 0.1Ω = 10mV → 1000 LSBs = 0x03E8
    int16_t shuntRaw = 1000;
    bus.setReg(0x40, 0x02, shuntRaw >> 8);
    bus.setReg(0x40, 0x03, shuntRaw & 0xFF);

    INA219Driver drv(bus, 0x40);
    TEST_ASSERT_TRUE(drv.init());
    TEST_ASSERT_TRUE(drv.isInitialized());

    // Just verify read returns 4 readings with correct parameter names
    auto readings = drv.read();
    TEST_ASSERT_EQUAL(4, (int)readings.size());

    std::set<std::string> params;
    for (auto& r : readings) params.insert(r.parameter);

    // These include a std::set which we can check
    TEST_ASSERT_EQUAL(4, (int)params.size());
}

// ---------------------------------------------------------------------------

#include <set>

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_factory_bme280);
    RUN_TEST(test_factory_mpu6050);
    RUN_TEST(test_factory_ads1115);
    RUN_TEST(test_factory_sht31);
    RUN_TEST(test_factory_bh1750);
    RUN_TEST(test_factory_ina219);
    RUN_TEST(test_factory_unknown);
    RUN_TEST(test_uri_format);
    RUN_TEST(test_bme280_probe_chip60);
    RUN_TEST(test_bme280_probe_chip58_bmp280);
    RUN_TEST(test_bme280_probe_fail_wrong_id);
    RUN_TEST(test_bme280_read_temperature);
    RUN_TEST(test_mpu6050_probe);
    RUN_TEST(test_mpu6050_probe_fail);
    RUN_TEST(test_mpu6050_read_accel_gyro);
    RUN_TEST(test_ads1115_probe);
    RUN_TEST(test_ads1115_read_channels);
    RUN_TEST(test_bh1750_read_lux);
    RUN_TEST(test_sht31_read_temp_hum);
    RUN_TEST(test_ina219_read_voltage_current);

    return UNITY_END();
}
