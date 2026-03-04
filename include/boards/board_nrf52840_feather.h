#pragma once
// ─── Adafruit Feather nRF52840 ────────────────────────────────────────────────
// Feather form-factor board with nRF52840, USB-C, JST LiPo connector,
// and onboard charger.  BLE 5.0 + 802.15.4.  Compatible with FeatherWings.
// https://learn.adafruit.com/introducing-the-adafruit-nrf52840-feather
//
// Platform: nordicnrf52   Board ID: adafruit_feather_nrf52840
//
// ⚠ No built-in WiFi.  See board_nrf52840_dk.h for full capability notes.

#define BOARD_NAME          "Adafruit Feather nRF52840"
#define BOARD_VARIANT       "nrf52840"
#define BOARD_HAS_WIFI      0
#define BOARD_HAS_BLE       1   // Bluetooth 5.0 LE
#define BOARD_HAS_CAN       0

// ── I2C ──────────────────────────────────────────────────────────────────────
// SDA and SCL are labelled on the Feather silkscreen.
// Adafruit nRF52 Arduino core maps: SDA = P0.12, SCL = P0.11.
// The SDA/SCL macros are defined by the Adafruit nRF52 platform variant.
#ifndef SDA
#  define SDA 25   // Adafruit nRF52 Arduino pin 25 → P0.12
#endif
#ifndef SCL
#  define SCL 26   // Adafruit nRF52 Arduino pin 26 → P0.11
#endif
#define BOARD_I2C_SDA       SDA
#define BOARD_I2C_SCL       SCL
#define BOARD_I2C_FREQ      400000UL

// ── UART ─────────────────────────────────────────────────────────────────────
// Serial is USB CDC.  Serial1 maps to the Feather TX/RX pads.
#ifndef TX
#  define TX 25   // P0.25
#endif
#ifndef RX
#  define RX 24   // P0.24
#endif
#define BOARD_UART_TX       TX
#define BOARD_UART_RX       RX
#define BOARD_UART1_TX      TX
#define BOARD_UART1_RX      RX

// ── Status LEDs ──────────────────────────────────────────────────────────────
// Red LED and Blue LED both active LOW.
#define BOARD_LED_PIN       LED_BLUE   // provided by Adafruit nRF52 core
#define BOARD_LED_RGB       0
#define BOARD_LED_ACTIVE    LOW

// ── Misc ─────────────────────────────────────────────────────────────────────
#define BOARD_SERIAL_BAUD   115200
#define BOARD_MCP_PORT      9000
#define BOARD_HTTP_PORT     80
