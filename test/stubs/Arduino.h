#pragma once
// Arduino.h stub for native (Linux) builds.
// Provides String, HardwareSerial, time functions, and Arduino constants
// that are used by MetricsSystem, uLogger, and NetworkManager.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <cstdio>
#include <ctime>
#include <string>
#include <algorithm>
#include <cctype>

typedef unsigned char byte;
typedef bool boolean;

// ─── String ──────────────────────────────────────────────────────────────────
class String {
public:
    String() {}
    String(const char* s)          : s_(s ? s : "") {}
    String(const std::string& s)   : s_(s) {}
    String(std::string&& s)        : s_(std::move(s)) {}
    String(char c)                 : s_(1, c) {}
    explicit String(int v, int base = 10)          { char b[32]; snprintf(b, sizeof(b), base == 16 ? "%x" : "%d", v);         s_ = b; }
    explicit String(unsigned int v, int base = 10) { char b[32]; snprintf(b, sizeof(b), base == 16 ? "%x" : "%u", v);         s_ = b; }
    explicit String(long v)                        { char b[32]; snprintf(b, sizeof(b), "%ld", v);   s_ = b; }
    explicit String(unsigned long v)               { char b[32]; snprintf(b, sizeof(b), "%lu", v);   s_ = b; }
    explicit String(float v,  int d = 2)           { char b[32]; snprintf(b, sizeof(b), "%.*f", d, (double)v); s_ = b; }
    explicit String(double v, int d = 2)           { char b[32]; snprintf(b, sizeof(b), "%.*f", d, v);         s_ = b; }

    const char* c_str()  const { return s_.c_str(); }
    size_t      length() const { return s_.length(); }
    size_t      size()   const { return s_.size(); }
    bool        isEmpty()const { return s_.empty(); }
    bool        empty()  const { return s_.empty(); }

    bool operator==(const String& o)  const { return s_ == o.s_; }
    bool operator==(const char*   o)  const { return o && s_ == o; }
    bool operator!=(const String& o)  const { return s_ != o.s_; }
    bool operator!=(const char*   o)  const { return !o || s_ != o; }
    bool operator< (const String& o)  const { return s_ <  o.s_; }
    bool operator> (const String& o)  const { return s_ >  o.s_; }
    bool operator<=(const String& o)  const { return s_ <= o.s_; }
    bool operator>=(const String& o)  const { return s_ >= o.s_; }

    String& operator=(const String& o)  { s_ = o.s_;  return *this; }
    String& operator=(const char*   o)  { s_ = o ? o : ""; return *this; }
    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(const char*   o) { if (o) s_ += o; return *this; }
    String& operator+=(char c)          { s_ += c;    return *this; }
    String& operator+=(int v)           { s_ += std::to_string(v); return *this; }

    String operator+(const String& o) const { return String(s_ + o.s_); }
    String operator+(const char*   o) const { return String(o ? s_ + o : s_); }
    String operator+(char c)          const { return String(s_ + c); }

    char operator[](int i) const { return (i >= 0 && (size_t)i < s_.size()) ? s_[i] : '\0'; }
    char charAt(int i)     const { return operator[](i); }

    int indexOf(char c, int from = 0) const {
        size_t p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const char* s, int from = 0) const {
        if (!s) return -1;
        size_t p = s_.find(s, from); return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const String& s, int from = 0) const { return indexOf(s.s_.c_str(), from); }
    int lastIndexOf(char c) const {
        size_t p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p;
    }

    String substring(unsigned int from, unsigned int to = (unsigned int)-1) const {
        if (from > s_.size()) return String();
        if (to == (unsigned int)-1) return String(s_.substr(from));
        return String(s_.substr(from, to - from));
    }

    bool startsWith(const String& s) const { return s_.rfind(s.s_, 0) == 0; }
    bool startsWith(const char*   s) const { return s && s_.rfind(s, 0) == 0; }
    bool endsWith  (const String& s) const {
        if (s.s_.size() > s_.size()) return false;
        return s_.compare(s_.size() - s.s_.size(), s.s_.size(), s.s_) == 0;
    }

    int    toInt()    const { try { return std::stoi(s_); }  catch(...) { return 0; } }
    float  toFloat()  const { try { return std::stof(s_); }  catch(...) { return 0.0f; } }
    double toDouble() const { try { return std::stod(s_); }  catch(...) { return 0.0; } }

    void trim() {
        size_t s = s_.find_first_not_of(" \t\r\n");
        if (s == std::string::npos) { s_ = ""; return; }
        s_ = s_.substr(s, s_.find_last_not_of(" \t\r\n") - s + 1);
    }
    void toLowerCase() { for (auto& c : s_) c = (char)tolower((unsigned char)c); }
    void toUpperCase() { for (auto& c : s_) c = (char)toupper((unsigned char)c); }
    void replace(char f, char r)          { for (auto& c : s_) if (c == f) c = r; }
    void replace(const String& f, const String& r) {
        size_t p = 0;
        while ((p = s_.find(f.s_, p)) != std::string::npos) {
            s_.replace(p, f.s_.size(), r.s_); p += r.s_.size();
        }
    }

    bool contains(const String& s) const { return s_.find(s.s_) != std::string::npos; }
    bool contains(const char*   s) const { return s && s_.find(s) != std::string::npos; }

