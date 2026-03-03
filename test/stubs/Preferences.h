#pragma once
// Preferences.h stub for native builds.
// Provides an in-memory key/value store that mirrors the Arduino Preferences API.

#include "Arduino.h"
#include <map>
#include <string>

class Preferences {
public:
    bool begin(const char* /*ns*/, bool /*readOnly*/ = false, const char* /*nvs*/ = nullptr) {
        return true;
    }
    void end() {}
    void clear() { data_.clear(); }

    bool   putString(const char* key, const String& v) { data_[key] = v.c_str(); return true; }
    bool   putString(const char* key, const char*   v) { data_[key] = v ? v : ""; return true; }
    String getString(const char* key, const String& def = String("")) {
        auto it = data_.find(key);
        return it != data_.end() ? String(it->second.c_str()) : def;
    }

    bool   putInt   (const char* key, int32_t  v) { data_[key] = std::to_string(v); return true; }
    int32_t getInt  (const char* key, int32_t  def = 0) {
        auto it = data_.find(key);
        if (it == data_.end()) return def;
        try { return std::stoi(it->second); } catch(...) { return def; }
    }
    bool   putUInt  (const char* key, uint32_t v) { data_[key] = std::to_string(v); return true; }
    uint32_t getUInt(const char* key, uint32_t def = 0) {
        auto it = data_.find(key);
        if (it == data_.end()) return def;
        try { return (uint32_t)std::stoul(it->second); } catch(...) { return def; }
    }
    bool   putBool  (const char* key, bool v)    { data_[key] = v ? "1" : "0"; return true; }
    bool   getBool  (const char* key, bool def = false) {
        auto it = data_.find(key);
        return it != data_.end() ? (it->second != "0") : def;
    }

    bool isKey(const char* key) { return data_.find(key) != data_.end(); }
    bool remove(const char* key) { return data_.erase(key) > 0; }

private:
    std::map<std::string, std::string> data_;
};
