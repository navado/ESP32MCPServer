#pragma once

// TLS identity helpers (issue #23) — pure, unit tested (test_tls_core).
//
// The device makes its own self-signed certificate on first boot; these
// helpers build its subjectAltName extension and format fingerprints.

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace mcp {
namespace tls {

// "a.b.c.d" -> 4 bytes.
inline bool parseIPv4(const std::string& s, std::array<uint8_t, 4>& out) {
    unsigned a, b, c, d;
    char tail;
    if (sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    out = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c),
           static_cast<uint8_t>(d)};
    return true;
}

// DER length octets.
inline void derLength(std::vector<uint8_t>& out, size_t len) {
    if (len < 0x80) {
        out.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFF) {
        out.push_back(0x81);
        out.push_back(static_cast<uint8_t>(len));
    } else {
        out.push_back(0x82);
        out.push_back(static_cast<uint8_t>(len >> 8));
        out.push_back(static_cast<uint8_t>(len & 0xFF));
    }
}

// SubjectAltName ::= SEQUENCE OF GeneralName, with dNSName [2] and
// iPAddress [7] entries (RFC 5280 4.2.1.6).  mbedtls 2.28 has no setter for
// it, so the extension value is passed to mbedtls_x509write_crt_set_extension.
inline std::vector<uint8_t> subjectAltNameDer(const std::vector<std::string>& dnsNames,
                                              const std::vector<std::array<uint8_t, 4>>& ips) {
    std::vector<uint8_t> body;
    for (const auto& n : dnsNames) {
        if (n.empty()) continue;
        body.push_back(0x82);
        derLength(body, n.size());
        body.insert(body.end(), n.begin(), n.end());
    }
    for (const auto& ip : ips) {
        body.push_back(0x87);
        body.push_back(4);
        body.insert(body.end(), ip.begin(), ip.end());
    }
    std::vector<uint8_t> out;
    out.push_back(0x30);
    derLength(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// "AB:CD:..." (upper-case hex, colon separated), the form browsers and
// `openssl x509 -fingerprint -sha256` print.
inline std::string fingerprintHex(const uint8_t* digest, size_t n) {
    static const char* const kHex = "0123456789ABCDEF";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ':';
        s += kHex[digest[i] >> 4];
        s += kHex[digest[i] & 0xF];
    }
    return s;
}

// Close frame payload: 2-byte big-endian status code, then the reason.
inline std::string wsClosePayload(uint16_t code, const std::string& reason) {
    std::string p;
    p += static_cast<char>(code >> 8);
    p += static_cast<char>(code & 0xFF);
    p += reason.substr(0, 123);   // control frames carry at most 125 bytes
    return p;
}

} // namespace tls
} // namespace mcp
