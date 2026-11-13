#include "MCPServer.h"
#include "MCPTypes.h"
#include <ArduinoJson.h>
#include <new>
#ifndef NATIVE_TEST
#include <Arduino.h>
#endif

using namespace mcp;

// ---------------------------------------------------------------------------
// Construction / lifecycle
// ---------------------------------------------------------------------------

MCPServer::MCPServer(uint16_t port)
#ifndef NATIVE_TEST
    : port_(port), wsServer_(port)
#else
    : port_(port)
#endif
{
}

void MCPServer::begin(bool /*isConnected*/) {
#ifndef NATIVE_TEST
    wsServer_.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        this->onWebSocketEvent(num, type, payload, length);
    });
    wsServer_.setGate([this](uint8_t num, const String& authorization,
                             const String& url) -> uint16_t {
        switch (admitClient(num, authorization.c_str(), url.c_str())) {
            case Admission::Accepted:       return 0;
            case Admission::TooManyClients: return auth::WS_CLOSE_TOO_MANY_CLIENTS;
            default:                        return auth::WS_CLOSE_UNAUTHORIZED;
        }
    });
    wsServer_.begin();
    // Drop clients that stop answering pings (half-open TCP, sleeping
    // laptops) so they do not hold one of the few client slots: ping every
    // 15 s, give up after 2 missed pongs of 5 s.
    wsServer_.enableHeartbeat(15000, 5000, 2);
#endif
}

void MCPServer::handleClient() {
#ifndef NATIVE_TEST
    wsServer_.loop();
#endif
}

#ifndef NATIVE_TEST
void GatedWebSocketsServer::runCbEvent(uint8_t num, WStype_t type, uint8_t* payload,
                                       size_t length) {
    if (type == WStype_CONNECTED && gate_ && num < WEBSOCKETS_SERVER_CLIENT_MAX) {
        WSclient_t* client = &_clients[num];
        uint16_t code = gate_(num, client->base64Authorization, client->cUrl);
        if (code != 0) {
            // Close frame carries only the code (the library writes `reason`
            // without the mandatory 2-byte code prefix).
            WebSockets::clientDisconnect(client, code);
            return;  // the application never sees this client
        }
    }
    WebSocketsServer::runCbEvent(num, type, payload, length);
}

void MCPServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            onClientConnect(num);
            break;
        case WStype_DISCONNECTED:
            removeClient(num);
            break;
        case WStype_TEXT: {
            if (payload == nullptr || length == 0) break;
            std::string json(reinterpret_cast<char*>(payload), length);
            std::string response = processMessage(num, json);
            if (!response.empty()) {
                wsServer_.sendTXT(num, response.c_str(), response.length());
            }
            break;
        }
        default:
            break;
    }
}

void MCPServer::onClientConnect(uint8_t num) {
    if (!sendFunc_) {
        sendFunc_ = [this](uint8_t cid, const std::string& msg) {
            wsServer_.sendTXT(cid, msg.c_str(), msg.length());
        };
    }
}
#endif

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
// Transport security
// ---------------------------------------------------------------------------

void MCPServer::setCredentials(const auth::Credentials& creds) {
    creds_        = creds;
    authEnforced_ = true;
}

void MCPServer::setRateLimit(uint32_t capacity, uint32_t perSec) {
    rateCapacity_ = capacity;
    ratePerSec_   = perSec;
}

uint32_t MCPServer::now() const {
    if (clock_) return clock_();
#ifndef NATIVE_TEST
    return millis();
#else
    return 0;
#endif
}

MCPServer::Admission MCPServer::admitClient(uint8_t clientId,
                                            const std::string& authorization,
                                            const std::string& url) {
    auth::Access access = authEnforced_
                              ? auth::authenticate(creds_, authorization, url)
                              : auth::Access::Full;
    if (access == auth::Access::None) return Admission::Unauthorized;
    // A reused slot replaces its previous occupant; otherwise enforce the cap.
    if (clients_.find(clientId) == clients_.end() && clients_.size() >= maxClients_) {
        return Admission::TooManyClients;
    }
    ClientState st;
    st.access  = access;
    st.limiter = auth::RateLimiter(rateCapacity_, ratePerSec_);
    clients_[clientId] = st;
    return Admission::Accepted;
}

