#pragma once
// ─── Adafruit HUZZAH32 ESP32 Feather ─────────────────────────────────────────
// Feather form-factor board with ESP32 module, JST LiPo connector and charger.
// Compatible with Feather Wings.  CP2104 USB-UART bridge.
// https://learn.adafruit.com/adafruit-huzzah32-esp32-feather

#define BOARD_NAME          "Adafruit HUZZAH32 ESP32 Feather"
#define BOARD_VARIANT       "esp32"
#define BOARD_HAS_WIFI      1
#define BOARD_HAS_BLE       1
#define BOARD_HAS_CAN       1   // TWAI via external transceiver

// ── I2C ──────────────────────────────────────────────────────────────────────
// SDA and SCL are clearly labelled on the board silkscreen.
#define BOARD_I2C_SDA       23
#define BOARD_I2C_SCL       22
#define BOARD_I2C_FREQ      400000UL

// ── UART ─────────────────────────────────────────────────────────────────────
#define BOARD_UART_TX       1    // USB-UART via CP2104
#define BOARD_UART_RX       3
#define BOARD_UART1_TX      17
#define BOARD_UART1_RX      16

// ── Status LED ───────────────────────────────────────────────────────────────
#define BOARD_LED_PIN       13   // Red LED, active HIGH
#define BOARD_LED_RGB       0

// ── Battery monitor ──────────────────────────────────────────────────────────
// ADC pin connected to the LiPo battery through a voltage divider (÷2).
// Read voltage: analogRead(BOARD_VBAT_PIN) * 2 * 3.3 / 4095
#define BOARD_VBAT_PIN      35   // A13 / GPIO35

// ── CAN / TWAI ───────────────────────────────────────────────────────────────
#define BOARD_CAN_TX        4
#define BOARD_CAN_RX        5

// ── Misc ─────────────────────────────────────────────────────────────────────
#define BOARD_SERIAL_BAUD   115200
#define BOARD_MCP_PORT      9000
#define BOARD_HTTP_PORT     80
