# ESP32 DevKitC V4

## Overview

| Item | Value |
|------|-------|
| Chip | ESP32 (dual-core Xtensa LX6 @ 240 MHz) |
| Flash | 4 MB (WROOM-32) or 16 MB (WROVER) |
| PSRAM | None (WROOM-32) / 8 MB (WROVER) |
| Connectivity | 2.4 GHz WiFi 4 + Bluetooth 4.2 Classic + BLE |
| USB | Micro-USB via CP2102/CH340 UART bridge |
| MCP transport | Full (WebSocket + mDNS + UDP discovery) |
| BLE via MCP (`ble/*`) | Not available — compiled out: the BLE stack does not fit the 4 MB flash budget alongside the WebSocket transport and OTA (the radio itself supports BLE) |
| PlatformIO env | `esp32-devkitc` |
| Where to buy | Amazon, AliExpress, Adafruit, SparkFun, Mouser |

The classic ESP32 DevKitC is the most widely cloned ESP32 board.  Variants from
AZ-Delivery, DOIT, Espressif, and many other vendors are all pin-compatible.

## Pinout Reference

```
                    ESP32 DevKitC V4 (38-pin)
              ┌──────────────────────────┐
  3.3 V ──── │ 3V3               5V     │ ──── 5V (USB)
  GND  ──── │ GND              GND     │ ──── GND
             │ GPIO1  (TX0) ←   GPIO36  │
             │ GPIO3  (RX0) →   GPIO39  │
             │ GPIO21 (SDA) ─── GPIO34  │
             │ GPIO22 (SCL) ─── GPIO35  │
             │                  GPIO32  │
             │ GPIO17 (TX2) ←   GPIO33  │
             │ GPIO16 (RX2) →   GPIO25  │
             │ GPIO5  (CAN TX)  GPIO26  │
             │ GPIO4  (CAN RX)  GPIO27  │
             │                  GPIO14  │
             │                  GPIO12  │
             │ GPIO2  (LED)     GPIO13  │
              └──────────────────────────┘
```

> Full pinout: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/hw-reference/esp32/get-started-devkitc.html

## I2C Sensor Wiring

```
ESP32 DevKitC                I2C Sensor
─────────────                ──────────
3V3  ──────────────────────  VCC / VIN
GND  ──────────────────────  GND
GPIO21 (SDA) ───────────────  SDA
GPIO22 (SCL) ───────────────  SCL
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
ESP32 DevKitC               SN65HVD230 transceiver        CAN Bus
─────────────               ──────────────────────         ───────
3V3  ──────────────────────  VCC (3.3 V)
GND  ──────────────────────  GND
GPIO5 (CAN TX) ─────────────  TXD
GPIO4 (CAN RX) ─────────────  RXD
                              CANH ─────────────────────── CANH
                              CANL ─────────────────────── CANL
```

## UART Serial Wiring (NMEA 0183 / GPS)

```
ESP32 DevKitC               Serial Device
─────────────               ─────────────
GND  ──────────────────────  GND
GPIO17 (UART2 TX) ──────────  RX
GPIO16 (UART2 RX) ──────────  TX
```

## Build Configuration

```ini
# platformio.ini (abridged)
[env:esp32-devkitc]
extends = esp32_base
board   = esp32dev
board_build.partitions = partitions_4mb_ota.csv
build_src_filter =
    ${esp32_base.build_src_filter}
    -<BLEScanner.cpp>
build_flags =
    ${esp32_base.build_flags}
    -D BOARD_ESP32_DEVKITC
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
> OTA, so flash **over USB** once (`pio run -e esp32-devkitc -t upload`, which writes the bootloader,
> partition table and app).  The filesystem moves and shrinks (previously 1.4 MB at
> 0x291000), so its contents are lost: re-upload the web UI with
> `pio run -e esp32-devkitc -t uploadfs`.  NVS is unchanged, so WiFi credentials, the OTA
> password and MCP tokens are kept.

## Notes

- **LED**: GPIO2 is connected to a plain blue LED (active HIGH) on most DevKitC
  boards.
- **GPIO12**: This is a strapping pin — it must be LOW at boot on WROOM-32
  modules (already pulled low on the DevKitC board).
- **Flash mode**: The WROVER variant allows more RAM-intensive workloads thanks
  to 8 MB PSRAM; useful if expanding the metrics history ring buffer.

## Security

MCP authentication is off by default; type `auth on` on the serial console to require an access token (it is generated and printed; `token show` prints it again). OTA uses a random per-device password printed at first boot (`ota show`). **There is no TLS on this board:** the token and data travel in clear text, so use it on a trusted network. See [Security](../../README.md#security).