auth::Access MCPServer::clientAccess(uint8_t clientId) const {
    auto it = clients_.find(clientId);
    return it == clients_.end() ? auth::Access::None : it->second.access;
}

std::string MCPServer::checkRequest(uint8_t clientId, const std::string& method,
                                    uint32_t id) {
    auto it = clients_.find(clientId);
    if (it == clients_.end()) {
        // Not admitted through the transport gate.  Only reachable when
        // authentication is enforced (tests and the open mode skip it).
        if (!authEnforced_) return std::string();
        return makeError(id, auth::ERR_UNAUTHORIZED, "Unauthorized");
    }
    ClientState& st = it->second;
    if (!st.limiter.allow(now())) {
        JsonDocument data;
        data["retryAfterMs"] = st.limiter.retryAfterMs();
        return makeErrorData(id, auth::ERR_RATE_LIMITED, "Rate limited", data);
    }
    if (!auth::methodAllowed(st.access, method)) {
        JsonDocument data;
        data["method"]   = method;
        data["access"]   = auth::accessName(st.access);
        data["required"] = auth::accessName(auth::Access::Full);
        return makeErrorData(id, auth::ERR_UNAUTHORIZED,
                             "Forbidden: requires a full-control token", data);
    }
    return std::string();
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

    // A message with a result/error and no method is a response to a
    // server-initiated request; we never send any, so drop it silently.
    if (!doc["method"].is<const char*>()) {
        if (doc["result"].is<JsonVariantConst>() || doc["error"].is<JsonVariantConst>())
            return std::string();
        return makeError(0, -32600, "Invalid Request: missing method");
    }

    // JSON-RPC: a request without "id" is a notification and MUST NOT be
    // answered (notifications/initialized, notifications/cancelled, ...).
    const bool isNotification = !doc["id"].is<JsonVariantConst>() || doc["id"].isNull();

    // Ids may be numbers or strings.  Handlers work with numeric ids; a
    // string id is swapped for a placeholder and restored in the response.
    const bool stringId = doc["id"].is<const char*>();
    std::string originalId = stringId ? doc["id"].as<const char*>() : std::string();
    uint32_t id = stringId ? 0u : (doc["id"] | 0u);

    std::string method = doc["method"].as<const char*>();

    std::string response = checkRequest(clientId, method, id);
    if (response.empty()) {
        if (method.compare(0, 14, "notifications/") == 0) {
            // initialized / cancelled / roots/list_changed: nothing to do.
            return std::string();
        }
        JsonObject params = doc["params"].is<JsonObject>()
                                ? doc["params"].as<JsonObject>()
                                : JsonObject();
        response = dispatch(clientId, method, id, params);
    }
    if (isNotification) return std::string();

    if (stringId && !response.empty()) {
        JsonDocument out;
        if (!deserializeJson(out, response)) {
            out["id"] = originalId;
            response.clear();
            serializeJson(out, response);
        }
    }
    return response;
}

std::string MCPServer::dispatch(uint8_t clientId, const std::string& method,
                                uint32_t id, const JsonObject& params) {
    if (method == "initialize") {
        return handleInitialize(clientId, id, params);
    }
    if (method == "ping") {
        JsonDocument empty;
        empty.to<JsonObject>();
        return makeResult(id, empty);
    }
    if (method == "tools/list") {
        return handleToolsList(clientId, id, params);
    }
    if (method == "tools/call") {
        return handleToolsCall(clientId, id, params);
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
        return invokeHandler(it->second, clientId, id, params);
    }

    return makeError(id, -32601, "Method not found");
}

// ---------------------------------------------------------------------------
// Handler implementations
// ---------------------------------------------------------------------------

std::string MCPServer::invokeHandler(const MethodHandler& handler, uint8_t clientId,
                                     uint32_t id, const JsonObject& params) {
    try {
        return handler(clientId, id, params);
    } catch (const std::bad_alloc&) {
        // Heap exhaustion (e.g. a huge ble/scan/results payload).  NEVER let
        // the exception escape: on the device it propagates out of the MCP
        // task through the WebSockets library and std::terminate reboots the
        // board, dropping every connected client.  Reply with a JSON-RPC error
        // instead; the client sees a clean failure and the server keeps serving.
        return makeError(id, -32603, "Internal error: out of memory");
    } catch (...) {
        return makeError(id, -32603, "Internal error");
    }
}

