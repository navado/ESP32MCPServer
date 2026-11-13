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
| BLE via MCP (`ble/*`) | Not available — compiled out: the BLE stack does not fit the 4 MB flash budget alongside the WebSocket transport and OTA (the radio itself supports BLE) |
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
# platformio.ini (abridged)
[env:adafruit-huzzah32]
extends = esp32_base
board   = featheresp32
board_build.partitions = partitions_4mb_ota.csv
build_src_filter =
    ${esp32_base.build_src_filter}
    -<BLEScanner.cpp>
build_flags =
    ${esp32_base.build_flags}
    -D BOARD_ADAFRUIT_HUZZAH32
```

All ESP32 envs extend `[esp32_base]`, which pins `platform = platformio/espressif32@6.9.0`
(Arduino core 2.0.17).  Do not use an unqualified `espressif32`: on machines with a
pioarduino / Arduino core 3.x platform installed it resolves to that platform, whose
`Network/NetworkManager.h` shadows this project's header and breaks the build.

### Flash layout and upgrading existing boards

This env uses `partitions_4mb_ota.csv` (the default 4 MB layout's 1.25 MB app slots are too
small for the WebSocket transport + OTA):

| Partition | Offset | Size | Contents |
|---|---|---|---|
| nvs | 0x9000 | 20 KB | WiFi credentials, OTA password, MCP tokens (same offset as before) |
| otadata | 0xE000 | 8 KB | OTA slot selection |
| app0 / app1 | 0x10000 / 0x1E0000 | 1.81 MB each | firmware (OTA A/B) |
| spiffs | 0x3B0000 | 128 KB | LittleFS: web UI, bus-history config, metrics |
| coredump | 0x3D0000 | 64 KB | crash dumps |

> **Upgrading a board that runs older firmware:** a partition table cannot be changed over
> OTA, so flash **over USB** once (`pio run -e adafruit-huzzah32 -t upload`, which writes the bootloader,
> partition table and app).  The filesystem moves and shrinks (previously 1.4 MB at
> 0x291000), so its contents are lost: re-upload the web UI with
> `pio run -e adafruit-huzzah32 -t uploadfs`.  NVS is unchanged, so WiFi credentials, the OTA
> password and MCP tokens are kept.

## Notes

- Use the **3V3** pin to power sensors — never 5V (no 5V regulator output on
  the Feather header).
- The red **LED** on GPIO13 is shared with the SPI MISO pin; avoid SPI devices
  if using the LED as a status indicator.
- GPIO34, GPIO36, GPIO39 are **input-only** (no internal pull-up/down).

## Security

The MCP WebSocket requires an access token, and OTA uses a random per-device password. Both are printed once on the serial console at first boot (`token show` / `ota show` to see them again). **There is no TLS on this board:** the token and data travel in clear text, so use it on a trusted network. See [Security](../../README.md#security).
