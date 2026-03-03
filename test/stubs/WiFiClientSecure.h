#pragma once
// WiFiClientSecure.h stub for native builds.
#include "WiFi.h"

class WiFiClientSecure {
public:
    void setCACert(const char* /*cert*/) {}
    void setInsecure() {}
    bool connect(const char* /*host*/, uint16_t /*port*/) { return false; }
    void stop() {}
    bool connected() const { return false; }
    int  available()       { return 0; }
    int  read()            { return -1; }
};
