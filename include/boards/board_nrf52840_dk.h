#pragma once
// ─── Nordic nRF52840-DK ──────────────────────────────────────────────────────
// Official Nordic Semiconductor development kit for nRF52840 SoC.
// ARM Cortex-M4F @ 64 MHz, 1 MB flash, 256 KB RAM, BLE 5.0 + 802.15.4.
// J-Link OB debugger onboard, Arduino-compatible header, USB-C.
// https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK
//
// Platform: nordicnrf52   Board ID: nordic_pca10056
//
// ⚠ No built-in WiFi.  WiFi-dependent features (WebSocket MCP server, mDNS,
//   UDP broadcast) are NOT compiled for this target.  This build provides:
//     • I2C sensor acquisition (SensorManager, I2CSensors)
//     • Metrics collection (MetricsSystem)
//     • Serial output of sensor readings
//   For full MCP-over-WiFi, attach an external ESP32-C3 in AT-command mode
//   to UART1 (TX=6 / RX=8) and bridge the JSON-RPC stream.

#define BOARD_NAME          "Nordic nRF52840-DK"
#define BOARD_VARIANT       "nrf52840"
#define BOARD_HAS_WIFI      0
#define BOARD_HAS_BLE       1   // Bluetooth 5.0 LE + 802.15.4
#define BOARD_HAS_CAN       0

// ── I2C ──────────────────────────────────────────────────────────────────────
// Arduino Wire on nRF52 uses the pins defined by the board variant.
// nRF52840-DK default: P0.26 = SDA, P0.27 = SCL.
// Do NOT define SDA/SCL as macros here — nrf52.h has struct members with
// those names and the preprocessor would clobber them.
#define BOARD_I2C_SDA       26
#define BOARD_I2C_SCL       27
#define BOARD_I2C_FREQ      400000UL

// ── UART ─────────────────────────────────────────────────────────────────────
// Serial is USB CDC (no physical pins needed).
// Serial1 is on the Arduino header; P0.06 = TX, P0.08 = RX.
// TX and RX are also struct-member names in the nRF52 SDK; define only the
// BOARD_ aliases to avoid clashes.
#define BOARD_UART_TX       6    // P0.06
#define BOARD_UART_RX       8    // P0.08
#define BOARD_UART1_TX      6
#define BOARD_UART1_RX      8

// ── Status LED ───────────────────────────────────────────────────────────────
// LED1–LED4 are active LOW on the nRF52840-DK.
#define BOARD_LED_PIN       13   // LED1 = P0.13
#define BOARD_LED_RGB       0
#define BOARD_LED_ACTIVE    LOW

// ── Misc ─────────────────────────────────────────────────────────────────────
#define BOARD_SERIAL_BAUD   115200
#define BOARD_MCP_PORT      9000   // reserved for future WiFi-module bridge
#define BOARD_HTTP_PORT     80
