#pragma once

#include "MCPServer.h"
#include <functional>
#include <queue>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// MockWebSocketClient
// A minimal stand-alone client stub used by NetworkManager tests.
// ---------------------------------------------------------------------------
class MockWebSocketClient {
public:
    virtual ~MockWebSocketClient() = default;

    bool isConnected() const { return connected_; }
    void connect()    { connected_ = true; }
    void disconnect() { connected_ = false; }

    virtual void onMessage(const std::string& message) {}

    void sendMessage(const std::string& message) {
        outgoing_.push(message);
    }

    std::string receiveMessage() {
        if (incoming_.empty()) return "";
        std::string msg = incoming_.front();
        incoming_.pop();
        return msg;
    }

    // Returns the most-recently received message without consuming it.
    std::string receivedMessage() const { return last_received_; }

    void queueIncomingMessage(const std::string& message) {
        incoming_.push(message);
        last_received_ = message;
    }

    void processOutgoingMessages() {
        while (!outgoing_.empty()) {
            onMessage(outgoing_.front());
            outgoing_.pop();
        }
    }

private:
    bool connected_ = false;
    std::queue<std::string> incoming_;
    std::queue<std::string> outgoing_;
    std::string last_received_;
};

// ---------------------------------------------------------------------------
// MockWebSocketServer
// A minimal broadcast-capable server stub.
// ---------------------------------------------------------------------------
class MockWebSocketServer {
public:
    void begin() { running_ = true; }
    void stop()  { running_ = false; }
    bool isRunning() const { return running_; }

    void addClient(MockWebSocketClient* client) {
        clients_.push_back(client);
    }

    void removeClient(MockWebSocketClient* client) {
        auto it = std::find(clients_.begin(), clients_.end(), client);
        if (it != clients_.end()) clients_.erase(it);
    }

    void broadcastMessage(const std::string& message) {
        for (auto* c : clients_) {
            if (c->isConnected()) c->queueIncomingMessage(message);
        }
    }

    void processMessages() {
        for (auto* c : clients_) {
            if (c->isConnected()) c->processOutgoingMessages();
        }
    }

private:
    bool running_ = false;
    std::vector<MockWebSocketClient*> clients_;
};

// ---------------------------------------------------------------------------
// MockWebSocket
//
// Wraps an MCPServer for unit testing.
//
//  • simulateMessage(clientId, json)
//      Feeds a raw JSON string into the server and returns the JSON-RPC
//      response string.
//
//  • getLastNotification()
//      Returns the most recent *push* notification sent by the server
//      (e.g. resource-updated broadcasts via broadcastResourceUpdate()).
//
//  • getAllNotifications()
//      Returns ALL push notifications accumulated since the last
//      clearNotifications() call.
//
// The key distinction:
//   – processMessage() returns the JSON-RPC *response* directly.
//   – broadcastResourceUpdate() fires the SendFunc callback; those messages
//     are stored here as notifications and NOT mixed with responses.
// ---------------------------------------------------------------------------
class MockWebSocket {
public:
    explicit MockWebSocket(mcp::MCPServer* server) : server_(server) {
        // Wire up the send function so that push notifications are captured.
        server_->setSendFunc([this](uint8_t /*clientId*/, const std::string& msg) {
            notifications_.push_back(msg);
        });
    }

    // Feed a message from client `clientId` and return the JSON-RPC response.
    std::string simulateMessage(uint8_t clientId, const char* json) {
        return server_->processMessage(clientId, std::string(json));
    }
    std::string simulateMessage(uint8_t clientId, const std::string& json) {
        return server_->processMessage(clientId, json);
    }

    std::string getLastNotification() const {
        return notifications_.empty() ? "" : notifications_.back();
    }

    std::vector<std::string> getAllNotifications() const {
        return notifications_;
    }

    void clearNotifications() { notifications_.clear(); }

private:
    mcp::MCPServer*          server_;
    std::vector<std::string> notifications_;
};

// ---------------------------------------------------------------------------
// Legacy event-type aliases (kept for any existing code that references them)
// ---------------------------------------------------------------------------
enum MockWebSocketEventType {
    WStype_DISCONNECTED,
    WStype_CONNECTED,
    WStype_TEXT,
    WStype_ERROR
};

using MockWebSocketEventCallback =
    std::function<void(uint8_t, MockWebSocketEventType, uint8_t*, size_t)>;
