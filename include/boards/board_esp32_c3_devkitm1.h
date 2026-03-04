#pragma once
// ─── ESP32-C3-DevKitM-1 ──────────────────────────────────────────────────────
// Espressif RISC-V based ESP32-C3 development board — low-cost, low-power IoT.
// Single-core RISC-V @ 160 MHz, 4 MB flash, 2.4 GHz WiFi + BLE 5.0.
// Native USB-Serial/JTAG; no external UART bridge chip required.
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/hw-reference/esp32c3/user-guide-devkitm-1.html

#define BOARD_NAME          "ESP32-C3-DevKitM-1"
#define BOARD_VARIANT       "esp32c3"
#define BOARD_HAS_WIFI      1
#define BOARD_HAS_BLE       1   // BLE 5.0 (no Classic BT)
#define BOARD_HAS_CAN       1   // TWAI

// ── I2C ──────────────────────────────────────────────────────────────────────
// NOTE: GPIO8 carries the onboard WS2812B RGB LED on this board.
// GPIO5/GPIO6 are chosen to avoid that conflict.
#define BOARD_I2C_SDA       5
#define BOARD_I2C_SCL       6
#define BOARD_I2C_FREQ      400000UL

// ── UART ─────────────────────────────────────────────────────────────────────
// UART0 is routed through the built-in USB Serial/JTAG peripheral.
// UART1 on GPIO7/GPIO10 for external serial devices.
#define BOARD_UART_TX       21   // USB-Serial/JTAG
#define BOARD_UART_RX       20
#define BOARD_UART1_TX      7
#define BOARD_UART1_RX      10

// ── Status LED ───────────────────────────────────────────────────────────────
#define BOARD_LED_PIN       8    // WS2812B RGB LED
#define BOARD_LED_RGB       1

// ── CAN / TWAI ───────────────────────────────────────────────────────────────
#define BOARD_CAN_TX        3
#define BOARD_CAN_RX        2

// ── Misc ─────────────────────────────────────────────────────────────────────
#define BOARD_SERIAL_BAUD   115200
#define BOARD_MCP_PORT      9000
#define BOARD_HTTP_PORT     80
