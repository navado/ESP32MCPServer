#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <ArduinoJson.h>
#include "MCPTypes.h"
#include "AuthPolicy.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef NATIVE_TEST
#include <WebSocketsServer.h>
#endif

namespace mcp {

#ifndef NATIVE_TEST
// WebSocketsServer that runs an admission gate on every new connection BEFORE
// the application sees it.  The library has already answered the upgrade
// (101) at that point, so a refused client is closed with an application
// close code (4401 unauthorized, 4429 too many clients) and its CONNECTED
// event is swallowed; no message from it is ever processed.
class GatedWebSocketsServer : public WebSocketsServer {
public:
    // Returns 0 to accept, or the WebSocket close code to refuse with.
    using Gate = std::function<uint16_t(uint8_t num, const String& authorization,
                                        const String& url)>;
    explicit GatedWebSocketsServer(uint16_t port) : WebSocketsServer(port) {}
    void setGate(Gate g) { gate_ = std::move(g); }

protected:
    void runCbEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) override;

private:
    Gate gate_;
};
#endif

struct Implementation {
    std::string name;
    std::string version;
};

struct ServerCapabilities {
    bool supportsSubscriptions;
    bool supportsResources;
};

// An MCP tool that exposes an existing JSON-RPC method (issue #13).
// `inputSchema` is a JSON Schema literal kept in flash and embedded verbatim
// in tools/list (never parsed or copied to the heap).
struct ToolDef {
    std::string name;          // "ble_scan"
    std::string title;         // "Scan for BLE devices"
    std::string description;
    const char* inputSchema = "{\"type\":\"object\"}";
    std::string method;        // underlying JSON-RPC method, e.g. "ble/scan"
    bool readOnly    = true;   // annotations (hints for the client)
    bool destructive = false;
    bool idempotent  = false;
    bool openWorld   = false;  // interacts with external entities (BLE peers)
};

// Protocol revisions this server speaks, newest first.
constexpr const char* MCP_PROTOCOL_VERSIONS[] = {"2025-06-18", "2025-03-26", "2024-11-05"};
constexpr const char* MCP_LATEST_PROTOCOL_VERSION = "2025-06-18";

// Client ids at or above this are request/response transports (Streamable
// HTTP sessions) that cannot receive server-initiated messages.
constexpr uint8_t MCP_HTTP_CLIENT_BASE = 100;

class MCPServer {
public:
    // Callback invoked by broadcastResourceUpdate() to push notifications to
    // subscribed clients.  In production this wraps ws_.text(); in tests it
    // is set by MockWebSocket to capture notifications.
    using SendFunc = std::function<void(uint8_t clientId, const std::string& msg)>;

    // Extension point: external components register handlers for additional
    // JSON-RPC methods (e.g. "sensors/i2c/scan", "bus/read").
    // handler(clientId, requestId, params) → JSON-RPC response string.
    using MethodHandler = std::function<std::string(uint8_t clientId,
                                                     uint32_t id,
                                                     const JsonObject& params)>;

    explicit MCPServer(uint16_t port = 9000);

    // Set up the WebSocket server and register system resources.
    void begin(bool isConnected);

    // Pump the WebSocket event loop (call from a FreeRTOS task).
    void handleClient();

    // Register/unregister a resource so it appears in resources/list responses.
    void registerResource(const MCPResource& resource);
    void unregisterResource(const std::string& uri);

    // Push a resource-updated notification to all subscribed clients.
    void broadcastResourceUpdate(const std::string& uri);

    // Process a raw JSON-RPC 2.0 message from `clientId` and return the
    // serialised JSON-RPC response string.  Used directly by tests via
    // MockWebSocket::simulateMessage().
    std::string processMessage(uint8_t clientId, const std::string& json);

    // Forget all per-client state (subscriptions) for `clientId`.  Called when
    // a WebSocket client disconnects: the library reuses client slot numbers,
    // so a new client must not inherit the previous occupant's subscriptions.
    void removeClient(uint8_t clientId);

    // Override the transport used for push notifications.
    void setSendFunc(SendFunc fn) { sendFunc_ = fn; }

    // Register a handler for a custom JSON-RPC method.
    // If method is already registered the old handler is replaced.
    void registerMethodHandler(const std::string& method, MethodHandler handler);

