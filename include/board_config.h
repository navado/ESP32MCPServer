#pragma once
// ─── Board Configuration Selector ────────────────────────────────────────────
// Selects the correct per-board pin/capability header based on the BOARD_*
// macro defined by the PlatformIO build_flags for the active environment.
//
// Usage — add exactly one of the following to the relevant [env] in
// platformio.ini (already done; shown here for reference only):
//
//   -D BOARD_ESP32_S3_DEVKITC1      ESP32-S3-DevKitC-1 (default)
//   -D BOARD_ESP32_DEVKITC          ESP32 DevKitC V4
//   -D BOARD_ESP32_C3_DEVKITM1      ESP32-C3-DevKitM-1
//   -D BOARD_ADAFRUIT_HUZZAH32      Adafruit HUZZAH32 ESP32 Feather
//   -D BOARD_M5STACK_CORE           M5Stack Core ESP32
//   -D BOARD_NRF52840_DK            Nordic nRF52840-DK
//   -D BOARD_NRF52840_FEATHER       Adafruit Feather nRF52840
//
// Each header defines:
//   BOARD_NAME, BOARD_VARIANT, BOARD_HAS_WIFI, BOARD_HAS_BLE, BOARD_HAS_CAN
//   BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ
//   BOARD_UART_TX, BOARD_UART_RX, BOARD_UART1_TX, BOARD_UART1_RX
//   BOARD_LED_PIN, BOARD_LED_RGB
//   BOARD_CAN_TX, BOARD_CAN_RX  (ESP32 only)
//   BOARD_SERIAL_BAUD, BOARD_MCP_PORT, BOARD_HTTP_PORT

#if defined(BOARD_ESP32_S3_DEVKITC1)
#  include "boards/board_esp32_s3_devkitc1.h"
#elif defined(BOARD_ESP32_DEVKITC)
#  include "boards/board_esp32_devkitc.h"
#elif defined(BOARD_ESP32_C3_DEVKITM1)
#  include "boards/board_esp32_c3_devkitm1.h"
#elif defined(BOARD_ADAFRUIT_HUZZAH32)
#  include "boards/board_adafruit_huzzah32.h"
#elif defined(BOARD_M5STACK_CORE)
#  include "boards/board_m5stack_core.h"
#elif defined(BOARD_NRF52840_DK)
#  include "boards/board_nrf52840_dk.h"
#elif defined(BOARD_NRF52840_FEATHER)
#  include "boards/board_nrf52840_feather.h"
#else
// ── Default: ESP32-S3-DevKitC-1 ─────────────────────────────────────────────
// No BOARD_* flag was set; fall back to the original target for backwards
// compatibility with existing builds that predate per-board configs.
#  include "boards/board_esp32_s3_devkitc1.h"
#  define BOARD_ESP32_S3_DEVKITC1   // set so downstream #ifdef checks work
#endif
