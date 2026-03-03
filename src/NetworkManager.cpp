#include "NetworkManager.h"
#include <esp_random.h>
#include <ArduinoJson.h>


NetworkManager::NetworkManager() 
    : state(NetworkState::INIT),
      server(80),
      ws("/ws"),
      connectAttempts(0),
      lastConnectAttempt(0) {
}

void NetworkManager::begin() {
    // Initialize LittleFS if not already initialized
    if (!LittleFS.begin(false)) {
        Serial.println("LittleFS Mount Failed - Formatting...");
        if (!LittleFS.begin(true)) {
            Serial.println("LittleFS Mount Failed Even After Format!");
            return;
        }
    }

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
       if (static_cast<system_event_id_t>(event) == SYSTEM_EVENT_STA_DISCONNECTED) {
            if (state == NetworkState::CONNECTED) {
                state = NetworkState::CONNECTION_FAILED;
                queueRequest(NetworkRequest::Type::CHECK_CONNECTION);
            }
        }
    });

    // Create network task
    xTaskCreatePinnedToCore(
        networkTaskCode,
        "NetworkTask",
        8192,
        this,
        1,
        &networkTaskHandle,
        0
    );
    
    if (loadCredentials()) {
        queueRequest(NetworkRequest::Type::CONNECT);
    } else {
        queueRequest(NetworkRequest::Type::START_AP);
    }
    
    setupWebServer();
}

void NetworkManager::setupWebServer() {
    ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                     AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });
    
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->handleRoot(request);
    });
    
    server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        this->handleSave(request);
    });
    
    server.on("/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->handleStatus(request);
    });

    server.on("/app", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (LittleFS.exists("/app.html")) {
            request->send(LittleFS, "/app.html", "text/html");
        } else {
            request->send(404, "text/plain", "Dashboard not found in filesystem");
        }
    });

    server.begin();
}

void NetworkManager::handleRoot(AsyncWebServerRequest *request) {
    if (LittleFS.exists(SETUP_PAGE_PATH)) {
        request->send(LittleFS, SETUP_PAGE_PATH, "text/html");
    } else {
        request->send(500, "text/plain", "Setup page not found in filesystem");
    }
}

void NetworkManager::handleSave(AsyncWebServerRequest *request) {
    if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
        request->send(400, "text/plain", "Missing parameters");
        return;
    }
    
    String ssid = request->getParam("ssid", true)->value();
    String password = request->getParam("password", true)->value();
    
    if (ssid.isEmpty()) {
        request->send(400, "text/plain", "SSID cannot be empty");
        return;
    }
    
    saveCredentials(ssid, password);
    request->send(200, "text/plain", "Credentials saved");
}

void NetworkManager::handleStatus(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print(getNetworkStatusJson(state, getSSID(), getIPAddress()));
    request->send(response);
}

void NetworkManager::onWebSocketEvent(AsyncWebSocket* server, 
                                    AsyncWebSocketClient* client,
                                    AwsEventType type, 
                                    void* arg, 
                                    uint8_t* data, 
                                    size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            client->text(getNetworkStatusJson(state, getSSID(), getIPAddress()));
            break;
        case WS_EVT_DISCONNECT:
            break;
        case WS_EVT_ERROR:
            break;
        case WS_EVT_DATA:
            break;
    }
}

void NetworkManager::networkTaskCode(void* parameter) {
    NetworkManager* manager = static_cast<NetworkManager*>(parameter);
    manager->networkTask();
}

