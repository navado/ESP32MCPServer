#pragma once

#include "BusReader.h"
#include <cstdint>
#include <queue>

namespace mcp {

// ---------------------------------------------------------------------------
// MockCANPort — in-memory ICANPort for unit tests.
//
// Usage:
//   MockCANPort port;
//   CANFrameRaw f{};
//   f.id = 0x7E8; f.dlc = 8;
//   f.data[0]=4; f.data[1]=0x41; f.data[2]=0x0C; // RPM response
//   f.data[3]=0x0F; f.data[4]=0xA0;
//   port.feedFrame(f);
//
//   CANBusReader reader(port, []{ return clock_ms; });
//   auto result = reader.readFor(50);
// ---------------------------------------------------------------------------
class MockCANPort : public ICANPort {
public:
    // Pre-load a frame to be returned by receive()
    void feedFrame(const CANFrameRaw& frame) {
        frames_.push(frame);
    }

    // Helper to build and feed a standard 11-bit OBD-II response frame
    void feedOBDResponse(uint8_t pid, uint8_t A, uint8_t B = 0,
                         uint8_t C = 0, uint8_t D = 0) {
        CANFrameRaw f{};
        f.id        = 0x7E8;
        f.extended  = false;
        f.dlc       = 8;
        f.data[0]   = 4;    // additional bytes
        f.data[1]   = 0x41; // service 01 response
        f.data[2]   = pid;
        f.data[3]   = A;
        f.data[4]   = B;
        f.data[5]   = C;
        f.data[6]   = D;
        frames_.push(f);
    }

    // Helper to build a 29-bit NMEA2000 extended frame
    void feedNMEA2000Frame(uint32_t canId, const uint8_t* data, uint8_t dlc) {
        CANFrameRaw f{};
        f.id       = canId;
        f.extended = true;
        f.dlc      = dlc;
        for (int i = 0; i < dlc && i < 8; ++i) f.data[i] = data[i];
        frames_.push(f);
    }

    // --- ICANPort implementation ------------------------------------------

    bool receive(CANFrameRaw& frame, uint32_t /*timeoutMs*/) override {
        if (frames_.empty()) return false;
        frame = frames_.front();
        frames_.pop();
        return true;
    }

    bool send(const CANFrameRaw& frame) override {
        sent_.push(frame);
        return true;
    }

    // Inspect sent frames (for loopback tests)
    bool hasSent() const { return !sent_.empty(); }
    CANFrameRaw popSent() {
        CANFrameRaw f = sent_.front();
        sent_.pop();
        return f;
    }

    size_t pendingFrames() const { return frames_.size(); }

private:
    std::queue<CANFrameRaw> frames_;
    std::queue<CANFrameRaw> sent_;
};

} // namespace mcp
