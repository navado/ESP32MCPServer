# Adafruit HUZZAH32 ESP32 Feather

## Overview

| Item | Value |
|------|-------|
| Chip | ESP32 (dual-core Xtensa LX6 @ 240 MHz) |
| Flash | 4 MB |
| PSRAM | None |
| Connectivity | 2.4 GHz WiFi 4 + Bluetooth 4.2 + BLE |
| USB | Micro-USB via CP2104 UART bridge |
| LiPo charging | Yes — JST 2-pin connector with onboard MCP73831 charger |
| Form factor | Adafruit Feather (compatible with FeatherWings) |
| MCP transport | Full (WebSocket + mDNS + UDP discovery) |
| PlatformIO env | `adafruit-huzzah32` |
| Where to buy | [Adafruit #3405](https://www.adafruit.com/product/3405), Amazon, Digi-Key |

## Pinout Reference

```
             Adafruit HUZZAH32 ESP32 Feather
          ┌────────────────────────────────┐
  RST ─── │ RST                  BAT  │ ── LiPo+
  3V3 ─── │ 3V3                  EN   │
  NC  ─── │ NC                   USB  │ ── 5V USB
  GND ─── │ GND                  GND  │ ── GND
  A0  ─── │ GPIO26               GPIO23│ ── I2C SDA
  A1  ─── │ GPIO25               GPIO22│ ── I2C SCL
  A2  ─── │ GPIO34 (in)          GPIO20│
  A3  ─── │ GPIO39 (in)          GPIO21│
  A4  ─── │ GPIO36 (in)          GPIO17│ ── UART1 TX
  A5  ─── │ GPIO4  (CAN RX)      GPIO16│ ── UART1 RX
  SCK ─── │ GPIO5  (CAN TX)      GPIO27│
  MO  ─── │ GPIO18               GPIO33│
  MI  ─── │ GPIO19               GPIO15│
  RX  ─── │ GPIO3 (RX0)          GPIO32│
  TX  ─── │ GPIO1 (TX0)          GPIO14│
  SDA ─── │ GPIO23               GPIO13│ ── Red LED
  SCL ─── │ GPIO22               GPIO12│
          └────────────────────────────┘
              Battery: JST (bottom left)
```

> Full pinout: https://learn.adafruit.com/adafruit-huzzah32-esp32-feather/pinouts

## I2C Sensor Wiring

SDA and SCL pins are labelled on the board silkscreen.

```
Adafruit HUZZAH32            I2C Sensor
─────────────────            ──────────
3V3  ───────────────────────  VCC / VIN
GND  ───────────────────────  GND
GPIO23 / SDA ───────────────  SDA
GPIO22 / SCL ───────────────  SCL
```

### Supported Sensors

| Sensor | Default I2C Address | Address Select |
|--------|--------------------|-|
| BME280 (temp / humidity / pressure) | 0x76 | SDO → GND = 0x76, SDO → 3V3 = 0x77 |
| BMP280 (temp / pressure) | 0x76 | SDO → GND = 0x76, SDO → 3V3 = 0x77 |
| MPU-6050 (accel / gyro) | 0x68 | AD0 → GND = 0x68, AD0 → 3V3 = 0x69 |
| ADS1115 (16-bit ADC) | 0x48 | ADDR: GND=0x48, VCC=0x49, SDA=0x4A, SCL=0x4B |
| SHT31 (temp / humidity) | 0x44 | ADDR → GND = 0x44, ADDR → 3V3 = 0x45 |
| BH1750 (ambient light) | 0x23 | ADDR → GND = 0x23, ADDR → 3V3 = 0x5C |
| INA219 (current / voltage) | 0x40 | A0/A1 → GND = 0x40 … see datasheet |

## CAN Bus Wiring (optional)

```
Adafruit HUZZAH32            SN65HVD230 transceiver        CAN Bus
─────────────────            ──────────────────────         ───────
3V3  ───────────────────────  VCC (3.3 V)
GND  ───────────────────────  GND
GPIO5 (CAN TX / SCK) ───────  TXD
GPIO4 (CAN RX / A5)  ───────  RXD
                               CANH ─────────────────────── CANH
                               CANL ─────────────────────── CANL
```

> GPIO5 and GPIO4 are shared with the SPI SCK and A5 labels; do not use SPI
> at the same time as CAN.

## UART Serial Wiring (NMEA 0183 / GPS)

```
Adafruit HUZZAH32            Serial Device
─────────────────            ─────────────
GND  ───────────────────────  GND
GPIO17 (UART1 TX) ───────────  RX
GPIO16 (UART1 RX) ───────────  TX
```

## Battery Monitor

Read the approximate LiPo battery voltage with:

```cpp
int raw = analogRead(BOARD_VBAT_PIN);          // 12-bit ADC
float v = raw * 2.0f * 3.3f / 4095.0f;        // ÷2 voltage divider
```

The `BOARD_VBAT_PIN` macro resolves to GPIO35 (labelled A13 on the board).

## Build Configuration

```ini
[env:adafruit-huzzah32]
platform  = espressif32
board     = featheresp32
framework = arduino
build_flags =
    -std=gnu++17
    -D BOARD_ADAFRUIT_HUZZAH32
```

## Notes

- Use the **3V3** pin to power sensors — never 5V (no 5V regulator output on
  the Feather header).
- The red **LED** on GPIO13 is shared with the SPI MISO pin; avoid SPI devices
  if using the LED as a status indicator.
- GPIO34, GPIO36, GPIO39 are **input-only** (no internal pull-up/down).
