#pragma once

#include "MCPServer.h"

namespace mcp {

// Register the MCP tool catalogue (tools/list) for the device's methods.
// Call after the method handlers are registered.
void registerMcpTools(MCPServer& server);

} // namespace mcp
