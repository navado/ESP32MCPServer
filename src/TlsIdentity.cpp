#include "TlsIdentity.h"

#if MCP_TLS && !defined(NATIVE_TEST)

#include "TlsCore.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>
#include <memory>

namespace mcp {
namespace tls {

namespace {

const char* const kNs = "tls";

void* spiramCalloc(size_t n, size_t size) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

bool fingerprintOf(const std::string& certPem, std::string& fp) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int rc = mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(certPem.c_str()),
                                    certPem.size() + 1);
    if (rc == 0) {
        uint8_t digest[32];
        mbedtls_sha256_ret(crt.raw.p, crt.raw.len, digest, 0);
        fp = fingerprintHex(digest, sizeof(digest));
    }
    mbedtls_x509_crt_free(&crt);
    return rc == 0;
}

bool create(const std::string& hostname, const std::string& ipv4, Identity& out, std::string& err) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_mpi serial;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_mpi_init(&serial);
    std::unique_ptr<unsigned char[]> buf(new unsigned char[2048]);
    bool ok = false;
    do {
        const char* pers = "esp32-mcp-tls";
        if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                  reinterpret_cast<const unsigned char*>(pers), strlen(pers))) {
            err = "rng seed failed"; break;
        }
        if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) ||
            mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                                mbedtls_ctr_drbg_random, &drbg)) {
            err = "key generation failed"; break;
        }
        unsigned char sn[16];
        mbedtls_ctr_drbg_random(&drbg, sn, sizeof(sn));
        sn[0] &= 0x7F;   // positive serial
        mbedtls_mpi_read_binary(&serial, sn, sizeof(sn));

        const std::string subject = "CN=" + hostname + ".local,O=ESP32 MCP Server";
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);
        if (mbedtls_x509write_crt_set_subject_name(&crt, subject.c_str()) ||
            mbedtls_x509write_crt_set_issuer_name(&crt, subject.c_str()) ||
            mbedtls_x509write_crt_set_serial(&crt, &serial) ||
            mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "20491231235959")) {
            err = "certificate fields failed"; break;
        }
        // Self-signed trust anchor for its own name: CA:TRUE, pathlen 0.
        mbedtls_x509write_crt_set_basic_constraints(&crt, 1, 0);
        mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE |
                                                       MBEDTLS_X509_KU_KEY_CERT_SIGN);
        mbedtls_x509write_crt_set_subject_key_identifier(&crt);
        mbedtls_x509write_crt_set_authority_key_identifier(&crt);

        out.names = {hostname + ".local", hostname};
        std::vector<std::array<uint8_t, 4>> ips;
        std::array<uint8_t, 4> ip{};
        if (parseIPv4(ipv4, ip) && ipv4 != "0.0.0.0") {
            ips.push_back(ip);
            out.names.push_back(ipv4);
        }
        const std::vector<uint8_t> san = subjectAltNameDer({hostname + ".local", hostname}, ips);
        if (mbedtls_x509write_crt_set_extension(
                &crt, MBEDTLS_OID_SUBJECT_ALT_NAME, MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME),
                0, san.data(), san.size())) {
            err = "subjectAltName failed"; break;
        }
        if (mbedtls_x509write_crt_pem(&crt, buf.get(), 2048, mbedtls_ctr_drbg_random, &drbg)) {
            err = "certificate signing failed"; break;
        }
        out.certPem = reinterpret_cast<char*>(buf.get());
        if (mbedtls_pk_write_key_pem(&key, buf.get(), 2048)) {
            err = "key export failed"; break;
        }
        out.keyPem = reinterpret_cast<char*>(buf.get());
        memset(buf.get(), 0, 2048);
        ok = fingerprintOf(out.certPem, out.fingerprint);
        if (!ok) err = "certificate does not parse";
    } while (false);
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ok;
}

}  // namespace

void useSpiramForMbedtls() {
    mbedtls_platform_set_calloc_free(spiramCalloc, heap_caps_free);
}

bool loadOrCreateIdentity(const std::string& hostname, const std::string& ipv4,
                          bool forceNew, Identity& out, std::string& err) {
    Preferences prefs;
    if (!forceNew && prefs.begin(kNs, true)) {
        out.certPem = prefs.getString("cert", "").c_str();
        out.keyPem  = prefs.getString("key", "").c_str();
        String names = prefs.getString("names", "");
        prefs.end();
        // A stored certificate that names no IP address (made before the
        // network was up) is replaced once the address is known.
        std::array<uint8_t, 4> probe{};
        const bool haveIp = parseIPv4(ipv4, probe) && ipv4 != "0.0.0.0";
        bool namesIp = false;
        for (size_t i = 0, j; i < names.length(); i = j + 1) {
            j = names.indexOf(',', i);
            if (j == (size_t)-1) j = names.length();
            std::string n = names.substring(i, j).c_str();
            namesIp |= parseIPv4(n, probe) && n != "0.0.0.0";
            if (j >= names.length()) break;
        }
        if (haveIp && !namesIp) out.certPem.clear();
        if (!out.certPem.empty() && !out.keyPem.empty() && fingerprintOf(out.certPem, out.fingerprint)) {
            out.names.clear();
            std::string n = names.c_str();
            size_t i = 0;
            while (i <= n.size() && !n.empty()) {
                size_t j = n.find(',', i);
                out.names.push_back(n.substr(i, j == std::string::npos ? std::string::npos : j - i));
                if (j == std::string::npos) break;
                i = j + 1;
            }
            return true;
        }
    }
    if (!create(hostname, ipv4, out, err)) return false;
    if (!prefs.begin(kNs, false)) { err = "NVS unavailable"; return false; }
    std::string names;
    for (const auto& n : out.names) names += (names.empty() ? "" : ",") + n;
    prefs.putString("cert", out.certPem.c_str());
    prefs.putString("key", out.keyPem.c_str());
    prefs.putString("names", names.c_str());
    prefs.end();
    return true;
}

void forgetIdentity() {
    Preferences prefs;
    if (prefs.begin(kNs, false)) {
        prefs.clear();
        prefs.end();
    }
}

} // namespace tls
} // namespace mcp

#endif // MCP_TLS
