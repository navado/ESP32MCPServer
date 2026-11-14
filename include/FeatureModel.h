#pragma once

// Feature / state model (issue #15).
//
// Several device features cannot run at the same time (BLE central vs a
// future peripheral role, BLE vs an OTA flash, ...).  This registry is the
// single place that knows:
//
//   * which features exist and whether they are AVAILABLE on this build
//     (compiled in + hardware present), with a reason when they are not;
//   * each feature's live STATE (idle / active / blocked), derived from a
//     status callback so it never goes stale;
//   * which client OWNS an active feature (released on disconnect);
//   * which features CONFLICT with each other.
//
// Request handlers ask checkStart() before starting work and turn a refusal
// into a structured JSON-RPC error (busyError() / unavailableError() /
// notReadyError()), so a client always learns WHAT is blocking and HOW to
// resolve it instead of getting a bare `started:false`.
//
// Pure logic + ArduinoJson only: unit tested natively (test_feature_model).
// Not thread-safe: on the device every call happens on the MCP task (request
// handlers, WebSocket disconnects and the change notifier all run there).

#include <ArduinoJson.h>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mcp {
namespace features {

// Stable application error codes (JSON-RPC implementation-defined range).
constexpr int ERR_FEATURE_BUSY        = -32010;  // blocked by itself or a conflicting feature
constexpr int ERR_FEATURE_UNAVAILABLE = -32011;  // compiled out / no hardware / not implemented
constexpr int ERR_RESOURCE_LIMIT      = -32012;  // memory plan or buffer caps reached
constexpr int ERR_FEATURE_NOT_READY   = -32013;  // precondition missing (e.g. not connected)

constexpr int NO_OWNER = -1;

enum class State { Idle, Active, Blocked };
const char* stateName(State s);

// Live status reported by a feature's status callback.
struct Status {
    bool        active = false;
    std::string detail;            // e.g. "scanning", "connecting", "discovering"
    uint32_t    retryAfterMs = 0;  // hint when the activity ends on its own
};

struct Feature {
    std::string              id;                 // "ble.central.scan"
    bool                     available = true;
    std::string              unavailableReason;  // when !available
    std::vector<std::string> conflictsWith;      // symmetric: declare on either side
    std::string              resolve;            // method that stops it, e.g. "ble/scan/stop"
    std::function<Status()>  status;             // null -> always idle
    std::function<void()>    release;            // stop action run when the owner disconnects
};

// Outcome of checkStart().
struct Decision {
    enum class Kind { Ok, Busy, Unavailable };
    Kind        kind = Kind::Ok;
    std::string feature;       // the feature that was asked for
    std::string blockedBy;     // feature id that blocks it (may equal `feature`)
    std::string state;         // blocker's detail, or its state name
    int         owner = NO_OWNER;
    uint32_t    retryAfterMs = 0;
    std::string resolve;       // method that clears the blocker
    std::string reason;        // for Unavailable

    bool ok() const { return kind == Kind::Ok; }
};

// One row of the status snapshot (system/features).
struct FeatureView {
    std::string              id;
    bool                     available = true;
    std::string              unavailableReason;
    State                    state = State::Idle;
    std::string              detail;
    int                      owner = NO_OWNER;
    std::vector<std::string> conflictsWith;  // symmetric closure
    std::string              blockedBy;      // when state == Blocked
};

class FeatureRegistry {
public:
    // Add or replace a feature.
    void add(Feature f);
    const Feature* find(const std::string& id) const;
    size_t size() const { return features_.size(); }

    // May `id` start now?  Unavailable beats busy; the feature itself being
    // active is reported as busy with blockedBy == id.
    Decision checkStart(const std::string& id) const;

    // Record that `clientId` started `id`.  Ownership is dropped automatically
    // once the feature reports inactive.
    void setOwner(const std::string& id, int clientId);
    int  owner(const std::string& id) const;

    // A client disconnected: run the release action of every ACTIVE feature it
    // owns and forget its ownership.  Returns the released feature ids.
    std::vector<std::string> releaseClient(int clientId);

    // Evaluate every feature (in registration order).
    std::vector<FeatureView> snapshot() const;

    // Serialise snapshot() into `out` as {"features":[...]}.
    void toJson(JsonObject out) const;

    // True if a and b conflict (declared on either side).
    bool conflicts(const std::string& a, const std::string& b) const;

private:
    Status statusOf(const Feature& f) const;
    std::vector<std::string> conflictSet(const Feature& f) const;

    std::vector<Feature>       features_;
    std::map<std::string, int> owners_;
};

// --- JSON-RPC error builders ---------------------------------------------

// {"jsonrpc":"2.0","id":id,"error":{"code":..,"message":..,"data":{..}}}
std::string busyError(uint32_t id, const Decision& d);
std::string unavailableError(uint32_t id, const std::string& feature,
                             const std::string& reason);
// `resolve` names the method that satisfies the precondition (e.g. "ble/connect").
std::string notReadyError(uint32_t id, const std::string& feature,
                          const std::string& reason, const std::string& resolve);
// Turns any non-ok Decision into the matching error.
std::string decisionError(uint32_t id, const Decision& d);

// Owner rendering used in error data and snapshots ("client#2"); empty for none.
std::string ownerName(int owner);

} // namespace features
} // namespace mcp
