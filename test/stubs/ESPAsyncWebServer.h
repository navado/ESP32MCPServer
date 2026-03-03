#pragma once
// ESPAsyncWebServer.h stub for native builds.
// Provides the minimal API surface used by NetworkManager.

#include "Arduino.h"
#include "LittleFS.h"
#include <functional>
#include <string>

// HTTP method bit-flags
#define HTTP_GET     0x01
#define HTTP_POST    0x02
#define HTTP_PUT     0x04
#define HTTP_DELETE  0x08
#define HTTP_PATCH   0x10
#define HTTP_ANY     0xFF

// WebSocket event types
typedef enum {
    WS_EVT_CONNECT    = 0,
    WS_EVT_DISCONNECT = 1,
    WS_EVT_ERROR      = 2,
    WS_EVT_DATA       = 3,
    WS_EVT_PONG       = 4
} AwsEventType;

// ─── AsyncWebParameter ────────────────────────────────────────────────────────
struct AsyncWebParameter {
    String _value;
    explicit AsyncWebParameter(const char* v = "") : _value(v) {}
    const String& value() const { return _value; }
};

// ─── AsyncResponseStream ──────────────────────────────────────────────────────
class AsyncResponseStream {
public:
    void print(const String& s) { _buf += s.c_str(); }
    void print(const char*   s) { if (s) _buf += s; }
    void println(const String& s) { _buf += s.c_str(); _buf += '\n'; }
    const std::string& buffer() const { return _buf; }
private:
    std::string _buf;
};

// ─── AsyncWebServerRequest ────────────────────────────────────────────────────
class AsyncWebServerRequest {
public:
    virtual ~AsyncWebServerRequest() = default;
    bool hasParam(const char* /*name*/, bool /*post*/ = false) const { return false; }
    const AsyncWebParameter* getParam(const char* /*name*/, bool /*post*/ = false) const { return nullptr; }

    void send(int /*code*/, const char* /*type*/ = "text/plain", const String& /*content*/ = "") {}
    void send(AsyncResponseStream* /*stream*/) {}
    // Template overload so LittleFS (MockLittleFS) can be passed without type errors
    template<typename FS>
    void send(FS& /*fs*/, const String& /*path*/, const char* /*type*/ = nullptr) {}
    template<typename FS>
    void send(FS& /*fs*/, const char*   /*path*/, const char* /*type*/ = nullptr) {}
    void redirect(const String& /*url*/) {}
    String url()  const { return String("/"); }
    String host() const { return String("localhost"); }

    AsyncResponseStream* beginResponseStream(const char* /*contentType*/) {
        return new AsyncResponseStream();
    }
};

// ─── AsyncWebSocketClient ─────────────────────────────────────────────────────
class AsyncWebSocketClient {
public:
    virtual ~AsyncWebSocketClient() = default;
    uint32_t id()           const { return 0; }
    bool     connected()    const { return false; }
    void text(const String& /*msg*/) {}
    void text(const char* /*msg*/, size_t /*len*/ = 0) {}
};

// ─── AsyncWebSocket ───────────────────────────────────────────────────────────
class AsyncWebSocket {
public:
    using EventCallback = std::function<void(AsyncWebSocket*,
                                             AsyncWebSocketClient*,
                                             AwsEventType,
                                             void*, uint8_t*, size_t)>;

    explicit AsyncWebSocket(const char* /*url*/) {}
    void    onEvent(EventCallback /*cb*/) {}
    void    textAll(const String& /*msg*/) {}
    void    textAll(const char* /*msg*/, size_t /*len*/ = 0) {}
    void    cleanupClients(size_t /*maxClients*/ = 0) {}
    size_t  count() const { return 0; }
    const char* url() const { return "/ws"; }
};

// ─── AsyncWebServer ───────────────────────────────────────────────────────────
class AsyncWebServer {
public:
    using HandlerCB = std::function<void(AsyncWebServerRequest*)>;

    explicit AsyncWebServer(uint16_t /*port*/ = 80) {}
    void on(const char* /*uri*/, int /*method*/, HandlerCB /*handler*/) {}
    void addHandler(AsyncWebSocket* /*ws*/) {}
    template<typename FS>
    void serveStatic(const char* /*uri*/, FS& /*fs*/, const char* /*path*/,
                     const char* /*cache*/ = nullptr) {}
    void begin() {}
    void end()   {}
    void reset() {}
};
