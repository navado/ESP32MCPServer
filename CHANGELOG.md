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
- **BLE client exposure** — #14
  - GATT **write** (`ble/write`, with or without response, by handle or UUID) and
    on-demand **subscribe** (`ble/subscribe` / `ble/unsubscribe`). Values are pushed to
    WebSocket clients as `notifications/ble/value` or polled with `ble/notifications`.
    All of these need the full-control token.
  - `ble/services/results` now includes each characteristic's `handle`.
  - Tools: `ble_write`, `ble_subscribe`, `ble_unsubscribe`, `ble_notifications`.
  - `examples/client/client.ts` rewritten as a typed module with BLE helpers and a
    `MCPError` that carries `code` and `data`. Fixed `listResources` / `readResource`
    (they read a nonexistent `result.data`) and the heartbeat (now a JSON-RPC `ping`).
  - `examples/client/ble_example.ts` and the dependency-free Python CLI
    `examples/client/mcp_ble.py` (Streamable HTTP).
- **Standard MCP clients can connect** — #13
  - **Streamable HTTP transport** at `POST /mcp` (port 80): sessions via `Mcp-Session-Id`,
    bearer auth, Origin check, `202` for notifications, `DELETE` to end a session. Works
    with Claude Code (`--transport http`), Claude Desktop via `mcp-remote`, and the MCP
    Inspector.
  - **Tools:** `tools/list` and `tools/call` expose the sensor, bus-history, metrics and
    BLE methods as 20 tools with JSON Schema inputs, annotations and `structuredContent`.
    Tools inherit the method's authorization.
  - **Lifecycle:** protocol version negotiation (`2025-06-18`, `2025-03-26`,
    `2024-11-05`), spec-shaped capability objects, `ping`, and notifications
    (`notifications/initialized` and others) are never answered. String request ids are
    supported.
  - `resources/read` / `resources/list` now include `uri` and `mimeType` as the spec
    requires; the old fields are kept.
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
- `initialize` returns capability *objects* (`resources: {subscribe, listChanged}`,
  `tools: {listChanged}`) instead of `{subscriptions: bool, resources: bool}`, and the
  negotiated protocol version instead of a fixed `2024-11-05` — #13.
- JSON-RPC messages without an `id` are treated as notifications and get no response.
  Before, they were answered with `id: 0` — #13.
- **Upgrading: existing MCP clients must send the token** printed on the serial console
  after flashing this version. The OTA password changes too if it was still `admin`. The
  `isDefaultPwd` field is gone from `/ota/config` — #12.
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
