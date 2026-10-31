#pragma once
#include <Arduino.h>
#include <FS.h>

// Append-only event journal.
//
// We spent a month unable to answer "why does it reboot" because only counters were kept.
// Counters tell you how often; a journal tells you what happened around it. There is room
// for it: the stock partition table left 12 MB of the onboard chip unallocated.
//
// The filesystem is held as an `fs::FS *`, so moving the journal to a soldered-on W25QXX
// later means handing `begin()` a different object — none of the logic below changes.

namespace Journal {

// Mounts the volume (formatting it on first use). Returns false if it cannot be mounted, in
// which case every other call becomes a no-op and the firmware carries on without a journal:
// losing the log must never take the alarm down with it.
bool begin(fs::FS &fs);
bool ready();

// printf-style. Each line is stamped with wall-clock time once NTP has synced, and with
// uptime before that, so entries from before the first sync are still ordered.
void log(const char *tag, const char *fmt, ...);

// Last `maxBytes` of the journal, trimmed to a line boundary.
String tail(size_t maxBytes);

size_t size();
void   clear();

} // namespace Journal
