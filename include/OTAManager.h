#pragma once

#if BOARD_HAS_WIFI

#include <Arduino.h>
#include <Preferences.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>

// ---------------------------------------------------------------------------
// Build-flag tunables — override any of these in platformio.ini build_flags.
// Fallback defaults are provided here so the header compiles stand-alone.
//
//   OTA_USERNAME          HTTP Basic Auth username                ("admin")
//   OTA_NVS_NAMESPACE     NVS namespace used to persist password  ("ota")
//   OTA_NVS_PWD_KEY       NVS key for the password value          ("password")
//   OTA_DEFAULT_PASSWORD  Password used on first boot             ("admin")
//   OTA_MIN_PASSWORD_LEN  Minimum acceptable password length      (8)
// ---------------------------------------------------------------------------
#ifndef OTA_USERNAME
#  define OTA_USERNAME         "admin"
#endif
#ifndef OTA_NVS_NAMESPACE
#  define OTA_NVS_NAMESPACE    "ota"
#endif
#ifndef OTA_NVS_PWD_KEY
#  define OTA_NVS_PWD_KEY      "password"
#endif
#ifndef OTA_DEFAULT_PASSWORD
#  define OTA_DEFAULT_PASSWORD "admin"
#endif
#ifndef OTA_MIN_PASSWORD_LEN
#  define OTA_MIN_PASSWORD_LEN 8
#endif

// ---------------------------------------------------------------------------
// OTAManager — password-protected Over-The-Air firmware update service.
//
// Routes registered on the shared AsyncWebServer:
//   GET  /ota          — Serves the OTA upload page (from LittleFS /ota.html,
//                        or a built-in minimal HTML form if the file is absent).
//   GET  /ota/status   — Public JSON endpoint: running fw version + last result.
//   POST /ota/update   — Accepts a multipart firmware binary.  Requires HTTP
//                        Basic Auth (username OTA_USERNAME, password as configured).
//   GET  /ota/config   — Requires auth. Returns OTA configuration JSON.
//   POST /ota/config   — Requires auth with current password.  Accepts form
//                        field "password" to update the OTA password.
//
// Password storage:
//   Stored in NVS namespace OTA_NVS_NAMESPACE, key OTA_NVS_PWD_KEY.
//   Default on first boot: OTA_DEFAULT_PASSWORD  (change immediately after flashing!).
//
// Usage:
//   OTAManager otaManager;
//   otaManager.begin();
//   otaManager.registerRoutes(server);   // call before server.begin()
// ---------------------------------------------------------------------------

class OTAManager {
public:
    OTAManager();

    // Load password from NVS.  Call before registerRoutes().
    void begin();

    // Directly set the OTA password and persist it to NVS.
    void setPassword(const String& newPassword);

    // Register all /ota/* routes on the provided web server.
    void registerRoutes(AsyncWebServer& server);

    // JSON representation of the last OTA attempt and firmware info.
    String getStatusJson() const;

private:
    Preferences preferences_;
    String      password_;          // current OTA password (plaintext in NVS)
    bool        uploadError_    = false;
    String      lastErrorMsg_;
    bool        uploadDone_     = false;

    // Returns true when the request carries valid Basic Auth credentials.
    bool authenticate(AsyncWebServerRequest* request) const;

    // HTTP handler helpers.
    void handlePageGet(AsyncWebServerRequest* request) const;
    void handleStatusGet(AsyncWebServerRequest* request) const;
    void handleUpdatePost(AsyncWebServerRequest* request);
    void handleUpdateUpload(AsyncWebServerRequest* request,
                            const String& filename,
                            size_t index, uint8_t* data, size_t len, bool final);
    void handleConfigGet(AsyncWebServerRequest* request) const;
    void handleConfigPost(AsyncWebServerRequest* request);
};

#endif // BOARD_HAS_WIFI
