#pragma once
#include <Arduino.h>

namespace Util {

// True once NTP has given us a plausible wall clock.
bool clockReady();

// "13.09.2026, 19:42:15" / "13.09 19:42". Falls back to "—" before the clock is synced.
String fmtTime(int64_t epoch, bool withSeconds = true);
String fmtShort(int64_t epoch);

// "3 д 04:12" from a millis() duration.
String fmtUptime(uint64_t ms);

// "12 мин назад", "вчера в 22:10", "—".
String fmtAgo(int64_t epoch);

// "2 ч 30 мин" from milliseconds, for cooldowns and mute windows.
String fmtDuration(uint64_t ms);

// Parse the argument of "/mute [N|0|off]". Returns the window in hours, where 0 means "turn
// mute off". A bare /mute yields `defaultHours`, and so does anything unparseable — a typo
// must never silence the alarm by accident.
uint32_t parseMuteHours(const String &cmd, uint32_t defaultHours, uint32_t maxHours);

// Escape &, < and > so a Telegram username can never break HTML parse_mode.
String htmlEscape(const String &s);

// RSSI in dBm -> "отличный / хороший / слабый / плохой".
const char *rssiWord(int rssi);

} // namespace Util
