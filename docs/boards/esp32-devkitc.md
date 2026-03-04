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
[env:esp32-devkitc]
platform  = espressif32
board     = esp32dev
framework = arduino
build_flags =
    -std=gnu++17
    -D BOARD_ESP32_DEVKITC
```

## Notes

- **LED**: GPIO2 is connected to a plain blue LED (active HIGH) on most DevKitC
  boards.
- **GPIO12**: This is a strapping pin — it must be LOW at boot on WROOM-32
  modules (already pulled low on the DevKitC board).
- **Flash mode**: The WROVER variant allows more RAM-intensive workloads thanks
  to 8 MB PSRAM; useful if expanding the metrics history ring buffer.
