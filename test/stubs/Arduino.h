// Minimal Arduino surface for host-side tests. Time and pin levels are driven by the test,
// so debounce, cooldown and mute windows can be exercised deterministically.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <algorithm>

// ---------- controllable clock ----------
extern uint32_t g_fakeMillis;   // millis() since boot
extern int64_t  g_fakeEpoch;    // wall clock, 0 = "NTP has not run"

inline uint32_t millis() { return g_fakeMillis; }
inline void     delay(uint32_t ms) { g_fakeMillis += ms; }
inline void     yield() {}

inline time_t test_time(time_t *t) { if (t) *t = (time_t)g_fakeEpoch; return (time_t)g_fakeEpoch; }
#define time(x) test_time(x)

// ---------- controllable pins ----------
#define HIGH 1
#define LOW  0
#define INPUT_PULLUP   2
#define INPUT_PULLDOWN 3
#define OUTPUT         1
#define LED_BUILTIN    2

extern int g_pinState[64];
inline int  digitalRead(uint8_t p) { return g_pinState[p & 63]; }
inline void digitalWrite(uint8_t, int) {}
inline void pinMode(uint8_t, int) {}

// ---------- Arduino String ----------
class String {
public:
    std::string s;
    String() {}
    String(const char *c) : s(c ? c : "") {}
    String(const std::string &c) : s(c) {}
    String(int v)      { char b[24]; snprintf(b, sizeof b, "%d", v);  s = b; }
    String(unsigned v) { char b[24]; snprintf(b, sizeof b, "%u", v);  s = b; }
    String(long v)     { char b[24]; snprintf(b, sizeof b, "%ld", v); s = b; }
    String(unsigned long v) { char b[24]; snprintf(b, sizeof b, "%lu", v); s = b; }
    String(long long v){ char b[32]; snprintf(b, sizeof b, "%lld", v); s = b; }

    const char *c_str() const { return s.c_str(); }
    size_t length() const { return s.size(); }
    void reserve(size_t n) { s.reserve(n); }
    char operator[](size_t i) const { return s[i]; }

    String &operator+=(const String &o) { s += o.s; return *this; }
    String &operator+=(const char *o)   { s += (o ? o : ""); return *this; }
    String &operator+=(char c)          { s += c; return *this; }

    bool operator==(const String &o) const { return s == o.s; }
    bool operator==(const char *o) const   { return s == (o ? o : ""); }
    bool operator!=(const char *o) const   { return !(*this == o); }
    bool equals(const String &o) const { return s == o.s; }
    bool equals(const char *o) const   { return s == (o ? o : ""); }

    bool startsWith(const char *p) const { return s.rfind(p, 0) == 0; }
    int  indexOf(char c) const { auto n = s.find(c); return n == std::string::npos ? -1 : (int)n; }
    int  indexOf(const char *c) const { auto n = s.find(c); return n == std::string::npos ? -1 : (int)n; }
    String substring(size_t from) const { return from >= s.size() ? String() : String(s.substr(from)); }
    long toInt() const { return strtol(s.c_str(), nullptr, 10); }
    void trim() {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        s = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
    }
};

inline String operator+(const String &a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const char *a, const String &b)   { String r(a); r += b; return r; }
inline String operator+(const String &a, const char *b)   { String r(a); r += b; return r; }

#ifndef strlcpy
inline size_t strlcpy(char *dst, const char *src, size_t n) {
    size_t len = strlen(src);
    if (n) { size_t c = std::min(len, n - 1); memcpy(dst, src, c); dst[c] = 0; }
    return len;
}
#endif

// ---------- Serial ----------
struct FakeSerial {
    void begin(unsigned long) {}
    void println() {}
    void println(const char *s) { (void)s; }
    void print(const char *s) { (void)s; }
    void flush() {}
    template <typename... A> void printf(const char *, A...) {}
};
extern FakeSerial Serial;

struct FakeESP { uint32_t getFreeHeap() { return 200000; } void restart() {} };
extern FakeESP ESP;
