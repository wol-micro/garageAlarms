// In-memory stand-in for the ESP32 NVS wrapper. Keeps a blob store that survives a simulated
// reboot (the test re-runs Store::begin() against the same map), which is what makes the
// persistence behaviour testable at all.
#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include <string>

struct NvsStore {
    std::map<std::string, std::vector<uint8_t>> blobs;
    std::map<std::string, int64_t>              i64;
    std::map<std::string, uint32_t>             u32;
    std::map<std::string, uint8_t>              u8;
    std::map<std::string, int16_t>              i16;
    void wipe() { blobs.clear(); i64.clear(); u32.clear(); u8.clear(); i16.clear(); }
};
extern NvsStore g_nvs;

class Preferences {
public:
    bool begin(const char *, bool = false) { return true; }
    void end() {}

    size_t putBytes(const char *k, const void *v, size_t n) {
        auto &b = g_nvs.blobs[k];
        b.assign((const uint8_t *)v, (const uint8_t *)v + n);
        return n;
    }
    size_t getBytes(const char *k, void *out, size_t n) {
        auto it = g_nvs.blobs.find(k);
        if (it == g_nvs.blobs.end()) return 0;
        size_t c = it->second.size() < n ? it->second.size() : n;
        memcpy(out, it->second.data(), c);
        return c;
    }
    bool remove(const char *k) { g_nvs.blobs.erase(k); return true; }

    size_t  putLong64(const char *k, int64_t v)  { g_nvs.i64[k] = v; return 8; }
    int64_t getLong64(const char *k, int64_t d)  { auto i = g_nvs.i64.find(k); return i == g_nvs.i64.end() ? d : i->second; }
    size_t   putUInt(const char *k, uint32_t v)  { g_nvs.u32[k] = v; return 4; }
    uint32_t getUInt(const char *k, uint32_t d)  { auto i = g_nvs.u32.find(k); return i == g_nvs.u32.end() ? d : i->second; }
    size_t  putUChar(const char *k, uint8_t v)   { g_nvs.u8[k] = v; return 1; }
    uint8_t getUChar(const char *k, uint8_t d)   { auto i = g_nvs.u8.find(k); return i == g_nvs.u8.end() ? d : i->second; }
    size_t  putShort(const char *k, int16_t v)   { g_nvs.i16[k] = v; return 2; }
    int16_t getShort(const char *k, int16_t d)   { auto i = g_nvs.i16.find(k); return i == g_nvs.i16.end() ? d : i->second; }
};