std::string MCPServer::handleInitialize(uint8_t clientId, uint32_t id,
                                        const JsonObject& params) {
    // Version negotiation: echo a supported requested version, otherwise
    // answer with the newest one we speak (the client decides whether it can
    // continue).  A missing version is tolerated for pre-spec clients.
    const char* chosen = MCP_LATEST_PROTOCOL_VERSION;
    JsonVariantConst requested = params["protocolVersion"];
    if (!requested.isNull()) {
        if (!requested.is<const char*>()) {
            JsonDocument data;
            JsonArray sup = data["supported"].to<JsonArray>();
            for (const char* v : MCP_PROTOCOL_VERSIONS) sup.add(v);
            data["requested"] = requested;
            return makeErrorData(id, -32602, "Unsupported protocol version", data);
        }
        const std::string want = requested.as<const char*>();
        for (const char* v : MCP_PROTOCOL_VERSIONS) {
            if (want == v) { chosen = v; break; }
        }
    }

    JsonDocument result;
    result["protocolVersion"] = chosen;

    JsonObject serverInfo = result["serverInfo"].to<JsonObject>();
    serverInfo["name"]    = serverInfo_.name;
    serverInfo["title"]   = "ESP32 MCP Server";
    serverInfo["version"] = serverInfo_.version;

    // Request/response transports (HTTP sessions) cannot receive
    // notifications/resources/updated, so they are not offered subscribe.
    const bool canPush = clientId < MCP_HTTP_CLIENT_BASE;
    JsonObject caps = result["capabilities"].to<JsonObject>();
    JsonObject res = caps["resources"].to<JsonObject>();
    res["subscribe"]   = capabilities_.supportsSubscriptions && canPush;
    res["listChanged"] = false;
    JsonObject tools = caps["tools"].to<JsonObject>();
    tools["listChanged"] = false;

    result["instructions"] =
        "Hardware gateway on an ESP32: read I2C sensors, NMEA/CAN bus history, "
        "metrics and logs, and scan or connect to Bluetooth LE devices. "
        "Use tools/list to discover the operations.";

    return makeResult(id, result);
}

// ---------------------------------------------------------------------------
// Tools (issue #13)
// ---------------------------------------------------------------------------

void MCPServer::registerTool(const ToolDef& tool) {
    for (auto& t : tools_) {
        if (t.name == tool.name) { t = tool; return; }
    }
    tools_.push_back(tool);
}

std::string MCPServer::handleToolsList(uint8_t clientId, uint32_t id,
                                       const JsonObject& /*params*/) {
    // Read-only clients only see the tools they may call.
    const auth::Access access = authEnforced_ ? clientAccess(clientId) : auth::Access::Full;
    JsonDocument result;
    JsonArray arr = result["tools"].to<JsonArray>();
    for (const auto& t : tools_) {
        if (!auth::methodAllowed(access, t.method)) continue;
        if (methodHandlers_.find(t.method) == methodHandlers_.end()) continue;
        JsonObject o = arr.add<JsonObject>();
        o["name"] = t.name;
        if (!t.title.empty()) o["title"] = t.title;
        o["description"] = t.description;
        o["inputSchema"] = serialized(t.inputSchema);
        JsonObject ann = o["annotations"].to<JsonObject>();
        if (!t.title.empty()) ann["title"] = t.title;
        ann["readOnlyHint"]    = t.readOnly;
        ann["destructiveHint"] = t.destructive;
        ann["idempotentHint"]  = t.idempotent;
        ann["openWorldHint"]   = t.openWorld;
    }
    return makeResult(id, result);
}

// Results larger than this are returned only as structuredContent, with a
// short text block, so a big scan result is not held twice in RAM.
static constexpr size_t TOOL_TEXT_MAX = 4096;

