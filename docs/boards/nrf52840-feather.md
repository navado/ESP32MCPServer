# Adafruit Feather nRF52840

## Overview

| Item | Value |
|------|-------|
| Chip | nRF52840 (ARM Cortex-M4F @ 64 MHz) |
| Flash | 1 MB |
| RAM | 256 KB |
| Connectivity | Bluetooth 5.0 LE + 802.15.4 |
| USB | USB-C (native USB CDC via nRF52840; no bridge chip) |
| LiPo charging | Yes — JST 2-pin connector with onboard charger |
| Form factor | Adafruit Feather (compatible with FeatherWings) |
| MCP transport | **Sensor-only** — no built-in WiFi |
| PlatformIO env | `nrf52840-feather` |
| Where to buy | [Adafruit #4062](https://www.adafruit.com/product/4062), Amazon, Digi-Key |

### WiFi Limitations

See [nrf52840-dk.md](nrf52840-dk.md) for the WiFi bridging options.  This board
uses the same nRF52840 SoC, so the same workarounds apply.

## Pinout Reference

```
              Adafruit Feather nRF52840
          ┌─────────────────────────────┐
  RST ─── │ RST                  BAT  │ ── LiPo+
  3V3 ─── │ 3V3                  EN   │
  GND ─── │ GND                  USB  │ ── 5V USB
  A0  ─── │ P0.04 / A0           GND  │ ── GND
  A1  ─── │ P0.05 / A1           P1.02│
  A2  ─── │ P0.30 / A2           P1.01│
  A3  ─── │ P0.28 / A3           P0.27│
  A4  ─── │ P0.02 / A4           P0.26│
  A5  ─── │ P0.03 / A5           P0.06│
  SCK ─── │ P0.14                P0.25│ ── UART TX
  MO  ─── │ P0.13                P0.24│ ── UART RX
  MI  ─── │ P0.15                P0.12│ ── I2C SDA
  RX  ─── │ P0.24 (RX)           P0.11│ ── I2C SCL
  TX  ─── │ P0.25 (TX)           P1.10│ ── Blue LED
  SDA ─── │ P0.12 (SDA)          P1.09│ ── Red LED
  SCL ─── │ P0.11 (SCL)               │
          └─────────────────────────────┘
```

> Full pinout: https://learn.adafruit.com/introducing-the-adafruit-nrf52840-feather/pinouts

## I2C Sensor Wiring

SDA and SCL are clearly labelled on the Feather silkscreen; a STEMMA QT /
Qwiic compatible header is also available on some revisions.

```
Adafruit nRF52840 Feather     I2C Sensor
────────────────────────      ──────────
3V3  ─────────────────────── VCC / VIN
GND  ─────────────────────── GND
P0.12 / SDA ─────────────── SDA
P0.11 / SCL ─────────────── SCL
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

## Serial Output

Sensor readings are printed to USB CDC Serial at 115200 baud:

```
pio device monitor -e nrf52840-feather
```

## Build Configuration

```ini
[env:nrf52840-feather]
platform  = nordicnrf52
board     = adafruit_feather_nrf52840
framework = arduino
build_flags =
    -std=gnu++17
    -D BOARD_NRF52840_FEATHER
```

## Notes

- **LED polarity**: Both the red LED (P1.09) and blue LED (P1.10) are
  **active LOW**.  `LED_RED` and `LED_BLUE` are defined by the Adafruit nRF52
  Arduino core.
- **Battery**: Use the `analogRead(PIN_VBAT)` with the `VBAT_MV_PER_LSB`
  constant from the Adafruit BSP to read battery voltage.
- **Bootloader**: The board ships with the Adafruit nRF52 USB DFU bootloader.
  PlatformIO uploads over USB automatically.
- **SoftDevice**: The Adafruit BSP includes SoftDevice S140 for BLE support;
  it is loaded automatically when using `nordicnrf52` platform.
