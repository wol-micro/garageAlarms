#pragma once
#include <Arduino.h>

enum class EventType : uint8_t {
    None           = 0,
    AlarmSmoke     = 1,
    Motion         = 2,
    BootCold       = 3,
    BootUnexpected = 4,
    Heartbeat      = 5,
    Test           = 6,
    AlarmOngoing   = 7,   // smoke contact still closed; eventTs is when it started
    SensorStuck    = 8,   // motion input held active far longer than any real event
};

namespace Events {

// Render the Telegram HTML body for an event. `late` marks a message that sat in the queue
// across a reboot or an outage, so the reader knows the timestamp is not "just now".
String render(EventType type, int64_t ts, bool late);

// The mute screen. Kept here with the rest of the user-facing text, and separate from BotUI
// so it can be rendered in a test: the previous version appended "motion notifications do not
// arrive" unconditionally, so turning mute off still claimed it was on.
String muteScreen(bool muted, int64_t until);

// Short label for logs and /status.
const char *name(EventType type);

// Alarms ignore mute; informational events do not.
bool isCritical(EventType type);

// What mute may not swallow. Alarms, obviously — but also /test: a delivery check that is
// silently dropped makes the bot look broken at the exact moment someone is trying to find
// out whether it works.
bool bypassesMute(EventType type);

} // namespace Events
