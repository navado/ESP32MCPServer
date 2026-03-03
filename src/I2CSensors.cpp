#include "I2CSensors.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace mcp {

// ---------------------------------------------------------------------------
// I2CSensorDriver — base helpers
// ---------------------------------------------------------------------------

std::string I2CSensorDriver::uri() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "sensor://i2c/%s_0x%02X",
                  name(), static_cast<unsigned>(address_));
    // Lower-case the name portion ("sensor://i2c/" is 13 chars)
    std::string s = buf;
    static const size_t prefix_len = 13; // strlen("sensor://i2c/")
    for (size_t i = prefix_len; i < s.size(); ++i) {
        if (s[i] == '_') break;
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32;
    }
    return s;
}

SensorReading I2CSensorDriver::make(const std::string& param, double value,
                                     const std::string& unit, uint64_t ts) const {
    char id[32];
    std::snprintf(id, sizeof(id), "%s_0x%02X",
                  name(), static_cast<unsigned>(address_));
    // Lower-case
    std::string sid = id;
    for (char& c : sid) if (c >= 'A' && c <= 'Z') c += 32;
    return SensorReading(sid, param, value, unit, ts);
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::vector<uint8_t> knownI2CAddresses() {
    return {
        0x23, 0x5C,         // BH1750
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, // INA219 (0x40..0x4F) + SHT31 (0x44,0x45)
        0x48, 0x49, 0x4A, 0x4B, // ADS1115
        0x68, 0x69,         // MPU6050
        0x76, 0x77,         // BME280 / BMP280
    };
}

std::unique_ptr<I2CSensorDriver> createI2CDriver(I2CInterface& bus,
                                                   uint8_t address) {
    switch (address) {
    case 0x76: case 0x77:
        return std::make_unique<BME280Driver>(bus, address);
    case 0x68: case 0x69:
        return std::make_unique<MPU6050Driver>(bus, address);
    case 0x48: case 0x49: case 0x4A: case 0x4B:
        return std::make_unique<ADS1115Driver>(bus, address);
    case 0x44: case 0x45:
        // SHT31 has priority over INA219 at those addresses
        return std::make_unique<SHT31Driver>(bus, address);
    case 0x40: case 0x41: case 0x42: case 0x43:
        return std::make_unique<INA219Driver>(bus, address);
    case 0x23: case 0x5C:
        return std::make_unique<BH1750Driver>(bus, address);
    default:
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// BME280 / BMP280
// ---------------------------------------------------------------------------

BME280Driver::BME280Driver(I2CInterface& bus, uint8_t addr)
    : I2CSensorDriver(bus, addr) {}

bool BME280Driver::probe() {
    uint8_t chipId = bus_.readReg8(address_, 0xD0);
    isBMP280_ = (chipId == 0x58);
    return (chipId == 0x60 || chipId == 0x58);
}

bool BME280Driver::readCalibration() {
    // Temperature / pressure calibration: 0x88..0x9F
    uint8_t buf[26] = {};
    if (!bus_.readBytes(address_, 0x88, buf, 24)) return false;

    calib_.T1 = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    calib_.T2 = static_cast<int16_t>(buf[2] | (buf[3] << 8));
    calib_.T3 = static_cast<int16_t>(buf[4] | (buf[5] << 8));

    calib_.P1 = static_cast<uint16_t>(buf[6])  | (static_cast<uint16_t>(buf[7])  << 8);
    calib_.P2 = static_cast<int16_t>(buf[8]  | (buf[9]  << 8));
    calib_.P3 = static_cast<int16_t>(buf[10] | (buf[11] << 8));
    calib_.P4 = static_cast<int16_t>(buf[12] | (buf[13] << 8));
    calib_.P5 = static_cast<int16_t>(buf[14] | (buf[15] << 8));
    calib_.P6 = static_cast<int16_t>(buf[16] | (buf[17] << 8));
    calib_.P7 = static_cast<int16_t>(buf[18] | (buf[19] << 8));
    calib_.P8 = static_cast<int16_t>(buf[20] | (buf[21] << 8));
    calib_.P9 = static_cast<int16_t>(buf[22] | (buf[23] << 8));

    if (!isBMP280_) {
        // Humidity calibration: H1 at 0xA1, H2..H6 at 0xE1..0xE7
        calib_.H1 = bus_.readReg8(address_, 0xA1);
        uint8_t h[7] = {};
        if (!bus_.readBytes(address_, 0xE1, h, 7)) return false;
        calib_.H2 = static_cast<int16_t>(h[0] | (h[1] << 8));
        calib_.H3 = h[2];
        calib_.H4 = static_cast<int16_t>((static_cast<int16_t>(h[3]) << 4) |
                                          (h[4] & 0x0F));
        calib_.H5 = static_cast<int16_t>((static_cast<int16_t>(h[5]) << 4) |
                                          (h[4] >> 4));
        calib_.H6 = static_cast<int8_t>(h[6]);
    }
    return true;
}

bool BME280Driver::init() {
    if (!probe()) return false;
    if (!readCalibration()) return false;

    // Normal mode, oversampling x1 for all channels
    // ctrl_hum (0xF2): osrs_h = 1
    if (!isBMP280_) bus_.writeReg8(address_, 0xF2, 0x01);
    // ctrl_meas (0xF4): osrs_t=1, osrs_p=1, mode=normal(11)
    bus_.writeReg8(address_, 0xF4, 0x27);
    // config (0xF5): t_sb=0.5ms, filter=off
    bus_.writeReg8(address_, 0xF5, 0x00);

    initialized_ = true;
    return true;
}

double BME280Driver::compensateTemp(int32_t adc, int32_t& t_fine) {
    int32_t var1 = (((adc >> 3) - (static_cast<int32_t>(calib_.T1) << 1)) *
                     static_cast<int32_t>(calib_.T2)) >> 11;
    int32_t var2 = (((((adc >> 4) - static_cast<int32_t>(calib_.T1)) *
                       ((adc >> 4) - static_cast<int32_t>(calib_.T1))) >> 12) *
                     static_cast<int32_t>(calib_.T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) / 256.0 / 100.0;
}

double BME280Driver::compensatePressure(int32_t adc, int32_t t_fine) {
    int64_t var1 = static_cast<int64_t>(t_fine) - 128000;
    int64_t var2 = var1 * var1 * static_cast<int64_t>(calib_.P6);
    var2 += (var1 * static_cast<int64_t>(calib_.P5)) << 17;
    var2 += static_cast<int64_t>(calib_.P4) << 35;
    var1  = ((var1 * var1 * static_cast<int64_t>(calib_.P3)) >> 8) +
             ((var1 * static_cast<int64_t>(calib_.P2)) << 12);
    var1  = (((static_cast<int64_t>(1) << 47) + var1) *
               static_cast<int64_t>(calib_.P1)) >> 33;
    if (var1 == 0) return 0.0;

    int64_t p = 1048576 - adc;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (static_cast<int64_t>(calib_.P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (static_cast<int64_t>(calib_.P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (static_cast<int64_t>(calib_.P7) << 4);
    return static_cast<double>(p) / 256.0; // Pa
}

double BME280Driver::compensateHumidity(int32_t adc, int32_t t_fine) {
    int32_t v = t_fine - 76800;
    v = (((((adc << 14) -
             (static_cast<int32_t>(calib_.H4) << 20) -
             (static_cast<int32_t>(calib_.H5) * v)) + 16384) >> 15) *
          (((((((v * static_cast<int32_t>(calib_.H6)) >> 10) *
               (((v * static_cast<int32_t>(calib_.H3)) >> 11) + 32768)) >> 10) +
              2097152) * static_cast<int32_t>(calib_.H2) + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * static_cast<int32_t>(calib_.H1)) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return static_cast<double>(v >> 12) / 1024.0;
}

std::vector<SensorReading> BME280Driver::read() {
    std::vector<SensorReading> out;
    if (!initialized_) return out;

    // Read 8 bytes starting at 0xF7: press_msb, press_lsb, press_xlsb,
    //                                  temp_msb,  temp_lsb,  temp_xlsb,
    //                                  hum_msb,   hum_lsb
    uint8_t buf[8] = {};
    if (!bus_.readBytes(address_, 0xF7, buf, isBMP280_ ? 6 : 8)) return out;

    int32_t pressADC = (static_cast<int32_t>(buf[0]) << 12) |
                       (static_cast<int32_t>(buf[1]) << 4)  |
                       (buf[2] >> 4);
    int32_t tempADC  = (static_cast<int32_t>(buf[3]) << 12) |
                       (static_cast<int32_t>(buf[4]) << 4)  |
                       (buf[5] >> 4);

    int32_t t_fine = 0;
    double tempC = compensateTemp(tempADC, t_fine);
    double pressP = compensatePressure(pressADC, t_fine);

    out.push_back(make("temperature", tempC,         "°C"));
    out.push_back(make("pressure",    pressP / 100.0, "hPa")); // Pa -> hPa

    if (!isBMP280_) {
        int32_t humADC = (static_cast<int32_t>(buf[6]) << 8) | buf[7];
        double hum = compensateHumidity(humADC, t_fine);
        out.push_back(make("humidity", hum, "%RH"));
    }
    return out;
}

std::vector<std::string> BME280Driver::parameters() const {
    if (isBMP280_) return {"temperature", "pressure"};
    return {"temperature", "humidity", "pressure"};
}

// ---------------------------------------------------------------------------
// MPU6050
// ---------------------------------------------------------------------------

MPU6050Driver::MPU6050Driver(I2CInterface& bus, uint8_t addr)
    : I2CSensorDriver(bus, addr) {}

bool MPU6050Driver::probe() {
    uint8_t who = bus_.readReg8(address_, 0x75);
    return (who == 0x68 || who == 0x72); // 0x72 = ICM-20602 compat
}

bool MPU6050Driver::init() {
    if (!probe()) return false;
    // Wake from sleep: PWR_MGMT_1 (0x6B) = 0x00
    bus_.writeReg8(address_, 0x6B, 0x00);
    // Accelerometer config: ±2g (0x1C = 0x00 → AFS_SEL=0)
    bus_.writeReg8(address_, 0x1C, 0x00);
    // Gyroscope config: ±250°/s (0x1B = 0x00 → FS_SEL=0)
    bus_.writeReg8(address_, 0x1B, 0x00);
    initialized_ = true;
    return true;
}

std::vector<SensorReading> MPU6050Driver::read() {
    std::vector<SensorReading> out;
    if (!initialized_) return out;

    // Read 14 bytes: ACCEL_XOUT[2], ACCEL_YOUT[2], ACCEL_ZOUT[2],
    //                TEMP_OUT[2], GYRO_XOUT[2], GYRO_YOUT[2], GYRO_ZOUT[2]
    uint8_t buf[14] = {};
    if (!bus_.readBytes(address_, 0x3B, buf, 14)) return out;

    auto be16s = [&](int i) -> int16_t {
        return static_cast<int16_t>((buf[i] << 8) | buf[i+1]);
    };

    double ax = be16s(0)  / accelScale_;
    double ay = be16s(2)  / accelScale_;
    double az = be16s(4)  / accelScale_;
    double t  = be16s(6)  / 340.0 + 36.53;
    double gx = be16s(8)  / gyroScale_;
    double gy = be16s(10) / gyroScale_;
    double gz = be16s(12) / gyroScale_;

    out.push_back(make("accel_x",     ax, "g"));
    out.push_back(make("accel_y",     ay, "g"));
    out.push_back(make("accel_z",     az, "g"));
    out.push_back(make("gyro_x",      gx, "°/s"));
    out.push_back(make("gyro_y",      gy, "°/s"));
    out.push_back(make("gyro_z",      gz, "°/s"));
    out.push_back(make("temperature", t,  "°C"));
    return out;
}

// ---------------------------------------------------------------------------
// ADS1115
// ---------------------------------------------------------------------------

ADS1115Driver::ADS1115Driver(I2CInterface& bus, uint8_t addr)
    : I2CSensorDriver(bus, addr) {}

bool ADS1115Driver::probe() {
    // ADS1115 doesn't have a chip-ID register; just check it ACKs
    return bus_.devicePresent(address_);
}

bool ADS1115Driver::init() {
    if (!probe()) return false;
    initialized_ = true;
    return true;
}

int16_t ADS1115Driver::readChannel(uint8_t muxBits) {
    // Config register (0x01):
    // [15]    OS  = 1 (start single-shot)
    // [14:12] MUX = muxBits
    // [11:9]  PGA = 001 (±4.096 V)
    // [8]     MODE= 1   (single-shot)
    // [7:5]   DR  = 100 (128 SPS)
    // [4]     COMP_MODE=0
    // [3]     COMP_POL=0
    // [2]     COMP_LAT=0
    // [1:0]   COMP_QUE=11 (disable comparator)
    uint16_t config = 0x8000 |
                      (static_cast<uint16_t>(muxBits & 0x07) << 12) |
                      0x0200 | // PGA=±4.096V
                      0x0100 | // single-shot
                      0x0080 | // 128 SPS
                      0x0003;  // disable comparator

    bus_.writeReg16(address_, 0x01, config);
    bus_.delayMs(9); // wait for conversion (128 SPS → ~7.8 ms)

    return bus_.readReg16s(address_, 0x00); // conversion register
}

std::vector<SensorReading> ADS1115Driver::read() {
    std::vector<SensorReading> out;
    if (!initialized_) return out;

    // MUX bits for single-ended: AIN0=100, AIN1=101, AIN2=110, AIN3=111
    const uint8_t mux[4] = {0x04, 0x05, 0x06, 0x07};
    const char*  names[4] = {"ch0_V","ch1_V","ch2_V","ch3_V"};

    for (int ch = 0; ch < 4; ++ch) {
        int16_t raw = readChannel(mux[ch]);
        float   v   = toVolts(raw);
        out.push_back(make(names[ch], v, "V"));
    }
    return out;
}

// ---------------------------------------------------------------------------
// SHT31
// ---------------------------------------------------------------------------

SHT31Driver::SHT31Driver(I2CInterface& bus, uint8_t addr)
    : I2CSensorDriver(bus, addr) {}

bool SHT31Driver::crc8(const uint8_t* data, size_t len, uint8_t expected) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc == expected;
}

bool SHT31Driver::probe() {
    return bus_.devicePresent(address_);
}

bool SHT31Driver::init() {
    if (!probe()) return false;
    // Soft reset
    bus_.sendCommand(address_, 0x30);
    bus_.sendCommand(address_, 0xA2);
    bus_.delayMs(2);
    initialized_ = true;
    return true;
}

std::vector<SensorReading> SHT31Driver::read() {
    std::vector<SensorReading> out;
    if (!initialized_) return out;

    // High repeatability measurement, clock stretching disabled
    // Command: 0x24 0x00
    uint8_t cmd[2] = {0x24, 0x00};
    bus_.writeBytes(address_, cmd[0], &cmd[1], 1);
    bus_.delayMs(20); // measurement takes ~15 ms

    uint8_t buf[6] = {};
    size_t n = bus_.readRaw(address_, buf, 6);
    if (n < 6) return out;

    // buf[0..1] = temperature raw, buf[2] = CRC
    // buf[3..4] = humidity raw,    buf[5] = CRC
    if (!crc8(buf, 2, buf[2]) || !crc8(buf + 3, 2, buf[5])) return out;

    uint16_t tempRaw = (buf[0] << 8) | buf[1];
    uint16_t humRaw  = (buf[3] << 8) | buf[4];

    double tempC = -45.0 + 175.0 * tempRaw / 65535.0;
    double hum   = 100.0 * humRaw / 65535.0;

    out.push_back(make("temperature", tempC, "°C"));
    out.push_back(make("humidity",    hum,   "%RH"));
    return out;
}

// ---------------------------------------------------------------------------
// BH1750
// ---------------------------------------------------------------------------

BH1750Driver::BH1750Driver(I2CInterface& bus, uint8_t addr)
    : I2CSensorDriver(bus, addr) {}

bool BH1750Driver::probe() {
    return bus_.devicePresent(address_);
}

bool BH1750Driver::init() {
    if (!probe()) return false;
    // Power on
    bus_.sendCommand(address_, 0x01);
    // Continuously H-resolution mode (1 lux resolution, 120 ms)
    bus_.sendCommand(address_, 0x10);
    bus_.delayMs(180);
    initialized_ = true;
    return true;
}

std::vector<SensorReading> BH1750Driver::read() {
    std::vector<SensorReading> out;
    if (!initialized_) return out;

    uint8_t buf[2] = {};
    size_t n = bus_.readRaw(address_, buf, 2);
    if (n < 2) return out;

    uint16_t raw = (buf[0] << 8) | buf[1];
    double lux = raw / 1.2; // datasheet: lux = raw / 1.2

    out.push_back(make("light_lux", lux, "lux"));
    return out;
}

// ---------------------------------------------------------------------------
// INA219
// ---------------------------------------------------------------------------

INA219Driver::INA219Driver(I2CInterface& bus, uint8_t addr)
    : I2CSensorDriver(bus, addr) {}

bool INA219Driver::probe() {
    // Check configuration register is readable and non-zero
    uint16_t cfg = static_cast<uint16_t>(bus_.readReg16u(address_, 0x00));
    return (cfg != 0x0000 && cfg != 0xFFFF);
}

bool INA219Driver::init() {
    if (!probe()) return false;

    // Config: Bus voltage range=32V, PGA=÷8(320mV), BADC=12bit, SADC=12bit, mode=continuous
    // 0x399F = 0b0011 1001 1001 1111
    bus_.writeReg16(address_, 0x00, 0x399F);

    // Calibration register for 0.1 mA LSB with a 0.1 Ω shunt:
    // Cal = trunc(0.04096 / (currentLSB_ * 0.001 * R_shunt))
    // With currentLSB_=0.1 mA and R=0.1 Ω: Cal = trunc(0.04096/(0.0001*0.1)) = 4096
    bus_.writeReg16(address_, 0x05, 4096);

    initialized_ = true;
    return true;
}

std::vector<SensorReading> INA219Driver::read() {
    std::vector<SensorReading> out;
    if (!initialized_) return out;

    // Shunt voltage register (0x01): LSB = 10 µV
    int16_t shuntRaw = bus_.readReg16s(address_, 0x01);
    double shuntMV = shuntRaw * 0.01; // 10µV → mV

    // Bus voltage register (0x02): bits [15:3], LSB = 4 mV
    uint16_t busRaw = bus_.readReg16u(address_, 0x02);
    double busV = (busRaw >> 3) * 0.004;

    // Current register (0x04): LSB = currentLSB_ mA
    int16_t currentRaw = bus_.readReg16s(address_, 0x04);
    double currentMA = currentRaw * currentLSB_;

    // Power register (0x03): LSB = 20 * currentLSB_ mA
    uint16_t powerRaw = bus_.readReg16u(address_, 0x03);
    double powerMW = powerRaw * 20.0 * currentLSB_;

    out.push_back(make("bus_voltage_V", busV,      "V"));
    out.push_back(make("shunt_mV",      shuntMV,   "mV"));
    out.push_back(make("current_mA",    currentMA, "mA"));
    out.push_back(make("power_mW",      powerMW,   "mW"));
    return out;
}

} // namespace mcp
