#pragma once

#include "BusReader.h"
#include <cstdint>
#include <string>
#include <queue>

namespace mcp {

// ---------------------------------------------------------------------------
// MockSerialPort — in-memory IBusPort for unit tests.
//
// Usage:
//   MockSerialPort port;
//   port.feedLine("$GPGGA,123519,...*47");
//   port.feedLine("$GPRMC,...*30");
//
//   SerialBusReader reader(port, []{ return clock_ms; });
//   auto result = reader.readFor(100);
// ---------------------------------------------------------------------------
class MockSerialPort : public IBusPort {
public:
    // Feed a complete NMEA line (without trailing \r\n — they are added here).
    void feedLine(const std::string& line) {
        for (char c : line) buf_.push(c);
        buf_.push('\r');
        buf_.push('\n');
    }

    // Feed arbitrary raw bytes.
    void feedBytes(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) buf_.push(static_cast<char>(data[i]));
    }

    // --- IBusPort implementation ------------------------------------------

    bool available() override {
        return !buf_.empty();
    }

    int read() override {
        if (buf_.empty()) return -1;
        char c = buf_.front();
        buf_.pop();
        return static_cast<uint8_t>(c);
    }

    size_t readBytes(char* outBuf, size_t len) override {
        size_t n = 0;
        while (n < len && !buf_.empty()) {
            outBuf[n++] = buf_.front();
            buf_.pop();
        }
        return n;
    }

    void flush() override {
        while (!buf_.empty()) buf_.pop();
    }

private:
    std::queue<char> buf_;
};

} // namespace mcp
