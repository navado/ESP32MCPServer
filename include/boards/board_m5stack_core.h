#pragma once
// ─── M5Stack Core ESP32 ──────────────────────────────────────────────────────
// All-in-one ESP32 module with 2" IPS LCD, IMU, speaker, micro-SD, and
// 3× Grove ports (A/B/C).  USB-C charging with onboard LiPo management.
// https://docs.m5stack.com/en/core/basic_v2.6
//
// ⚠ Several GPIO pins are used internally by the LCD, SD card, and speaker.
//   See the GPIO conflict table in docs/boards/m5stack-core.md before
//   assigning any new peripherals.

#define BOARD_NAME          "M5Stack Core ESP32"
#define BOARD_VARIANT       "esp32"
#define BOARD_HAS_WIFI      1
#define BOARD_HAS_BLE       1
#define BOARD_HAS_CAN       0   // No CAN on standard I/O; requires custom cable

// ── I2C ──────────────────────────────────────────────────────────────────────
// Grove Port A (red connector on the side) is the primary user I2C port.
// Internal peripherals (IMU SH200Q/MPU6886) also share this bus.
#define BOARD_I2C_SDA       21
#define BOARD_I2C_SCL       22
#define BOARD_I2C_FREQ      400000UL

// ── UART ─────────────────────────────────────────────────────────────────────
// Grove Port C (blue, bottom) is UART.  Port A overrides if I2C is in use.
#define BOARD_UART_TX       1    // USB-UART via CP2104
#define BOARD_UART_RX       3
#define BOARD_UART1_TX      17   // Grove Port C TX
#define BOARD_UART1_RX      16   // Grove Port C RX

// ── Status LED ───────────────────────────────────────────────────────────────
// No simple user LED; use the LCD or the small power RGB on the side.
#define BOARD_LED_PIN       (-1)
#define BOARD_LED_RGB       0

// ── Internal GPIO map (informational — do not reassign) ──────────────────────
// GPIO23 = LCD MOSI / SD MOSI     GPIO18 = LCD SCK / SD SCK
// GPIO14 = LCD DC                 GPIO27 = LCD RST
// GPIO33 = LCD CS                 GPIO4  = SD CS
// GPIO25 = speaker DAC out        GPIO34 = Mic ADC (input only)
// GPIO35 = battery ADC (input)    GPIO36 = button C input
// GPIO37 = button B input         GPIO38 = button A input
// GPIO39 = IMU interrupt (input)

// ── Misc ─────────────────────────────────────────────────────────────────────
#define BOARD_SERIAL_BAUD   115200
#define BOARD_MCP_PORT      9000
#define BOARD_HTTP_PORT     80
