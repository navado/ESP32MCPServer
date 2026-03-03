#pragma once
// WiFiUDP.h stub for native (Linux) builds.

#include "WiFi.h"
#include <stdint.h>

class WiFiUDP {
public:
    bool   begin(uint16_t /*port*/)                           { return true; }
    void   stop()                                             {}
    int    beginPacket(const char* /*host*/, uint16_t /*port*/) { return 1; }
    int    beginPacket(IPAddress /*ip*/, uint16_t /*port*/)   { return 1; }
    size_t write(const uint8_t* /*buf*/, size_t size)         { return size; }
    size_t write(uint8_t /*b*/)                               { return 1; }
    int    endPacket()                                        { return 1; }
    int    available()                                        { return 0; }
    int    read()                                             { return -1; }
    int    parsePacket()                                      { return 0; }
};
