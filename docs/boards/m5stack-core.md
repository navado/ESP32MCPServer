# M5Stack Core ESP32

## Overview

| Item | Value |
|------|-------|
| Chip | ESP32 (dual-core Xtensa LX6 @ 240 MHz) |
| Flash | 16 MB |
| PSRAM | None (Basic) / 4 MB (Gray/Fire variants) |
| Connectivity | 2.4 GHz WiFi 4 + Bluetooth 4.2 + BLE |
| Display | 2" IPS LCD 320×240 (ILI9342C, SPI) |
| Sensors (internal) | 6-axis IMU (SH200Q or MPU6886 depending on variant) |
| Storage | MicroSD card slot (SPI, shared with LCD) |
| USB | USB-C via CP2104 UART bridge |
| Battery | 110 mAh LiPo with IP5306 management IC |
| MCP transport | Full (WebSocket + mDNS + UDP discovery) |
| PlatformIO env | `m5stack-core` |
| Where to buy | [M5Stack Official Store](https://shop.m5stack.com), Amazon, AliExpress |

## GPIO Conflict Map

Many GPIO pins are committed to internal hardware.  **Read this before wiring
external sensors.**

| GPIO | Internal function | Available for external use? |
|------|------------------|-----------------------------|
| 1 | UART0 TX (USB-CP2104) | Yes (debug only) |
| 3 | UART0 RX (USB-CP2104) | Yes (debug only) |
| 4 | SD card CS | No |
| 12 | SD MISO | No |
| 13 | Speaker output (DAC) | No |
| 14 | LCD DC | No |
| 15 | SD CLK | No |
| 16 | Grove Port C RX | Yes |
| 17 | Grove Port C TX | Yes |
| 18 | LCD CLK / SD CLK | No |
| 19 | SD MISO | No |
| 21 | I2C SDA (Grove A + IMU) | Yes — shared |
| 22 | I2C SCL (Grove A + IMU) | Yes — shared |
| 23 | LCD MOSI / SD MOSI | No |
| 25 | Speaker DAC | No |
| 26 | Grove Port B I/O | Yes |
| 27 | LCD RST | No |
| 33 | LCD CS / Grove A power | No |
| 34 | Microphone ADC | Input only |
| 35 | Battery ADC | Input only |
| 36 | Button C | Input only |
| 37 | Button B | Input only |
| 38 | Button A | Input only |
| 39 | IMU interrupt | Input only |

## I2C Sensor Wiring

External I2C sensors connect to **Grove Port A** (the red 4-pin HY2.0 socket
on the side of the unit).  The bus is shared with the onboard IMU, so address
conflicts with the IMU (0x68 / 0x6C) must be avoided.

```
M5Stack Core                 I2C Sensor            Grove Port A
────────────                 ──────────             ────────────
3V3  ───────────────────────  VCC / VIN  ──────────  pin 1 (red)
GND  ───────────────────────  GND        ──────────  pin 2 (black)
GPIO21 (SDA) ───────────────  SDA        ──────────  pin 3 (yellow)
GPIO22 (SCL) ───────────────  SCL        ──────────  pin 4 (white)
```

You can use a standard HY2.0 4-pin → Dupont adapter (widely sold) to connect
breakout boards directly to Grove Port A.

### Supported Sensors

| Sensor | Default I2C Address | Address Select | IMU conflict? |
|--------|--------------------|-|-----|
| BME280 | 0x76 | SDO → GND = 0x76 | No |
| BMP280 | 0x76 | SDO → GND = 0x76 | No |
| MPU-6050 | 0x68 | AD0 → GND = 0x68 | **Yes** (0x68 = IMU address) |
| ADS1115 | 0x48 | — | No |
| SHT31 | 0x44 | — | No |
| BH1750 | 0x23 | — | No |
| INA219 | 0x40 | — | No |

> If MPU-6050 is needed, set AD0 → 3V3 (address 0x69) to avoid conflict with
> the onboard IMU.

## UART Serial Wiring (NMEA 0183 / GPS)

Connect to **Grove Port C** (blue connector, bottom face).

```
M5Stack Core                 Serial Device         Grove Port C
────────────                 ─────────────          ────────────
GND  ───────────────────────  GND        ──────────  pin 2 (black)
GPIO17 (TX) ────────────────  RX         ──────────  pin 3 (yellow)
GPIO16 (RX) ────────────────  TX         ──────────  pin 4 (white)
```

## CAN Bus Wiring

CAN is not natively accessible on M5Stack's standard Grove ports; GPIO5 and
GPIO4 (typical TWAI pins) are not broken out.  Use a custom connector or the
M5Stack Bus expansion headers on the bottom.

## Build Configuration

```ini
[env:m5stack-core]
platform  = espressif32
board     = m5stack-core-esp32
framework = arduino
build_flags =
    -std=gnu++17
    -D BOARD_M5STACK_CORE
```

## Notes

- **Display**: The LCD uses the ILI9342C SPI driver.  If you add display
  support, use the `M5Stack` or `M5Unified` library; do not share the SPI bus
  with external devices.
- **Power button**: Hold for 6 seconds to power off.  A short press wakes from
  deep sleep.
- **Basic vs Gray/Fire**: The Gray variant adds PSRAM; the Fire uses a different
  power management IC.  All three share the same GPIO map for I2C and UART.
