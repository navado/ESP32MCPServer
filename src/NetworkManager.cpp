#include "NetworkManager.h"
#include "DiscoveryManager.h"
#include "BusHistory.h"
#ifndef NATIVE_TEST
#  include "board_config.h"
#  include "OTAManager.h"
#endif
#include <esp_random.h>
#include <ArduinoJson.h>


NetworkManager::NetworkManager() 
    : state(NetworkState::INIT),
      server(80),
      ws("/ws"),
      connectAttempts(0),
      lastConnectAttempt(0) {
}

void NetworkManager::setDiscoveryManager(DiscoveryManager* dm) {
    discovery_ = dm;
}

void NetworkManager::setBusHistory(mcp::BusHistory* bh) {
    busHistory_ = bh;
}

#ifndef NATIVE_TEST
void NetworkManager::setOTAManager(OTAManager* ota) {
    ota_ = ota;
}
#endif

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
                if (discovery_) discovery_->onNetworkDisconnected();
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

    server.on("/discovery", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->handleDiscoveryGet(request);
    });

    server.on("/discovery", HTTP_POST, [this](AsyncWebServerRequest *request) {
        this->handleDiscoveryPost(request);
    });

    server.on("/bus-history", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->handleBusHistoryGet(request);
    });

    server.on("/bus-history", HTTP_POST, [this](AsyncWebServerRequest *request) {
        this->handleBusHistoryPost(request);
    });

    // Serve the bus history config UI page from LittleFS.
    server.on("/bus-history-config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (LittleFS.exists("/bus_history_config.html")) {
            request->send(LittleFS, "/bus_history_config.html", "text/html");
        } else {
            request->send(404, "text/plain", "Bus history config page not found");
        }
    });

#ifndef NATIVE_TEST
    if (ota_) {
        ota_->registerRoutes(server);
    }
#endif

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
            if (discovery_) discovery_->onNetworkConnected(getIPAddress());
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
    if (discovery_) discovery_->onNetworkConnected(WiFi.softAPIP().toString());
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

void NetworkManager::handleClient() {
    NetworkRequest request;
    if (requestQueue.pop(request)) {
        handleRequest(request);
    }
}

void NetworkManager::handleDiscoveryGet(AsyncWebServerRequest *request) {
    if (!discovery_) {
        request->send(503, "application/json", "{\"error\":\"Discovery not configured\"}");
        return;
    }
    DiscoveryConfig cfg = discovery_->getConfig();
    JsonDocument doc;
    doc["hostname"]          = cfg.hostname;
    doc["fqdn"]              = String(cfg.hostname) + ".local";
    doc["ip"]                = getIPAddress();
    doc["mcpPort"]           = cfg.mcpPort;
    doc["httpPort"]          = cfg.httpPort;
    doc["broadcastInterval"] = cfg.broadcastInterval;
    doc["broadcastPort"]     = cfg.broadcastPort;
    doc["mdnsActive"]        = discovery_->isMdnsActive();
    String json;
    serializeJson(doc, json);
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print(json);
    request->send(response);
}

void NetworkManager::handleDiscoveryPost(AsyncWebServerRequest *request) {
    if (!discovery_) {
        request->send(503, "application/json", "{\"error\":\"Discovery not configured\"}");
        return;
    }
    if (request->hasParam("hostname", true)) {
        String h = request->getParam("hostname", true)->value();
        if (!h.isEmpty()) discovery_->setHostname(h);
    }
    if (request->hasParam("broadcastInterval", true)) {
        String bStr = request->getParam("broadcastInterval", true)->value();
        discovery_->setBroadcastInterval((uint32_t)bStr.toInt());
    }
    handleDiscoveryGet(request);
}

// ---------------------------------------------------------------------------
// /bus-history  GET — return current config + allocated sizes + free heap
// /bus-history  POST — update config (form params), persist, reallocate
// ---------------------------------------------------------------------------

void NetworkManager::handleBusHistoryGet(AsyncWebServerRequest *request) {
    if (!busHistory_) {
        request->send(503, "application/json",
                      "{\"error\":\"Bus history not configured\"}");
        return;
    }
    std::string json = busHistory_->configToJson();
    AsyncResponseStream *resp = request->beginResponseStream("application/json");
    resp->print(json.c_str());
    request->send(resp);
}

void NetworkManager::handleBusHistoryPost(AsyncWebServerRequest *request) {
    if (!busHistory_) {
        request->send(503, "application/json",
                      "{\"error\":\"Bus history not configured\"}");
        return;
    }
    mcp::BusHistoryConfig cfg = busHistory_->getConfig();

    auto applyUint = [&](const char* key, uint32_t& field) {
        if (request->hasParam(key, true)) {
            field = (uint32_t)request->getParam(key, true)->value().toInt();
        }
    };
    applyUint("canFrameCount",     cfg.canFrameCount);
    applyUint("nmeaLineCount",     cfg.nmeaLineCount);
    applyUint("nmea2000Count",     cfg.nmea2000Count);
    applyUint("obdiiCount",        cfg.obdiiCount);
    applyUint("ramBudgetBytes",    cfg.ramBudgetBytes);
    applyUint("safetyMarginBytes", cfg.safetyMarginBytes);

    busHistory_->setConfig(cfg); // persist, reallocate, announces via callback

    handleBusHistoryGet(request); // return updated status
}