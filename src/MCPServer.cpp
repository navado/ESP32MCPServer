#include "MCPServer.h"
#include "MCPTypes.h"
#include <ArduinoJson.h>

using namespace mcp;

// ---------------------------------------------------------------------------
// Construction / lifecycle
// ---------------------------------------------------------------------------

MCPServer::MCPServer(uint16_t port) : port_(port) {}

void MCPServer::begin(bool /*isConnected*/) {
    // In production this would initialise the AsyncWebSocket and register
    // the WebSocket event handler that calls processMessage().  Kept as a
    // stub here because the WebSocket library headers are ESP32-specific;
    // the logic is exercised through processMessage() in unit tests.
}

void MCPServer::handleClient() {
    // In production: ws_.cleanupClients();
}

// ---------------------------------------------------------------------------
// Resource management
// ---------------------------------------------------------------------------

void MCPServer::registerResource(const MCPResource& resource) {
    resources_[resource.uri] = resource;
}

void MCPServer::unregisterResource(const std::string& uri) {
    resources_.erase(uri);
    subscriptions_.erase(uri);
}

// ---------------------------------------------------------------------------
// Extension method handlers
// ---------------------------------------------------------------------------

void MCPServer::registerMethodHandler(const std::string& method,
                                       MethodHandler handler) {
    methodHandlers_[method] = std::move(handler);
}

void MCPServer::unregisterMethodHandler(const std::string& method) {
    methodHandlers_.erase(method);
}

// ---------------------------------------------------------------------------
// Message processing (JSON-RPC 2.0)
// ---------------------------------------------------------------------------

std::string MCPServer::processMessage(uint8_t clientId, const std::string& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        return makeError(0, -32700, "Parse error");
    }

    // Validate jsonrpc field
    if (!doc["jsonrpc"].is<const char*>() ||
        std::string(doc["jsonrpc"].as<const char*>()) != "2.0") {
        return makeError(0, -32600, "Invalid Request: missing jsonrpc 2.0");
    }

    if (!doc["method"].is<const char*>()) {
        return makeError(0, -32600, "Invalid Request: missing method");
    }

    uint32_t id = doc["id"] | 0u;
    std::string method = doc["method"].as<const char*>();
    JsonObject params = doc["params"].is<JsonObject>()
                            ? doc["params"].as<JsonObject>()
                            : JsonObject();

    return dispatch(clientId, method, id, params);
}

std::string MCPServer::dispatch(uint8_t clientId, const std::string& method,
                                uint32_t id, const JsonObject& params) {
    if (method == "initialize") {
        return handleInitialize(id, params);
    }
    if (method == "resources/list") {
        return handleResourcesList(id, params);
    }
    if (method == "resources/read") {
        return handleResourceRead(id, params);
    }
    if (method == "resources/subscribe") {
        return handleSubscribe(clientId, id, params);
    }
    if (method == "resources/unsubscribe") {
        return handleUnsubscribe(clientId, id, params);
    }

    // Check extension method handlers registered by external components
    auto it = methodHandlers_.find(method);
    if (it != methodHandlers_.end()) {
        return it->second(clientId, id, params);
    }

    return makeError(id, -32601, "Method not found");
}

// ---------------------------------------------------------------------------
// Handler implementations
// ---------------------------------------------------------------------------

std::string MCPServer::handleInitialize(uint32_t id, const JsonObject& /*params*/) {
    JsonDocument result;
    result["protocolVersion"] = PROTOCOL_VERSION;

    JsonObject serverInfo = result["serverInfo"].to<JsonObject>();
    serverInfo["name"]    = serverInfo_.name;
    serverInfo["version"] = serverInfo_.version;

    JsonObject caps = result["capabilities"].to<JsonObject>();
    caps["subscriptions"] = capabilities_.supportsSubscriptions;
    caps["resources"]     = capabilities_.supportsResources;

    return makeResult(id, result);
}

std::string MCPServer::handleResourcesList(uint32_t id, const JsonObject& /*params*/) {
    JsonDocument result;
    JsonArray arr = result["resources"].to<JsonArray>();

    for (const auto& kv : resources_) {
        JsonObject obj = arr.add<JsonObject>();
        obj["uri"]  = kv.second.uri;
        obj["name"] = kv.second.name;
        obj["type"] = kv.second.type;
    }

    return makeResult(id, result);
}

std::string MCPServer::handleResourceRead(uint32_t id, const JsonObject& params) {
    if (!params["uri"].is<const char*>()) {
        return makeError(id, -32602, "Invalid params: uri required");
    }

    std::string uri = params["uri"].as<const char*>();
    auto it = resources_.find(uri);
    if (it == resources_.end()) {
        return makeError(id, -32001, "Resource not found");
    }

    JsonDocument result;
    result["uri"]   = it->second.uri;
    result["value"] = it->second.value;

    JsonArray contents = result["contents"].to<JsonArray>();
    JsonObject content = contents.add<JsonObject>();
    content["type"] = it->second.type;
    content["text"] = it->second.value;

    return makeResult(id, result);
}

std::string MCPServer::handleSubscribe(uint8_t clientId, uint32_t id,
                                       const JsonObject& params) {
    if (!params["uri"].is<const char*>()) {
        return makeError(id, -32602, "Invalid params: uri required");
    }

    std::string uri = params["uri"].as<const char*>();
    if (resources_.find(uri) == resources_.end()) {
        return makeError(id, -32001, "Resource not found");
    }

    subscriptions_[uri].insert(clientId);

    JsonDocument result;
    result["success"] = true;
    return makeResult(id, result);
}

std::string MCPServer::handleUnsubscribe(uint8_t clientId, uint32_t id,
                                         const JsonObject& params) {
    if (!params["uri"].is<const char*>()) {
        return makeError(id, -32602, "Invalid params: uri required");
    }

    std::string uri = params["uri"].as<const char*>();
    auto it = subscriptions_.find(uri);
    if (it != subscriptions_.end()) {
        it->second.erase(clientId);
        if (it->second.empty()) subscriptions_.erase(it);
    }

    JsonDocument result;
    result["success"] = true;
    return makeResult(id, result);
}

// ---------------------------------------------------------------------------
// Broadcast
// ---------------------------------------------------------------------------

void MCPServer::broadcastResourceUpdate(const std::string& uri) {
    if (!sendFunc_) return;

    auto it = subscriptions_.find(uri);
    if (it == subscriptions_.end()) return;

    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"]  = "notifications/resources/updated";
    JsonObject notifParams = doc["params"].to<JsonObject>();
    notifParams["uri"] = uri;

    std::string notification;
    serializeJson(doc, notification);

    for (uint8_t cid : it->second) {
        sendFunc_(cid, notification);
    }
}

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 envelope builders
// ---------------------------------------------------------------------------

std::string MCPServer::makeResult(uint32_t id, JsonDocument& resultDoc) {
    JsonDocument env;
    env["jsonrpc"] = "2.0";
    env["id"]      = id;
    env["result"]  = resultDoc.as<JsonVariant>();

    std::string out;
    serializeJson(env, out);
    return out;
}

std::string MCPServer::makeError(uint32_t id, int code, const std::string& message) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"]      = id;
    JsonObject error = doc["error"].to<JsonObject>();
    error["code"]    = code;
    error["message"] = message;

    std::string out;
    serializeJson(doc, out);
    return out;
}
