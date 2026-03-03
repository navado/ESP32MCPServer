#ifndef MCP_TYPES_H
#define MCP_TYPES_H

#include <ArduinoJson.h>
#include <string>

namespace mcp {

enum class MCPRequestType {
    INITIALIZE,
    RESOURCES_LIST,
    RESOURCE_READ,
    SUBSCRIBE,
    UNSUBSCRIBE
};

using RequestId = uint32_t;

struct MCPRequest {
    MCPRequestType type;
    RequestId id;
    JsonObject params;

    MCPRequest() : type(MCPRequestType::INITIALIZE), id(0), params() {}
};

struct MCPResponse {
    bool success;
    std::string message;
    JsonVariant data;

    MCPResponse() : success(false), message(""), data() {}
    MCPResponse(bool s, const std::string &msg, JsonVariant d)
        : success(s), message(msg), data(d) {}
};

struct MCPResource {
    std::string name;
    std::string uri;
    std::string type;
    std::string value;

    MCPResource() = default;
    MCPResource(const std::string &n, const std::string &u, const std::string &t, const std::string &v)
        : name(n), uri(u), type(t), value(v) {}
};

} // namespace mcp

#endif // MCP_TYPES_H