    // Remove a previously registered method handler.
    void unregisterMethodHandler(const std::string& method);

    // Expose a registered method as an MCP tool (tools/list, tools/call).
    void registerTool(const ToolDef& tool);
    size_t toolCount() const { return tools_.size(); }
    const std::vector<ToolDef>& tools() const { return tools_; }

    // --- Transport security (issue #12) -----------------------------------
    // Enable authentication with these credentials.  Until this is called the
    // server is open (unit tests); the firmware always calls it.
    void setCredentials(const auth::Credentials& creds);
    // Per-client token bucket: `capacity` burst, `perSec` sustained.
    void setRateLimit(uint32_t capacity, uint32_t perSec);
    // Maximum concurrently admitted clients (default 4).
    void setMaxClients(size_t n) { maxClients_ = n; }
    // Time source for rate limiting (defaults to millis()).
    void setClock(std::function<uint32_t()> clock) { clock_ = std::move(clock); }

    enum class Admission { Accepted, Unauthorized, TooManyClients };
    // Admission decision for a new connection; on Accepted the client gets
    // the access level of its token.  Called by the transport at connect.
    Admission admitClient(uint8_t clientId, const std::string& authorization,
                          const std::string& url);
    auth::Access clientAccess(uint8_t clientId) const;
    size_t clientCount() const { return clients_.size(); }

private:

    uint16_t port_;
    Implementation serverInfo_{"esp32-mcp-server", "1.0.0"};
    ServerCapabilities capabilities_{true, true};
    SendFunc sendFunc_;
#ifndef NATIVE_TEST
    GatedWebSocketsServer wsServer_;
    void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    void onClientConnect(uint8_t num);
#endif

    // Resource registry: URI -> MCPResource
    std::unordered_map<std::string, MCPResource> resources_;

    // Subscription registry: URI -> set of subscribed client IDs
    std::unordered_map<std::string, std::unordered_set<uint8_t>> subscriptions_;

    // Extension method registry: method name -> handler
    std::unordered_map<std::string, MethodHandler> methodHandlers_;

    // Tool registry, in registration order (tools/list order).
    std::vector<ToolDef> tools_;

    // Admitted clients: access level + request limiter.
    struct ClientState {
        auth::Access      access = auth::Access::None;
        auth::RateLimiter limiter;
    };
    std::unordered_map<uint8_t, ClientState> clients_;
    bool              authEnforced_ = false;
    auth::Credentials creds_;
    size_t            maxClients_ = 4;
    uint32_t          rateCapacity_ = 20;
    uint32_t          ratePerSec_   = 10;
    std::function<uint32_t()> clock_;
    uint32_t now() const;

    // Admission / authorization / rate checks for one request; returns an
    // error response, or "" to proceed.
    std::string checkRequest(uint8_t clientId, const std::string& method, uint32_t id);

    // Dispatch a parsed request to the appropriate handler.
    std::string dispatch(uint8_t clientId, const std::string& method,
                         uint32_t id, const JsonObject& params);

    std::string handleInitialize(uint8_t clientId, uint32_t id, const JsonObject& params);
    std::string handleToolsList(uint8_t clientId, uint32_t id, const JsonObject& params);
    std::string handleToolsCall(uint8_t clientId, uint32_t id, const JsonObject& params);
    std::string invokeHandler(const MethodHandler& handler, uint8_t clientId,
                              uint32_t id, const JsonObject& params);
    std::string handleResourcesList(uint32_t id, const JsonObject& params);
    std::string handleResourceRead(uint32_t id, const JsonObject& params);
    std::string handleSubscribe(uint8_t clientId, uint32_t id, const JsonObject& params);
    std::string handleUnsubscribe(uint8_t clientId, uint32_t id, const JsonObject& params);

    // JSON-RPC 2.0 envelope builders
    static std::string makeResult(uint32_t id, JsonDocument& resultDoc);
    static std::string makeError(uint32_t id, int code, const std::string& message);
    static std::string makeErrorData(uint32_t id, int code, const std::string& message,
                                     JsonDocument& data);
};

} // namespace mcp

#endif // MCP_SERVER_H