std::string MCPServer::handleToolsCall(uint8_t clientId, uint32_t id,
                                       const JsonObject& params) {
    if (!params["name"].is<const char*>()) {
        return makeError(id, -32602, "Invalid params: name required");
    }
    const std::string name = params["name"].as<const char*>();
    const ToolDef* tool = nullptr;
    for (const auto& t : tools_) {
        if (t.name == name) { tool = &t; break; }
    }
    auto handlerIt = tool ? methodHandlers_.find(tool->method) : methodHandlers_.end();
    if (!tool || handlerIt == methodHandlers_.end()) {
        return makeError(id, -32602, "Unknown tool: " + name);
    }

    // The tool inherits the authorization of the method it wraps.
    if (authEnforced_) {
        const auth::Access access = clientAccess(clientId);
        if (!auth::methodAllowed(access, tool->method)) {
            JsonDocument data;
            data["tool"]     = name;
            data["access"]   = auth::accessName(access);
            data["required"] = auth::accessName(auth::Access::Full);
            return makeErrorData(id, auth::ERR_UNAUTHORIZED,
                                 "Forbidden: requires a full-control token", data);
        }
    }

    JsonObject args = params["arguments"].is<JsonObject>()
                          ? params["arguments"].as<JsonObject>()
                          : JsonObject();
    std::string raw = invokeHandler(handlerIt->second, clientId, id, args);

    try {
        JsonDocument inner;
        if (deserializeJson(inner, raw)) {
            return makeError(id, -32603, "Internal error: tool returned invalid JSON");
        }
        JsonDocument result;
        JsonArray content = result["content"].to<JsonArray>();
        JsonObject text = content.add<JsonObject>();
        text["type"] = "text";

        if (inner["error"].is<JsonObject>()) {
            // Tool execution errors are reported inside the result so the
            // model can see and react to them (MCP spec), keeping the code
            // and data of the underlying JSON-RPC error.
            JsonObject err = inner["error"];
            std::string msg = err["message"] | "Error";
            if (err["data"].is<JsonVariantConst>()) {
                std::string data;
                serializeJson(err["data"], data);
                msg += " " + data;
            }
            text["text"] = msg;
            result["structuredContent"]["error"] = err;
            result["isError"] = true;
            return makeResult(id, result);
        }

        JsonVariant value = inner["result"];
        if (value.is<JsonObject>()) {
            result["structuredContent"] = value;
        } else {
            result["structuredContent"]["value"] = value;
        }
        std::string body;
        serializeJson(value, body);
        if (body.size() <= TOOL_TEXT_MAX) {
            text["text"] = body;
        } else {
            text["text"] = "Result is " + std::to_string(body.size()) +
                           " bytes; see structuredContent.";
        }
        result["isError"] = false;
        return makeResult(id, result);
    } catch (const std::bad_alloc&) {
        return makeError(id, -32603, "Internal error: out of memory");
    }
}

std::string MCPServer::handleResourcesList(uint32_t id, const JsonObject& /*params*/) {
    JsonDocument result;
    JsonArray arr = result["resources"].to<JsonArray>();

    for (const auto& kv : resources_) {
        JsonObject obj = arr.add<JsonObject>();
        obj["uri"]         = kv.second.uri;
        obj["name"]        = kv.second.name;
        obj["mimeType"]    = kv.second.type;
        obj["type"]        = kv.second.type;   // legacy field
        obj["description"] = kv.second.value;
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
    result["success"] = true;
    result["uri"]     = it->second.uri;
    result["value"]   = it->second.value;

    JsonArray contents = result["contents"].to<JsonArray>();
    JsonObject content = contents.add<JsonObject>();
    content["uri"]      = it->second.uri;
    content["mimeType"] = it->second.type;
    content["type"]     = it->second.type;   // legacy field
    content["text"]     = it->second.value;

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

void MCPServer::removeClient(uint8_t clientId) {
    clients_.erase(clientId);
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        it->second.erase(clientId);
        if (it->second.empty()) {
            it = subscriptions_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// Broadcast
// ---------------------------------------------------------------------------

bool MCPServer::sendToClient(uint8_t clientId, const std::string& msg) {
    if (!sendFunc_ || clientId >= MCP_HTTP_CLIENT_BASE) return false;
    if (authEnforced_ && !hasClient(clientId)) return false;
    sendFunc_(clientId, msg);
    return true;
}

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

std::string MCPServer::makeErrorData(uint32_t id, int code, const std::string& message,
                                     JsonDocument& data) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"]      = id;
    JsonObject error = doc["error"].to<JsonObject>();
    error["code"]    = code;
    error["message"] = message;
    error["data"]    = data.as<JsonVariant>();

    std::string out;
    serializeJson(doc, out);
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
