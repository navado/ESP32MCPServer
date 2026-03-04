# ESP32-C3-DevKitM-1

## Overview

| Item | Value |
|------|-------|
| Chip | ESP32-C3 (single-core RISC-V @ 160 MHz) |
| Flash | 4 MB |
| PSRAM | None |
| Connectivity | 2.4 GHz WiFi 4 + BLE 5.0 (no Classic BT) |
| USB | USB-C via built-in USB Serial/JTAG (no bridge chip) |
| MCP transport | Full (WebSocket + mDNS + UDP discovery) |
| PlatformIO env | `esp32-c3-devkitm-1` |
| Where to buy | Espressif Store, Mouser, DigiKey, Amazon |

The ESP32-C3 is a cost-optimised IoT SoC with a RISC-V core.  It is ideal for
battery-powered sensor nodes where processing requirements are modest.

## Pinout Reference

```
                  ESP32-C3-DevKitM-1
              ┌──────────────────────┐
  3.3 V ──── │ 3V3           5V     │ ──── 5V (USB)
  GND  ──── │ GND          GND     │ ──── GND
             │ GPIO0        GPIO10  │ ──── UART1 RX
             │ GPIO1        GPIO7   │ ──── UART1 TX
             │ GPIO2        GPIO6   │ ──── I2C SCL
             │ GPIO3 (CAN TX) GPIO5 │ ──── I2C SDA
             │ GPIO4 (CAN RX) GPIO8 │ ──── WS2812 LED
             │ GPIO20 (RX0) GPIO18  │ ──── USB D-
             │ GPIO21 (TX0) GPIO19  │ ──── USB D+
              └──────────────────────┘
```

> Full pinout: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/hw-reference/esp32c3/user-guide-devkitm-1.html

## I2C Sensor Wiring

> **Note**: GPIO8 is used by the onboard WS2812B RGB LED.  The I2C bus is
> therefore placed on GPIO5 (SDA) / GPIO6 (SCL) to avoid the conflict.

```
ESP32-C3-DevKitM-1           I2C Sensor
──────────────────           ──────────
3V3  ───────────────────────  VCC / VIN
GND  ───────────────────────  GND
GPIO5 (SDA) ────────────────  SDA
GPIO6 (SCL) ────────────────  SCL
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
ESP32-C3-DevKitM-1           SN65HVD230 transceiver        CAN Bus
──────────────────           ──────────────────────         ───────
3V3  ───────────────────────  VCC (3.3 V)
GND  ───────────────────────  GND
GPIO3 (CAN TX) ──────────────  TXD
GPIO4 (CAN RX) ──────────────  RXD
                               CANH ─────────────────────── CANH
                               CANL ─────────────────────── CANL
```

## UART Serial Wiring (NMEA 0183 / GPS)

```
ESP32-C3-DevKitM-1           Serial Device
──────────────────           ─────────────
GND  ───────────────────────  GND
GPIO7 (UART1 TX) ────────────  RX
GPIO10 (UART1 RX) ───────────  TX
```

## Build Configuration

```ini
[env:esp32-c3-devkitm-1]
platform  = espressif32
board     = esp32-c3-devkitm-1
framework = arduino
build_flags =
    -std=gnu++17
    -D BOARD_ESP32_C3_DEVKITM1
```

## Notes

- **Single core**: The ESP32-C3 has only one CPU core.  The FreeRTOS task
  `xTaskCreatePinnedToCore(..., 1)` in `main.cpp` pins to core 1; on a
  single-core chip this silently falls back to core 0.  This is harmless.
- **No Classic Bluetooth**: Only BLE 5.0 is available.
- **Strapping pins**: GPIO2, GPIO8, GPIO9 affect boot mode.  GPIO8 is already
  the LED pin; do not pull it LOW externally.