    int  compareTo(const String& o) const { return s_.compare(o.s_); }
    bool concat(const String& s)          { s_ += s.s_; return true; }
    bool concat(const char*   s)          { if (s) s_ += s; return true; }

    // write() methods required by ArduinoJson's Writer<String> template
    size_t write(const uint8_t* buf, size_t size) {
        s_.append(reinterpret_cast<const char*>(buf), size);
        return size;
    }
    size_t write(uint8_t c) { s_ += static_cast<char>(c); return 1; }

    // Implicit conversion for std::map and std::string contexts
    operator const std::string&() const { return s_; }
    operator std::string()        const { return s_; }

private:
    std::string s_;
};

inline String operator+(const char* a, const String& b) { return String(std::string(a ? a : "") + b.c_str()); }
inline String operator+(char a,        const String& b) { return String(std::string(1, a) + b.c_str()); }
inline bool   operator==(const char* a, const String& b) { return b == a; }
inline bool   operator!=(const char* a, const String& b) { return b != a; }

// ─── HardwareSerial ───────────────────────────────────────────────────────────
class HardwareSerial {
public:
    void begin(long /*baud*/) {}
    template<typename T> void print(T v)   { std::string s = toStr(v); printf("%s", s.c_str()); }
    template<typename T> void println(T v) { std::string s = toStr(v); printf("%s\n", s.c_str()); }
    void println()                         { printf("\n"); }
    template<typename... A> void printf(const char* fmt, A... args) {
        if constexpr (sizeof...(A) == 0) ::printf("%s", fmt);
        else                             ::printf(fmt, args...);
    }
    bool available() const { return false; }
    int  read()            { return -1; }
    operator bool() const  { return true; }
private:
    template<typename T> static std::string toStr(T v) { return std::to_string(v); }
    static std::string toStr(const char* v)  { return v ? v : ""; }
    static std::string toStr(const String& v){ return v.c_str(); }
    static std::string toStr(const std::string& v) { return v; }
};

inline HardwareSerial Serial;
inline HardwareSerial Serial1;
inline HardwareSerial Serial2;

// ─── Time ─────────────────────────────────────────────────────────────────────
inline unsigned long millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}
inline unsigned long micros() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000000UL + ts.tv_nsec / 1000UL);
}
inline void delay(unsigned long ms) {
    struct timespec ts = { (time_t)(ms / 1000), (long)((ms % 1000) * 1000000L) };
    nanosleep(&ts, nullptr);
}
inline void delayMicroseconds(unsigned int us) {
    struct timespec ts = { 0, (long)((long)us * 1000L) };
    nanosleep(&ts, nullptr);
}

// ─── Math / GPIO stubs ────────────────────────────────────────────────────────
#ifndef min
template<typename T> inline T min(T a, T b) { return a < b ? a : b; }
#endif
#ifndef max
template<typename T> inline T max(T a, T b) { return a > b ? a : b; }
#endif
template<typename T> inline T constrain(T x, T lo, T hi) { return x < lo ? lo : x > hi ? hi : x; }

#define LOW  0
#define HIGH 1
#define INPUT       0
#define OUTPUT      1
#define INPUT_PULLUP 2

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int  digitalRead(int)    { return 0; }
inline int  analogRead(int)     { return 0; }
inline void analogWrite(int, int) {}

// ─── PROGMEM (no-op on host) ──────────────────────────────────────────────────
#define PROGMEM
#define F(s)               (s)
#define pgm_read_byte(a)   (*(const uint8_t*)(a))
#define pgm_read_word(a)   (*(const uint16_t*)(a))
#define pgm_read_dword(a)  (*(const uint32_t*)(a))
#define PSTR(s)            (s)

// ─── ESP-IDF logging macros ───────────────────────────────────────────────────
#ifndef log_e
#define log_e(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef log_w
#define log_w(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef log_i
#define log_i(fmt, ...) fprintf(stdout, "[I] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef log_d
#define log_d(fmt, ...) fprintf(stdout, "[D] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef log_v
#define log_v(fmt, ...)
#endif

// ─── ESP class stub ───────────────────────────────────────────────────────────
class EspClass {
public:
    uint32_t getFreeHeap()     const { return 200000; }
    uint32_t getMinFreeHeap()  const { return 180000; }
    uint32_t getMaxAllocHeap() const { return 180000; }
    uint32_t getHeapSize()     const { return 512000; }
    void     restart()               {}
};
inline EspClass ESP;

// ─── FreeRTOS stubs ───────────────────────────────────────────────────────────
typedef int      BaseType_t;
typedef uint32_t TickType_t;
typedef void*    TaskHandle_t;
typedef void*    QueueHandle_t;

#define portMAX_DELAY     ((TickType_t)0xffffffffUL)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE            ((BaseType_t)1)
#define pdFALSE           ((BaseType_t)0)

inline TickType_t xTaskGetTickCount() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (TickType_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

inline void vTaskDelay(TickType_t /*ticks*/) {}

inline BaseType_t xTaskCreatePinnedToCore(
    void        (*pvTaskCode)(void*),
    const char* /*pcName*/,
    uint32_t    /*usStackDepth*/,
    void*       pvParameters,
    unsigned    /*uxPriority*/,
    TaskHandle_t* pvCreatedTask,
    int         /*xCoreID*/)
{
    (void)pvTaskCode; (void)pvParameters;
    if (pvCreatedTask) *pvCreatedTask = nullptr;
    return pdTRUE;
}
