#pragma once

// MCP over TLS (issue #23): an esp_https_server on port 443 serving
//
//   POST/DELETE https://<device>/mcp   Streamable HTTP (same sessions, auth
//                                      and handlers as http://<device>/mcp)
//   wss://<device>/ws                  the JSON-RPC WebSocket protocol of
//                                      ws://<device>:9000, with pushes
//
// Built with MCP_TLS=1 (the ESP32-S3 N16R8 env): TLS sessions take ~40 KB
// each, so mbedTLS allocates from PSRAM (tls::useSpiramForMbedtls).
//
// Threading: esp_http_server runs its handlers on its own task, but
// MCPServer belongs to the MCP task.  WebSocket admissions and messages are
// queued and run by pump() on the MCP task (HTTP POSTs go through the
// McpHttpTransport queue); pushes to wss clients are handed back to the
// server task with httpd_queue_work, the only safe way to write to a TLS
// session from another task.

#include "board_config.h"

#if MCP_TLS && !defined(NATIVE_TEST)

#include "AuthPolicy.h"
#include "MCPServer.h"
#include "McpHttpEndpoint.h"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace mcp {

// MCPServer client ids of wss clients: below MCP_HTTP_CLIENT_BASE (they can
// receive pushes) and clear of the ws://:9000 ids (0..4).
constexpr uint8_t MCP_WSS_CLIENT_BASE = 60;
constexpr uint8_t MCP_WSS_MAX_CLIENTS = 4;

class McpTlsServer {
public:
    McpTlsServer(MCPServer& server, McpHttpEndpoint& http) : server_(server), http_(http) {}

    void setCredentials(const auth::Credentials& creds) { creds_ = creds; }

    // Start on `port` with the PEM certificate and key (copied).
    bool begin(const std::string& certPem, const std::string& keyPem, uint16_t port = 443);
    bool running() const { return handle_ != nullptr; }

    // Run queued WebSocket work.  MCP task only.
    void pump();

    // Push to a wss client (any task); routed via httpd_queue_work.
    void send(uint8_t clientId, const std::string& msg);

    static constexpr size_t MAX_FRAME_BYTES = 16 * 1024;

    // --- used by the C handlers ---
    struct Slot {
        bool    used = false;
        int     fd = -1;
        uint8_t clientId = 0;
    };
    struct Job {
        enum class Kind { Admit, Message, Release };
        Kind        kind = Kind::Message;
        uint8_t     clientId = 0;
        std::string authorization, url, body, response;
        MCPServer::Admission admission = MCPServer::Admission::Accepted;
        std::atomic<bool> done{false};
    };
    void* handle_ = nullptr;   // httpd_handle_t
    MCPServer& server_;
    McpHttpEndpoint& http_;
    auth::Credentials creds_;
    std::mutex mu_;            // guards slots_ and queue_
    Slot slots_[MCP_WSS_MAX_CLIENTS];
    std::deque<std::shared_ptr<Job>> queue_;
    std::string certPem_, keyPem_;   // must outlive the server

    std::shared_ptr<Job> submit(std::shared_ptr<Job> job);
    // Wait (on the server task) for a queued job; false on timeout.
    static bool wait(const std::shared_ptr<Job>& job, uint32_t timeoutMs);
};

} // namespace mcp

#endif // MCP_TLS
