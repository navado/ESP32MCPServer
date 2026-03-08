#pragma once

#if BOARD_HAS_WIFI

#include <Arduino.h>
#include <Preferences.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>

// ---------------------------------------------------------------------------
// OTAManager — password-protected Over-The-Air firmware update service.
//
// Routes registered on the shared AsyncWebServer:
//   GET  /ota          — Serves the OTA upload page (from LittleFS /ota.html,
//                        or a built-in minimal HTML form if the file is absent).
//   GET  /ota/status   — Public JSON endpoint: running fw version + last result.
//   POST /ota/update   — Accepts a multipart firmware binary.  Requires HTTP
//                        Basic Auth (username "admin", password as configured).
//   GET  /ota/config   — Requires auth. Returns OTA configuration JSON.
//   POST /ota/config   — Requires auth with current password.  Accepts form
//                        field "password" to update the OTA password.
//
// Password storage:
//   Stored in NVS namespace "ota", key "password".
//   Default on first boot: "admin"  (change immediately after flashing!).
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
    static constexpr const char* OTA_USERNAME   = "admin";
    static constexpr const char* NVS_NS         = "ota";
    static constexpr const char* NVS_PWD_KEY    = "password";
    static constexpr const char* DEFAULT_PWD    = "admin";

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
