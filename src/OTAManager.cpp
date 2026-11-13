#include "board_config.h"

#if BOARD_HAS_WIFI

#include "OTAManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_random.h>

// ---------------------------------------------------------------------------
OTAManager::OTAManager() {}

String OTAManager::generatePassword() {
    return String(mcp::auth::generateToken([]() { return esp_random(); }, 12).c_str());
}

void OTAManager::begin() {
    preferences_.begin(OTA_NVS_NAMESPACE, /*readOnly=*/true);
    password_ = preferences_.getString(OTA_NVS_PWD_KEY, "");
    preferences_.end();
    // No working default: generate a per-device password on first boot, and
    // replace the old compiled-in "admin" default if a device still has it.
    if (password_.isEmpty() || password_ == OTA_LEGACY_DEFAULT_PASSWORD) {
        setPassword(generatePassword());
        Serial.println("[OTA] ==================================================");
        Serial.printf ("[OTA] Generated OTA password (user '%s'): %s\n",
                       OTA_USERNAME, password_.c_str());
        Serial.println("[OTA] Shown once. 'ota show' / 'ota rotate' on this console.");
        Serial.println("[OTA] ==================================================");
    } else {
        Serial.println("[OTA] Manager ready (password is set)");
    }
}

String OTAManager::rotatePassword() {
    String p = generatePassword();
    preferences_.begin(OTA_NVS_NAMESPACE, /*readOnly=*/false);
    preferences_.putString(OTA_NVS_PWD_KEY, p);
    preferences_.end();
    return p;
}

void OTAManager::setPassword(const String& newPassword) {
    if (newPassword.isEmpty()) return;
    preferences_.begin(OTA_NVS_NAMESPACE, /*readOnly=*/false);
    preferences_.putString(OTA_NVS_PWD_KEY, newPassword);
    preferences_.end();
    password_ = newPassword;
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void OTAManager::registerRoutes(AsyncWebServer& server) {
    // Public: OTA upload page
    server.on("/ota", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handlePageGet(req);
    });

    // Public: firmware status
    server.on("/ota/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleStatusGet(req);
    });

    // Protected: firmware upload  (multipart POST)
    server.on("/ota/update", HTTP_POST,
        [this](AsyncWebServerRequest* req) { handleUpdatePost(req); },
        [this](AsyncWebServerRequest* req, const String& fn,
               size_t idx, uint8_t* data, size_t len, bool final) {
            handleUpdateUpload(req, fn, idx, data, len, final);
        });

    // Protected: read / change OTA config
    server.on("/ota/config", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleConfigGet(req);
    });
    server.on("/ota/config", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handleConfigPost(req);
    });
}

// ---------------------------------------------------------------------------
// Authentication
// ---------------------------------------------------------------------------

uint32_t OTAManager::sourceKey(AsyncWebServerRequest* request) {
    AsyncClient* c = request->client();
    return c ? static_cast<uint32_t>(c->remoteIP()) : 0u;
}

bool OTAManager::authenticate(AsyncWebServerRequest* request, bool countFailure) const {
    const uint32_t now = millis();
    const uint32_t src = sourceKey(request);
    if (lockouts_.locked(src, now)) return false;
    // A request without credentials is the normal first step of the browser
    // Basic Auth flow; only WRONG credentials count as failures.
    const bool hasCreds = request->hasHeader("Authorization");
    const bool ok = request->authenticate(OTA_USERNAME, password_.c_str());
    if (ok) {
        lockouts_.recordSuccess(src);
    } else if (hasCreds && countFailure) {
        lockouts_.recordFailure(src, now);
        if (lockouts_.locked(src, now)) {
            Serial.printf("[OTA] repeated failed logins from %s: locked for %u ms\n",
                          request->client() ? request->client()->remoteIP().toString().c_str() : "?",
                          (unsigned)lockouts_.retryAfterMs(src, now));
        }
    }
    return ok;
}

bool OTAManager::requireAuth(AsyncWebServerRequest* request) const {
    if (authenticate(request)) return true;
    const uint32_t now = millis();
    const uint32_t src = sourceKey(request);
    if (lockouts_.locked(src, now)) {
        AsyncWebServerResponse* resp = request->beginResponse(
            429, "text/plain", "Too many failed logins; try again later");
        resp->addHeader("Retry-After", String((lockouts_.retryAfterMs(src, now) + 999) / 1000));
        request->send(resp);
        return false;
    }
    request->requestAuthentication();
    return false;
}

// ---------------------------------------------------------------------------
// GET /ota  — minimal upload form (or LittleFS /ota.html if present)
// ---------------------------------------------------------------------------

