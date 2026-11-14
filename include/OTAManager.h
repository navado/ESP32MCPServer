#pragma once

#if BOARD_HAS_WIFI

#include <Arduino.h>
#include <Preferences.h>
#include <Update.h>
#include "AuthPolicy.h"
#include <ESPAsyncWebServer.h>

// ---------------------------------------------------------------------------
// Build-flag tunables — override any of these in platformio.ini build_flags.
// Fallback defaults are provided here so the header compiles stand-alone.
//
//   OTA_USERNAME          HTTP Basic Auth username                ("admin")
//   OTA_NVS_NAMESPACE     NVS namespace used to persist password  ("ota")
//   OTA_NVS_PWD_KEY       NVS key for the password value          ("password")
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
// Legacy compiled-in default.  Never accepted: a device still holding it in
// NVS gets a random password on the next boot (issue #12).
#define OTA_LEGACY_DEFAULT_PASSWORD "admin"
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
//   There is no compiled-in default: on first boot (or if NVS still holds the
//   old "admin" default) a random per-device password is generated and
//   printed once on the serial console.  `ota show` / `ota rotate` on the
//   serial console display or replace it.
//   Failed logins: after 5 consecutive bad credentials from one IP, that IP's
//   attempts are refused with 429 for 30 s, doubling per further failure up
//   to 5 min.  Other hosts are unaffected.
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

    // Current password (serial console `ota show`).
    const String& getPassword() const { return password_; }

    // Generate and persist a new random password; takes effect after reboot
    // (the HTTP task may be using the current one).  Returns the new password.
    String rotatePassword();

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

    // Returns true when the request carries valid Basic Auth credentials and
    // its source IP is not locked out.  Wrong credentials count as a failure
    // for that IP when `countFailure` (once per request, not per chunk).
    bool authenticate(AsyncWebServerRequest* request, bool countFailure = true) const;
    // authenticate(), answering 429 (locked out) or 401 itself on failure.
    bool requireAuth(AsyncWebServerRequest* request) const;
    static uint32_t sourceKey(AsyncWebServerRequest* request);

    // Per-source-IP lockout: a host that keeps guessing locks out only itself.
    // 32 sources (~1 KB): an attacker needs more than 32 concurrently failing
    // LAN addresses before untracked hosts share the overflow lockout.
    mutable mcp::auth::LockoutTable lockouts_{32, 5, 30000, 300000};
    static String generatePassword();

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
