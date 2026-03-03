#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <ArduinoJson.h>
#include "MCPTypes.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace mcp {

struct Implementation {
    std::string name;
    std::string version;
};

struct ServerCapabilities {
    bool supportsSubscriptions;
    bool supportsResources;
};

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

    // Override the transport used for push notifications.
    void setSendFunc(SendFunc fn) { sendFunc_ = fn; }

    // Register a handler for a custom JSON-RPC method.
    // If method is already registered the old handler is replaced.
    void registerMethodHandler(const std::string& method, MethodHandler handler);

    // Remove a previously registered method handler.
    void unregisterMethodHandler(const std::string& method);

private:
    static constexpr const char* PROTOCOL_VERSION = "2024-11-05";

    uint16_t port_;
    Implementation serverInfo_{"esp32-mcp-server", "1.0.0"};
    ServerCapabilities capabilities_{true, true};
    SendFunc sendFunc_;

    // Resource registry: URI -> MCPResource
    std::unordered_map<std::string, MCPResource> resources_;

    // Subscription registry: URI -> set of subscribed client IDs
    std::unordered_map<std::string, std::unordered_set<uint8_t>> subscriptions_;

    // Extension method registry: method name -> handler
    std::unordered_map<std::string, MethodHandler> methodHandlers_;

    // Dispatch a parsed request to the appropriate handler.
    std::string dispatch(uint8_t clientId, const std::string& method,
                         uint32_t id, const JsonObject& params);

    std::string handleInitialize(uint32_t id, const JsonObject& params);
    std::string handleResourcesList(uint32_t id, const JsonObject& params);
    std::string handleResourceRead(uint32_t id, const JsonObject& params);
    std::string handleSubscribe(uint8_t clientId, uint32_t id, const JsonObject& params);
    std::string handleUnsubscribe(uint8_t clientId, uint32_t id, const JsonObject& params);

    // JSON-RPC 2.0 envelope builders
    static std::string makeResult(uint32_t id, JsonDocument& resultDoc);
    static std::string makeError(uint32_t id, int code, const std::string& message);
};

} // namespace mcp

#endif // MCP_SERVER_H
