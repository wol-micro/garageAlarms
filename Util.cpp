#include "Util.h"
#include <time.h>

namespace Util {

bool clockReady()
{
    return time(nullptr) > 1700000000;  // ~Nov 2023; anything below is an unsynced clock
}

String fmtTime(int64_t epoch, bool withSeconds)
{
    if (epoch < 1700000000)
        return String("—");
    time_t t = (time_t)epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[32];
    strftime(buf, sizeof(buf), withSeconds ? "%d.%m.%Y, %H:%M:%S" : "%d.%m.%Y, %H:%M", &tmv);
    return String(buf);
}

String fmtShort(int64_t epoch)
{
    if (epoch < 1700000000)
        return String("—");
    time_t t = (time_t)epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[24];
    strftime(buf, sizeof(buf), "%d.%m %H:%M", &tmv);
    return String(buf);
}

String fmtUptime(uint64_t ms)
{
    uint64_t sec  = ms / 1000;
    uint32_t days = sec / 86400;
    uint32_t hrs  = (sec % 86400) / 3600;
    uint32_t mins = (sec % 3600) / 60;
    char buf[32];
    if (days)
        snprintf(buf, sizeof(buf), "%u д %02u:%02u", days, hrs, mins);
    else
        snprintf(buf, sizeof(buf), "%02u:%02u", hrs, mins);
    return String(buf);
}

String fmtDuration(uint64_t ms)
{
    uint64_t sec = ms / 1000;
    if (sec < 60)
        return String((uint32_t)sec) + " с";
    uint32_t mins = sec / 60;
    if (mins < 60)
        return String(mins) + " мин";
    uint32_t hrs = mins / 60;
    mins %= 60;
    if (mins == 0)
        return String(hrs) + " ч";
    return String(hrs) + " ч " + String(mins) + " мин";
}

String fmtAgo(int64_t epoch)
{
    if (epoch < 1700000000 || !clockReady())
        return String("—");
    int64_t now  = (int64_t)time(nullptr);
    int64_t diff = now - epoch;
    if (diff < 0)
        return fmtShort(epoch);
    if (diff < 60)
        return String("только что");
    if (diff < 3600)
        return String((uint32_t)(diff / 60)) + " мин назад";
    if (diff < 86400)
        return String((uint32_t)(diff / 3600)) + " ч назад";
    return fmtShort(epoch);
}

uint32_t parseMuteHours(const String &cmd, uint32_t defaultHours, uint32_t maxHours)
{
    const int sp = cmd.indexOf(' ');
    if (sp <= 0)
        return defaultHours;              // bare /mute

    String arg = cmd.substring(sp + 1);
    arg.trim();
    if (arg == "0" || arg == "off" || arg == "выкл")
        return 0;                         // an explicit request to turn it off

    const long h = arg.toInt();
    if (h <= 0)
        return defaultHours;
    return (uint32_t)h > maxHours ? maxHours : (uint32_t)h;
}

String htmlEscape(const String &s)
{
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '&')      out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else               out += c;
    }
    return out;
}

const char *rssiWord(int rssi)
{
    if (rssi >= -55) return "отличный";
    if (rssi >= -67) return "хороший";
    if (rssi >= -78) return "слабый";
    return "плохой";
}

} // namespace Util