void OTAManager::handlePageGet(AsyncWebServerRequest* request) const {
    if (LittleFS.exists("/ota.html")) {
        request->send(LittleFS, "/ota.html", "text/html");
        return;
    }
    // Built-in fallback: a minimal inline form.
    static const char HTML[] =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>OTA Update</title></head><body>"
        "<h2>ESP32 OTA Firmware Update</h2>"
        "<form method='POST' action='/ota/update' enctype='multipart/form-data'>"
        "<p>Firmware binary (.bin):</p>"
        "<input type='file' name='firmware' accept='.bin' required><br><br>"
        "<input type='submit' value='Upload &amp; Flash'>"
        "</form>"
        "<p><em>Authentication required: username <b>" OTA_USERNAME "</b>, password as configured.</em></p>"
        "<p><a href='/ota/status'>Status JSON</a></p>"
        "</body></html>";
    request->send(200, "text/html", HTML);
}

// ---------------------------------------------------------------------------
// GET /ota/status  — public JSON
// ---------------------------------------------------------------------------

String OTAManager::getStatusJson() const {
    JsonDocument doc;

    const esp_partition_t* part = esp_ota_get_running_partition();
    if (part) {
        doc["partition"] = part->label;
    }

    const esp_app_desc_t* appDesc = esp_ota_get_app_description();
    if (appDesc) {
        doc["version"]    = appDesc->version;
        doc["idfVersion"] = appDesc->idf_ver;
        doc["buildDate"]  = appDesc->date;
        doc["buildTime"]  = appDesc->time;
    }

    if (uploadDone_) {
        doc["lastUpdate"] = uploadError_ ? "failed" : "success";
    }
    if (uploadError_ && !lastErrorMsg_.isEmpty()) {
        doc["lastError"] = lastErrorMsg_;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

void OTAManager::handleStatusGet(AsyncWebServerRequest* request) const {
    AsyncResponseStream* resp = request->beginResponseStream("application/json");
    resp->print(getStatusJson());
    request->send(resp);
}

// ---------------------------------------------------------------------------
// POST /ota/update  — authenticated firmware upload
// ---------------------------------------------------------------------------

void OTAManager::handleUpdatePost(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;

    bool ok = !Update.hasError();
    String msg = ok ? "Update successful — rebooting." : ("Update failed: " + lastErrorMsg_);

    AsyncWebServerResponse* resp = request->beginResponse(
        ok ? 200 : 500, "text/plain", msg);
    resp->addHeader("Connection", "close");
    request->send(resp);

    if (ok) {
        delay(500);
        ESP.restart();
    }
}

void OTAManager::handleUpdateUpload(AsyncWebServerRequest* request,
                                    const String& /*filename*/,
                                    size_t index, uint8_t* data,
                                    size_t len, bool final) {
    // Re-check auth on each chunk to prevent unauthenticated stream injection,
    // but count a wrong password once per request, not once per chunk.
    if (!authenticate(request, /*countFailure=*/index == 0)) return;

    if (index == 0) {
        uploadError_ = false;
        uploadDone_  = false;
        lastErrorMsg_ = "";
        Serial.printf("[OTA] Upload started, free heap: %u\n", (unsigned)ESP.getFreeHeap());

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            uploadError_  = true;
            lastErrorMsg_ = Update.errorString();
            Serial.printf("[OTA] begin() failed: %s\n", lastErrorMsg_.c_str());
            return;
        }
    }

    if (uploadError_) return;   // skip remaining chunks after an error

    if (Update.write(data, len) != len) {
        uploadError_  = true;
        lastErrorMsg_ = Update.errorString();
        Serial.printf("[OTA] write() failed: %s\n", lastErrorMsg_.c_str());
        return;
    }

    if (final) {
        uploadDone_ = true;
        if (!Update.end(true)) {
            uploadError_  = true;
            lastErrorMsg_ = Update.errorString();
            Serial.printf("[OTA] end() failed: %s\n", lastErrorMsg_.c_str());
        } else {
            Serial.println("[OTA] Upload complete — awaiting reboot.");
        }
    }
}

// ---------------------------------------------------------------------------
// GET /ota/config  — protected
// ---------------------------------------------------------------------------

void OTAManager::handleConfigGet(AsyncWebServerRequest* request) const {
    if (!requireAuth(request)) return;
    JsonDocument doc;
    doc["username"]        = OTA_USERNAME;
    doc["passwordIsSet"]   = true;
    String json;
    serializeJson(doc, json);
    AsyncResponseStream* resp = request->beginResponseStream("application/json");
    resp->print(json);
    request->send(resp);
}

// ---------------------------------------------------------------------------
// POST /ota/config  — change password (requires current credentials)
// ---------------------------------------------------------------------------

void OTAManager::handleConfigPost(AsyncWebServerRequest* request) {
    if (!requireAuth(request)) return;
    if (!request->hasParam("password", true)) {
        request->send(400, "text/plain", "Missing 'password' field");
        return;
    }
    String newPwd = request->getParam("password", true)->value();
    if (newPwd.length() < OTA_MIN_PASSWORD_LEN) {
        request->send(400, "text/plain",
            "Password must be at least " + String(OTA_MIN_PASSWORD_LEN) + " characters");
        return;
    }
    setPassword(newPwd);
    request->send(200, "text/plain", "OTA password updated");
}

#endif // BOARD_HAS_WIFI
