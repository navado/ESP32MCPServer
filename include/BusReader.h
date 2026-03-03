#pragma once

#include "NMEAParser.h"
#include "CANParser.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mcp {

// ---------------------------------------------------------------------------
// Abstract hardware-port interfaces (injectable for tests)
// ---------------------------------------------------------------------------

class IBusPort {
public:
    virtual ~IBusPort() = default;
    virtual bool   available()          = 0;
    virtual int    read()               = 0;      // -1 on empty
    virtual size_t readBytes(char* buf, size_t len) = 0;
    virtual void   flush()              = 0;
};

struct CANFrameRaw {           // mirrors CANFrame but without parser deps
    uint32_t id;
    bool     extended;
    bool     rtr;
    uint8_t  dlc;
    uint8_t  data[8];
};

class ICANPort {
public:
    virtual ~ICANPort() = default;
    // Returns false on timeout / no frame
    virtual bool receive(CANFrameRaw& frame, uint32_t timeoutMs) = 0;
    virtual bool send   (const CANFrameRaw& frame)               = 0;
};

// ---------------------------------------------------------------------------
// Result from a timed bus read session
// ---------------------------------------------------------------------------
enum class ParseMode { RAW, PARSED };

struct BusReadResult {
    uint32_t durationMs    = 0;
    size_t   linesReceived = 0;  // serial
    size_t   framesReceived= 0;  // CAN

    // Serial / NMEA 0183
    std::vector<std::string> rawLines;
    std::vector<ParsedNMEA>  parsedNMEA;

    // CAN / NMEA 2000 / OBD-II
    std::vector<CANFrame>     rawFrames;
    std::vector<NMEA2000Data> parsedNMEA2000;
    std::vector<OBDIIData>    parsedOBDII;

    // JSON serialisation helper (does not require ArduinoJson)
    std::string toJson(ParseMode mode) const;
};

// ---------------------------------------------------------------------------
// SerialBusReader — reads NMEA 0183 or any line-based protocol from a serial
// port for a fixed duration.
// ---------------------------------------------------------------------------
class SerialBusReader {
public:
    explicit SerialBusReader(IBusPort& port,
                             std::function<uint32_t()> clock = nullptr);

    // clock: function returning current time in milliseconds (default: millis())
    void setClock(std::function<uint32_t()> clock) { clock_ = clock; }

    BusReadResult readFor(uint32_t durationMs, ParseMode mode = ParseMode::PARSED);

private:
    IBusPort&                   port_;
    std::function<uint32_t()>   clock_;
    std::string                 lineBuffer_;

    uint32_t now() const { return clock_ ? clock_() : defaultClock(); }
    static uint32_t defaultClock();
};

// ---------------------------------------------------------------------------
// CANBusReader — reads raw CAN frames or parses NMEA 2000 / OBD-II for a
// fixed duration.
// ---------------------------------------------------------------------------
class CANBusReader {
public:
    explicit CANBusReader(ICANPort& port,
                          std::function<uint32_t()> clock = nullptr);

    void setClock(std::function<uint32_t()> clock) { clock_ = clock; }

    BusReadResult readFor(uint32_t durationMs, ParseMode mode = ParseMode::PARSED);

private:
    ICANPort&                   port_;
    std::function<uint32_t()>   clock_;

    uint32_t now() const { return clock_ ? clock_() : defaultClock(); }
    static uint32_t defaultClock();
};

} // namespace mcp
