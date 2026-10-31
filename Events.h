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
// `repeats` > 1 means several occurrences were coalesced while the link was down; the
// timestamp is the first of them.
String render(EventType type, int64_t ts, bool late, uint8_t repeats = 1);

// The mute screen. Kept here with the rest of the user-facing text, and separate from BotUI
// so it can be rendered in a test: the previous version appended "motion notifications do not
// arrive" unconditionally, so turning mute off still claimed it was on.
String muteScreen(bool muted, int64_t until);

// Short label for logs and /status.
const char *name(EventType type);

// One-line headline, used as the Pushover notification title.
const char *title(EventType type);

// Which events are duplicated to Pushover: the alarms worth waking up for, plus /test so a
// delivery check covers both channels. Boot notices and the heartbeat stay on Telegram.
bool goesToPushover(EventType type);

// Pushover priority for an event. Alarms are emergency (repeat until acknowledged); a test
// is not, or checking delivery would be its own punishment.
int pushoverPriority(EventType type);

// Alarms ignore mute; informational events do not.
bool isCritical(EventType type);

// What mute may not swallow. Alarms, obviously — but also /test: a delivery check that is
// silently dropped makes the bot look broken at the exact moment someone is trying to find
// out whether it works.
bool bypassesMute(EventType type);

} // namespace Events
