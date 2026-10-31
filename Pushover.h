#pragma once
#include <Arduino.h>

// Second delivery channel, duplicating alarms that also go to Telegram.
//
// The reason it exists: a Telegram message arrives with whatever sound the recipient's
// client happens to be set to, which in practice meant silence. Pushover has server-side
// emergency priority — it repeats until the message is acknowledged and overrides the
// phone's quiet hours — which is what an alarm actually needs.

namespace Pushover {

// True once both credentials are present in secrets.h.
bool configured();

// Blocking send. Returns true only when Pushover answers "status":1 — handing bytes to the
// socket is not delivery, the same lesson the Telegram path already learned.
//
// `priority` 2 is emergency and requires `retrySec` >= 30 and `expireSec` <= 10800.
bool send(const String &title, const String &message, int priority,
          const char *sound, uint32_t retrySec, uint32_t expireSec);

} // namespace Pushover