void NetworkManager::networkTask() {
    NetworkRequest request;
    TickType_t lastCheck = xTaskGetTickCount();
    
    while (true) {
        // Process any pending requests
        if (requestQueue.pop(request)) {
            handleRequest(request);
        }
        
        // Periodic connection check
        if (state == NetworkState::CONNECTED && 
            (xTaskGetTickCount() - lastCheck) >= pdMS_TO_TICKS(RECONNECT_INTERVAL)) {
            queueRequest(NetworkRequest::Type::CHECK_CONNECTION);
            lastCheck = xTaskGetTickCount();
        }
        
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void NetworkManager::handleRequest(const NetworkRequest& request) {
    switch (request.type) {
        case NetworkRequest::Type::CONNECT:
            connect();
            break;
        case NetworkRequest::Type::START_AP:
            startAP();
            break;
        case NetworkRequest::Type::CHECK_CONNECTION:
            checkConnection();
            break;
    }
}

void NetworkManager::connect() {
    if (!credentials.valid || connectAttempts >= MAX_CONNECT_ATTEMPTS) {
        startAP();
        return;
    }

    state = NetworkState::CONNECTING;
    WiFi.mode(WIFI_STA);
    WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());
    
    connectAttempts++;
    lastConnectAttempt = millis();

    // Schedule a connection check
    queueRequest(NetworkRequest::Type::CHECK_CONNECTION);
}

void NetworkManager::checkConnection() {
    if (state == NetworkState::CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            state = NetworkState::CONNECTED;
            connectAttempts = 0;
            ws.textAll(getNetworkStatusJson(state, getSSID(), getIPAddress()));
        } else if (millis() - lastConnectAttempt >= CONNECT_TIMEOUT) {
            state = NetworkState::CONNECTION_FAILED;
            queueRequest(NetworkRequest::Type::CONNECT);
        } else {
            queueRequest(NetworkRequest::Type::CHECK_CONNECTION);
        }
    } else if (state == NetworkState::CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            state = NetworkState::CONNECTION_FAILED;
            queueRequest(NetworkRequest::Type::CONNECT);
        }
    }
}

void NetworkManager::startAP() {
    state = NetworkState::AP_MODE;
    WiFi.mode(WIFI_AP);
    
    if (apSSID.isEmpty()) {
        apSSID = generateUniqueSSID();
    }
    
    WiFi.softAP(apSSID.c_str());
    ws.textAll(getNetworkStatusJson(state, apSSID, WiFi.softAPIP().toString()));
}

String NetworkManager::generateUniqueSSID() {
    uint32_t chipId = (uint32_t)esp_random();
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "ESP32_%08X", chipId);
    return String(ssid);
}

bool NetworkManager::loadCredentials() {
    preferences.begin("network", true);
    credentials.ssid = preferences.getString("ssid", "");
    credentials.password = preferences.getString("pass", "");
    preferences.end();
    
    credentials.valid = !credentials.ssid.isEmpty();
    return credentials.valid;
}

void NetworkManager::saveCredentials(const String& ssid, const String& password) {
    preferences.begin("network", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    preferences.end();
    
    credentials.ssid = ssid;
    credentials.password = password;
    credentials.valid = true;
    
    connectAttempts = 0;
    queueRequest(NetworkRequest::Type::CONNECT);
}

void NetworkManager::clearCredentials() {
    preferences.begin("network", false);
    preferences.clear();
    preferences.end();
    
    credentials.ssid = "";
    credentials.password = "";
    credentials.valid = false;
}
String NetworkManager::getNetworkStatusJson(NetworkState state, const String& ssid, const String& ip) {
    JsonDocument doc; // Ensure you include <ArduinoJson.h>

    switch (state) {
        case NetworkState::CONNECTED:
            doc["status"] = "connected";
            break;
        case NetworkState::CONNECTING:
            doc["status"] = "connecting";
            break;
        case NetworkState::AP_MODE:
            doc["status"] = "ap_mode";
            break;
        case NetworkState::CONNECTION_FAILED:
            doc["status"] = "connection_failed";
            break;
        default:
            doc["status"] = "initializing";
    }

    doc["ssid"] = ssid;
    doc["ip"] = ip;

    String response;
    serializeJson(doc, response);
    return response;
}


bool NetworkManager::isConnected() {
    return state == NetworkState::CONNECTED && WiFi.status() == WL_CONNECTED;
}

String NetworkManager::getIPAddress() {
    return state == NetworkState::AP_MODE ? 
           WiFi.softAPIP().toString() : 
           WiFi.localIP().toString();
}

String NetworkManager::getSSID() {
    return state == NetworkState::AP_MODE ? 
           apSSID : 
           credentials.ssid;
}

void NetworkManager::queueRequest(NetworkRequest::Type type, const String &message) {
    if (!requestQueue.push({type, message})) {
        Serial.println("Request queue is full!");
    }
}