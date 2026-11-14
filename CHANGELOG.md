# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Security
- **Optional MCP access tokens** — #12
  - **Off by default**: existing clients and examples keep working unchanged. Turn it on
    with the serial command `auth on` (or `auth off` to undo), or ship it on with the
    build flag `MCP_AUTH_DEFAULT=1`. Stored in NVS `security`/`auth`.
  - Full-control token, plus an optional read-only token, stored in NVS namespace
    `security`. Sent as `Authorization: Bearer <token>` or `?token=<token>`.
  - Generated and printed on the serial console when auth is turned on. Managed with the
    serial commands `token show|rotate|set`, `token ro rotate|clear`, `ota show|rotate`.
  - With auth on, unauthenticated connections are closed with code 4401, and a read-only
    token calling a state-changing method gets `-32020`.
  - Limits: at most 4 clients (close code 4429); per-client rate limit of burst 20, then
    10/s (`-32021`); a heartbeat drops dead clients.
  - The dashboard and `examples/client/client.ts` take an optional token.
- **OTA has no default password** — #12. A random per-device password is generated on
  first boot (also replacing a stored `admin`). Repeated failed logins from one IP lock
  that IP out with `429`.

### Added
- **Bluetooth LE over MCP (ESP32-S3)** — #9, #11
  - Advertising scanner: `ble/scan`, `ble/scan/results`, `ble/scan/payloads`, `ble/scan/poll`, `ble/scan/stop`.
    Non-blocking scans, per-device raw payload history, a MAC-independent payload registry,
    and capture-time filters (`minRssi`, `manufacturerId`, `serviceUuid`, `macPrefix`).
  - GATT client: `ble/connect` (asynchronous, enforced timeout, public / random / RPA
    addresses via `addrType`), `ble/connect/results`, `ble/disconnect`,
    `ble/services/start|results|stop` (enumeration, value reads, notification capture).
  - See the README's *Bluetooth (BLE)* section for capabilities and limits.
- **MCP WebSocket transport** on port 9000 (the port mDNS already advertised) — #9.
- **Boot-time memory plan** (`include/MemoryPlan.h`): PSRAM is detected at boot and bus
  history / BLE capture are sized accordingly (`[mem] plan=…` on the serial console) — #11.
- **`esp32-s3-devkitc-1-n16r8` env** for ESP32-S3 modules with 8 MB octal PSRAM
  (N8R8 / N16R8) — #11.
- `partitions_4mb_ota.csv` with 1.81 MB OTA slots for 4 MB ESP32 boards — #9.

### Changed
- **Upgrading:** MCP clients are unaffected (authentication is off until `auth on`). The
  OTA password changes if it was still `admin`, and the `isDefaultPwd` field is gone from
  `/ota/config` — #12.
- **`esp32-c3-devkitm-1` moves to `partitions_4mb_ota.csv`** (the firmware outgrew the
  1.25 MB app slots). Flash once over USB, then `-t uploadfs`; NVS is kept — #12.
- **4 MB boards (`esp32-devkitc`, `adafruit-huzzah32`, `m5stack-core`) use a new partition
  table.** It cannot be applied over OTA: flash once over USB, then re-upload the web UI
  with `pio run -e <env> -t uploadfs` (the filesystem moves and shrinks to 128 KB, so its
  contents are lost; NVS — WiFi credentials, OTA password — is kept). See the board pages
  in `docs/boards/`. — #9
- BLE is compiled out on the 4 MB boards (`esp32-devkitc`, `esp32-c3-devkitm-1`,
  `adafruit-huzzah32`, `m5stack-core`): it does not fit alongside the WebSocket transport
  and OTA. — #9
- ESP32 platform pinned to `platformio/espressif32@6.9.0` (Arduino core 2.0.17). — #11
- Default log level lowered to INFO (`CORE_DEBUG_LEVEL=3`); AsyncTCP runs on core 1. — #9
- Bus-history auto budget is capped per board (`BOARD_BUS_HISTORY_HEAP_CAP`) and, at
  runtime, by the memory plan. — #9, #11

### Fixed
- Boot loop on startup: lwIP was used before initialisation (`Invalid mbox`). — #9
- Uncaught exceptions in MCP method handlers rebooted the board; they now return a
  JSON-RPC `-32603` error. — #9
- BLE/GATT defects found in review and hardware testing (#10): reconnect failing after the
  first connection, random-address peers unreachable, `ble/services/results` blocking for
  its full limit, unbounded `notifyCaptureMs`, a GATT-task race that could wedge the
  client, stale WebSocket subscriptions, unbounded peer-controlled allocations. — #11

### Security
- The MCP WebSocket (port 9000) is **unauthenticated**: anyone on the network can call
  every method, including BLE scan/connect. Restrict access at the network level until
  authentication is added.
