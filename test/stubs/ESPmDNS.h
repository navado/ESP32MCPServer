#pragma once
// ESPmDNS.h stub for native (Linux) builds.

#include "Arduino.h"

class MDNSClass {
public:
    bool begin(const char* /*hostname*/) { return true; }
    void end() {}
    void addService(const char* /*service*/, const char* /*proto*/, uint16_t /*port*/) {}
    void addServiceTxt(const char* /*service*/, const char* /*proto*/,
                       const char* /*key*/, const char* /*value*/) {}
};

inline MDNSClass MDNS;
