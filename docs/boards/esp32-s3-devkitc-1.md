# ESP32-S3-DevKitC-1

## Overview

| Item | Value |
|------|-------|
| Chip | ESP32-S3 (dual-core Xtensa LX7 @ 240 MHz) |
| Flash | 8 MB (N8R8 variant) |
| PSRAM | 8 MB (N8R8 variant) |
| Connectivity | 2.4 GHz WiFi 4 (802.11 b/g/n) + BLE 5.0 |
| USB | Dual USB-C: USB JTAG/CDC + USB OTG |
| MCP transport | Full (WebSocket + mDNS + UDP discovery) |
| PlatformIO env | `esp32-s3-devkitc-1` |
| Where to buy | [Espressif Store](https://www.espressif.com/en/products/devkits), Mouser, DigiKey, Amazon |

## Pinout Reference

```
                    ESP32-S3-DevKitC-1
              ┌──────────────────────────┐
  3.3 V ──── │ 3V3               5V     │ ──── 5V (USB)
  GND  ──── │ GND              GND     │ ──── GND
             │ GPIO1            GPIO43  │ ──── UART0 TX
             │ GPIO2            GPIO44  │ ──── UART0 RX
             │ GPIO3            GPIO15  │
             │ GPIO4  (CAN TX)  GPIO16  │ ──── UART1 RX
             │ GPIO5  (CAN RX)  GPIO17  │ ──── UART1 TX
             │ GPIO6            GPIO18  │
             │ GPIO7            GPIO8   │ ──── I2C SDA
             │ GPIO21           GPIO9   │ ──── I2C SCL
             │                  GPIO48  │ ──── WS2812 LED
              └──────────────────────────┘
```

> Full pinout: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html

## I2C Sensor Wiring

All supported sensors share the same two-wire I2C bus (SDA = GPIO8, SCL = GPIO9).
Power all 3.3 V sensors from the board's **3V3** pin.

```
ESP32-S3-DevKitC-1          I2C Sensor
──────────────────          ──────────
3V3  ──────────────────────  VCC / VIN
GND  ──────────────────────  GND
GPIO8 (SDA) ───────────────  SDA
GPIO9 (SCL) ───────────────  SCL
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
| INA219 (current / voltage) | 0x40 | A0/A1 → GND = 0x40 … see datasheet for others |

Multiple sensors can be connected simultaneously using the same SDA/SCL lines,
provided each sensor has a unique address.

## CAN Bus Wiring (optional)

Requires a 3.3 V-compatible CAN transceiver such as the **SN65HVD230** or
**TJA1051T/3**.  Do **not** use 5 V-only transceivers (e.g. MCP2551) directly.

```
ESP32-S3-DevKitC-1          SN65HVD230 transceiver        CAN Bus
──────────────────          ──────────────────────         ───────
3V3  ──────────────────────  VCC (3.3 V)
GND  ──────────────────────  GND
GPIO4 (CAN TX) ─────────────  TXD
GPIO5 (CAN RX) ─────────────  RXD
                              CANH ─────────────────────── CANH
                              CANL ─────────────────────── CANL
```

Add 120 Ω termination resistors at each end of the CAN bus if the cable is
longer than ~0.5 m.

## UART Serial Wiring (NMEA 0183 / GPS)

```
ESP32-S3-DevKitC-1          Serial Device (e.g. GPS module)
──────────────────          ──────────────────────────────
GND  ──────────────────────  GND
GPIO17 (UART1 TX) ──────────  RX
GPIO16 (UART1 RX) ──────────  TX
```

## Build Configuration

```ini
# platformio.ini
[env:esp32-s3-devkitc-1]
platform  = espressif32
board     = esp32-s3-devkitc-1
framework = arduino
build_flags =
    -std=gnu++17
    -D BOARD_ESP32_S3_DEVKITC1
```

## Notes

- **LED**: GPIO48 drives a WS2812B RGB LED (not a plain GPIO).  Use the
  `Adafruit_NeoPixel` or FastLED library to control it.
- **Strapping pins**: GPIO0, GPIO3, GPIO45, GPIO46 have special boot functions.
  Avoid using them unless you understand the implications.
- **PSRAM**: The N8R8 variant has 8 MB PSRAM wired internally via Octal-SPI —
  no user pins are consumed.
