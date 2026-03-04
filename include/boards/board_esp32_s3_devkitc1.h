#pragma once
// ─── ESP32-S3-DevKitC-1 ──────────────────────────────────────────────────────
// Espressif official development board with ESP32-S3-WROOM-1 module.
// Dual-core Xtensa LX7 @ 240 MHz, 8 MB PSRAM (N8R8 variant), 16 MB flash,
// native USB (OTG + JTAG/CDC), 2.4 GHz WiFi + BLE 5.0.
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html

#define BOARD_NAME          "ESP32-S3-DevKitC-1"
#define BOARD_VARIANT       "esp32s3"
#define BOARD_HAS_WIFI      1
#define BOARD_HAS_BLE       1
#define BOARD_HAS_CAN       1   // TWAI peripheral (ISO 11898-1 compatible)

// ── I2C (Arduino Wire defaults for ESP32-S3) ─────────────────────────────────
#define BOARD_I2C_SDA       8
#define BOARD_I2C_SCL       9
#define BOARD_I2C_FREQ      400000UL

// ── UART ─────────────────────────────────────────────────────────────────────
// UART0 is routed through the on-chip USB JTAG/CDC bridge (no physical pins).
// UART1 is available on the GPIO header for external bus devices (NMEA, etc.).
#define BOARD_UART_TX       43   // USB-CDC / programming port
#define BOARD_UART_RX       44
#define BOARD_UART1_TX      17   // External serial (NMEA 0183, GPS, etc.)
#define BOARD_UART1_RX      16

// ── Status LED ───────────────────────────────────────────────────────────────
// DevKitC-1 v1.4+ has a WS2812B RGB LED on GPIO48. Earlier revisions expose
// a plain blue LED on GPIO2.  Set BOARD_LED_RGB=1 when using the WS2812.
#define BOARD_LED_PIN       48
#define BOARD_LED_RGB       1    // 1 = addressable WS2812B, 0 = plain GPIO

// ── CAN / TWAI ───────────────────────────────────────────────────────────────
// Requires an external 3.3 V CAN transceiver (e.g. SN65HVD230, TJA1050-3V3).
#define BOARD_CAN_TX        4
#define BOARD_CAN_RX        5

// ── Misc ─────────────────────────────────────────────────────────────────────
#define BOARD_SERIAL_BAUD   115200
#define BOARD_MCP_PORT      9000
#define BOARD_HTTP_PORT     80
