#include "FeatureModel.h"
#include <algorithm>

namespace mcp {
namespace features {

const char* stateName(State s) {
    switch (s) {
        case State::Active:  return "active";
        case State::Blocked: return "blocked";
        default:             return "idle";
    }
}

std::string ownerName(int owner) {
    if (owner == NO_OWNER) return std::string();
    return "client#" + std::to_string(owner);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

void FeatureRegistry::add(Feature f) {
    for (auto& existing : features_) {
        if (existing.id == f.id) { existing = std::move(f); return; }
    }
    features_.push_back(std::move(f));
}

const Feature* FeatureRegistry::find(const std::string& id) const {
    for (const auto& f : features_) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

Status FeatureRegistry::statusOf(const Feature& f) const {
    if (!f.available || !f.status) return Status{};
    return f.status();
}

bool FeatureRegistry::conflicts(const std::string& a, const std::string& b) const {
    if (a == b) return false;
    const Feature* fa = find(a);
    const Feature* fb = find(b);
    if (fa && std::find(fa->conflictsWith.begin(), fa->conflictsWith.end(), b) !=
                  fa->conflictsWith.end())
        return true;
    if (fb && std::find(fb->conflictsWith.begin(), fb->conflictsWith.end(), a) !=
                  fb->conflictsWith.end())
        return true;
    return false;
}

std::vector<std::string> FeatureRegistry::conflictSet(const Feature& f) const {
    std::vector<std::string> out;
    for (const auto& other : features_) {
        if (conflicts(f.id, other.id)) out.push_back(other.id);
    }
    // Conflicts declared against features this build does not register are
    // still worth reporting (e.g. a future role); keep declared order.
    for (const auto& c : f.conflictsWith) {
        if (std::find(out.begin(), out.end(), c) == out.end() && c != f.id)
            out.push_back(c);
    }
    return out;
}

Decision FeatureRegistry::checkStart(const std::string& id) const {
    Decision d;
    d.feature = id;

    const Feature* f = find(id);
    if (!f) {
        d.kind   = Decision::Kind::Unavailable;
        d.reason = "unknown feature";
        return d;
    }
    if (!f->available) {
        d.kind   = Decision::Kind::Unavailable;
        d.reason = f->unavailableReason.empty() ? "not available on this build"
                                                : f->unavailableReason;
        return d;
    }

    auto blockedBy = [&](const Feature& b, const Status& st) {
        d.kind         = Decision::Kind::Busy;
        d.blockedBy    = b.id;
        d.state        = st.detail.empty() ? stateName(State::Active) : st.detail;
        d.owner        = owner(b.id);
        d.retryAfterMs = st.retryAfterMs;
        d.resolve      = b.resolve;
    };

    Status self = statusOf(*f);
    if (self.active) { blockedBy(*f, self); return d; }

    for (const auto& other : features_) {
        if (!conflicts(id, other.id)) continue;
        Status st = statusOf(other);
        if (st.active) { blockedBy(other, st); return d; }
    }
    return d;  // Ok
}

void FeatureRegistry::setOwner(const std::string& id, int clientId) {
    if (clientId == NO_OWNER) owners_.erase(id);
    else                      owners_[id] = clientId;
}

int FeatureRegistry::owner(const std::string& id) const {
    auto it = owners_.find(id);
    if (it == owners_.end()) return NO_OWNER;
    const Feature* f = find(id);
    // Ownership only means something while the feature is running.
    if (!f || !statusOf(*f).active) return NO_OWNER;
    return it->second;
}

std::vector<std::string> FeatureRegistry::releaseClient(int clientId) {
    std::vector<std::string> released;
    for (auto it = owners_.begin(); it != owners_.end();) {
        if (it->second != clientId) { ++it; continue; }
        const Feature* f = find(it->first);
        if (f && statusOf(*f).active) {
            if (f->release) f->release();
            released.push_back(f->id);
        }
        it = owners_.erase(it);
    }
    return released;
}

std::vector<FeatureView> FeatureRegistry::snapshot() const {
    // Evaluate each status callback once.
    std::vector<Status> st;
    st.reserve(features_.size());
    for (const auto& f : features_) st.push_back(statusOf(f));

    std::vector<FeatureView> out;
    out.reserve(features_.size());
    for (size_t i = 0; i < features_.size(); ++i) {
        const Feature& f = features_[i];
        FeatureView v;
        v.id                = f.id;
        v.available         = f.available;
        v.unavailableReason = f.available ? std::string() : f.unavailableReason;
        v.conflictsWith     = conflictSet(f);
        if (st[i].active) {
            v.state  = State::Active;
            v.detail = st[i].detail;
            v.owner  = owner(f.id);
        } else if (f.available) {
            for (size_t j = 0; j < features_.size(); ++j) {
                if (j != i && st[j].active && conflicts(f.id, features_[j].id)) {
                    v.state     = State::Blocked;
                    v.blockedBy = features_[j].id;
                    break;
                }
            }
        }
        out.push_back(std::move(v));
    }
    return out;
}

void FeatureRegistry::toJson(JsonObject out) const {
    JsonArray arr = out["features"].to<JsonArray>();
    for (const auto& v : snapshot()) {
        JsonObject o = arr.add<JsonObject>();
        o["id"]        = v.id;
        o["available"] = v.available;
        if (!v.available) o["reason"] = v.unavailableReason;
        o["state"] = stateName(v.state);
        if (!v.detail.empty()) o["detail"] = v.detail;
        if (v.owner != NO_OWNER) o["owner"] = ownerName(v.owner);
        if (!v.blockedBy.empty()) o["blockedBy"] = v.blockedBy;
        JsonArray c = o["conflictsWith"].to<JsonArray>();
        for (const auto& id : v.conflictsWith) c.add(id);
    }
}

// ---------------------------------------------------------------------------
// Error builders
// ---------------------------------------------------------------------------

static std::string finish(JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    return out;
}

static JsonObject errorEnvelope(JsonDocument& doc, uint32_t id, int code,
                                const char* message) {
    doc["jsonrpc"] = "2.0";
    doc["id"]      = id;
    JsonObject err = doc["error"].to<JsonObject>();
    err["code"]    = code;
    err["message"] = message;
    return err["data"].to<JsonObject>();
}

std::string busyError(uint32_t id, const Decision& d) {
    JsonDocument doc;
    JsonObject data = errorEnvelope(doc, id, ERR_FEATURE_BUSY, "Feature busy");
    data["feature"]      = d.feature;
    data["blockedBy"]    = d.blockedBy;
    data["state"]        = d.state;
    if (d.owner != NO_OWNER) data["owner"] = ownerName(d.owner);
    data["retryAfterMs"] = d.retryAfterMs;
    if (!d.resolve.empty()) data["resolve"] = d.resolve;
    return finish(doc);
}

std::string unavailableError(uint32_t id, const std::string& feature,
                             const std::string& reason) {
    JsonDocument doc;
    JsonObject data = errorEnvelope(doc, id, ERR_FEATURE_UNAVAILABLE, "Feature unavailable");
    data["feature"] = feature;
    data["reason"]  = reason;
    return finish(doc);
}

std::string notReadyError(uint32_t id, const std::string& feature,
                          const std::string& reason, const std::string& resolve) {
    JsonDocument doc;
    JsonObject data = errorEnvelope(doc, id, ERR_FEATURE_NOT_READY, "Feature not ready");
    data["feature"] = feature;
    data["reason"]  = reason;
    if (!resolve.empty()) data["resolve"] = resolve;
    return finish(doc);
}

std::string decisionError(uint32_t id, const Decision& d) {
    if (d.kind == Decision::Kind::Unavailable) return unavailableError(id, d.feature, d.reason);
    return busyError(id, d);
}

} // namespace features
} // namespace mcp
