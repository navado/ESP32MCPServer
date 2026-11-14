#pragma once

// Per-device TLS identity (issue #23): an EC P-256 key and a self-signed
// certificate, generated on first boot and kept in NVS namespace "tls".
// Clients trust it by pinning the SHA-256 fingerprint the serial console
// prints (`tls show`), or by importing the PEM served at /tls.pem.

#include <string>
#include <vector>

namespace mcp {
namespace tls {

struct Identity {
    std::string certPem;
    std::string keyPem;
    std::string fingerprint;   // SHA-256 of the DER certificate, AB:CD:...
    std::vector<std::string> names;   // subjectAltName entries, for the log
};

// Load the stored identity, or create one for `hostname` (e.g.
// "esp32-mcp-008a2c": the cert covers <hostname>.local, <hostname> and
// `ipv4` if given).  `forceNew` replaces a stored one.  False on failure
// (`err` says why).
bool loadOrCreateIdentity(const std::string& hostname, const std::string& ipv4,
                          bool forceNew, Identity& out, std::string& err);

// Drop the stored identity (a new one is made on the next boot).
void forgetIdentity();

// Route mbedTLS allocations to PSRAM (falls back to internal RAM): a TLS
// session needs ~40 KB of buffers, more than the internal heap left beside
// WiFi and BLE.  Call once, early in setup(), only when PSRAM is present.
void useSpiramForMbedtls();

} // namespace tls
} // namespace mcp
