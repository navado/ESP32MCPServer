#pragma once
// Wire.h stub for native builds.
// I2C tests use mock/mock_i2c.h directly; this stub only needs to compile.

#include "Arduino.h"

class TwoWire {
public:
    bool begin()                              { return true; }
    bool begin(int /*sda*/, int /*scl*/, uint32_t /*freq*/ = 100000) { return true; }
    void beginTransmission(uint8_t /*addr*/)  {}
    uint8_t endTransmission(bool /*stop*/ = true) { return 0; }
    size_t  requestFrom(uint8_t /*addr*/, uint8_t /*n*/, bool /*stop*/ = true) { return 0; }
    size_t  write(uint8_t /*b*/)              { return 1; }
    size_t  write(const uint8_t* /*buf*/, size_t n) { return n; }
    int     available()                       { return 0; }
    int     read()                            { return 0; }
    int     peek()                            { return -1; }
    void    flush()                           {}
    void    setClock(uint32_t /*freq*/)       {}
    void    setTimeOut(uint16_t /*ms*/)       {}
};

inline TwoWire Wire;
inline TwoWire Wire1;
