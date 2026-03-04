#pragma once
// ─── ESP32 DevKitC V4 ────────────────────────────────────────────────────────
// Espressif classic development board with ESP32-WROOM-32 module.
// Dual-core Xtensa LX6 @ 240 MHz, 4 MB flash, 2.4 GHz WiFi + BT/BLE.
// Most widely available ESP32 form factor; sold under many vendor names.
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/hw-reference/esp32/get-started-devkitc.html

#define BOARD_NAME          "ESP32 DevKitC V4"
#define BOARD_VARIANT       "esp32"
#define BOARD_HAS_WIFI      1
#define BOARD_HAS_BLE       1   // Bluetooth Classic 4.2 + BLE
#define BOARD_HAS_CAN       1   // TWAI peripheral

// ── I2C (Arduino Wire defaults for classic ESP32) ────────────────────────────
#define BOARD_I2C_SDA       21
#define BOARD_I2C_SCL       22
#define BOARD_I2C_FREQ      400000UL

// ── UART ─────────────────────────────────────────────────────────────────────
// UART0 is connected to the onboard CP2102/CH340 USB-UART bridge.
// UART2 is available for external peripherals.
#define BOARD_UART_TX       1    // UART0 TX (USB bridge)
#define BOARD_UART_RX       3    // UART0 RX (USB bridge)
#define BOARD_UART1_TX      17   // UART2 TX (external devices)
#define BOARD_UART1_RX      16   // UART2 RX

// ── Status LED ───────────────────────────────────────────────────────────────
#define BOARD_LED_PIN       2    // Blue LED, active HIGH
#define BOARD_LED_RGB       0

// ── CAN / TWAI ───────────────────────────────────────────────────────────────
#define BOARD_CAN_TX        5
#define BOARD_CAN_RX        4

// ── Misc ─────────────────────────────────────────────────────────────────────
#define BOARD_SERIAL_BAUD   115200
#define BOARD_MCP_PORT      9000
#define BOARD_HTTP_PORT     80
