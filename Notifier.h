#pragma once
#include <Arduino.h>
#include "Events.h"

class AsyncTelegram2;

// Reliable outbound delivery.
//
// The original sketch popped a recipient off the pending list and then called sendTo()
// without looking at the result, so an alarm raised while the link was down vanished
// silently — precisely when it mattered most. Here an item leaves the queue only after
// Telegram confirms it, retries use exponential backoff, and the queue is persisted so it
// survives a brownout.

namespace Notifier {

void begin(AsyncTelegram2 *bot);
void loop();

// Queue an event for every subscriber.
void broadcast(EventType type, int64_t ts);

// Queue an event for one chat (replies to a specific user).
void enqueue(int64_t chatId, EventType type, int64_t ts);

// Queue for one chat and, if the event type warrants it, for Pushover too. Used by /test so
// a delivery check exercises every path a real alarm would take.
void enqueueBothChannels(int64_t chatId, EventType type, int64_t ts);

uint8_t  pending();
uint32_t deliveredCount();
uint32_t droppedCount();

} // namespace Notifier
