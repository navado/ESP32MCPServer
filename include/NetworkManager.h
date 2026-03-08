#pragma once

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "RequestQueue.h"

// Forward declarations — avoid pulling ESP32-only headers into every TU.
class DiscoveryManager;
namespace mcp { class BusHistory; }
#ifndef NATIVE_TEST
class OTAManager;
#endif

enum class NetworkState {
    INIT,
    CONNECTING,
    CONNECTED,
    CONNECTION_FAILED,
    AP_MODE
};

struct NetworkCredentials {
    String ssid;
    String password;
    bool valid;
};

struct NetworkRequest {
    enum class Type {
        CONNECT,
        START_AP,
        CHECK_CONNECTION
    };
    
    Type type;
    String data;
};

class NetworkManager {
public:
    NetworkManager();
    void begin();
    bool isConnected();
    String getIPAddress();
    String getSSID();

    // Save WiFi credentials and trigger a connection attempt.
    // Exposed as public so tests can set credentials without HTTP.
    void saveCredentials(const String& ssid, const String& password);

    // Wire in a DiscoveryManager so NetworkManager can notify it when the
    // network connects or disconnects.  Call before begin().
    void setDiscoveryManager(DiscoveryManager* dm);

    // Wire in a BusHistory so the /bus-history endpoint can read/write config.
    // Call before begin().
    void setBusHistory(mcp::BusHistory* bh);

#ifndef NATIVE_TEST
    // Wire in an OTAManager so its /ota/* routes are registered on the server.
    // Call before begin().
    void setOTAManager(OTAManager* ota);
#endif

    // Process one pending network request synchronously (used in tests
    // where the FreeRTOS background task is not running).
    void handleClient();

private:
    static constexpr uint32_t CONNECT_TIMEOUT = 15000; // 15 seconds
    static constexpr uint8_t MAX_CONNECT_ATTEMPTS = 3;
    static constexpr uint16_t RECONNECT_INTERVAL = 5000; // 5 seconds
    static constexpr const char* SETUP_PAGE_PATH = "/wifi_setup.html";

    NetworkState state;
    Preferences preferences;
    AsyncWebServer server;
    AsyncWebSocket ws;
    RequestQueue<NetworkRequest> requestQueue;
    TaskHandle_t networkTaskHandle;
    
    String apSSID;
    uint8_t connectAttempts;
    uint32_t lastConnectAttempt;
    NetworkCredentials credentials;
    DiscoveryManager*  discovery_   = nullptr;
    mcp::BusHistory*   busHistory_  = nullptr;
#ifndef NATIVE_TEST
    OTAManager*        ota_         = nullptr;
#endif

    void setupWebServer();
    void handleRequest(const NetworkRequest& request);
    
    void startAP();
    void connect();
    void checkConnection();
    
    bool loadCredentials();
    void clearCredentials();
    
    String generateUniqueSSID();
    void queueRequest(NetworkRequest::Type type, const String& data = "");

    // Web handlers
    void handleRoot(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request);
    void handleStatus(AsyncWebServerRequest *request);
    void handleDiscoveryGet(AsyncWebServerRequest *request);
    void handleDiscoveryPost(AsyncWebServerRequest *request);
    void handleBusHistoryGet(AsyncWebServerRequest *request);
    void handleBusHistoryPost(AsyncWebServerRequest *request);
    void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                         AwsEventType type, void* arg, uint8_t* data, size_t len);
    
    static void networkTaskCode(void* parameter);
    void networkTask();
    static String getNetworkStatusJson(NetworkState state, const String& ssid, const String& ip);
};