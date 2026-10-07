#pragma once
#include <Arduino.h>
#include "config.h"

// Everything here survives a reboot (NVS flash). The original sketch kept the subscriber
// list in RAM only, so any reset silently disarmed the alarm for everyone.

struct Subscriber {
    int64_t id;
    char    name[24];
};

// One queued outbound notification. Self-contained on purpose: the whole queue is
// persisted as a blob, so an alarm raised seconds before a brownout still gets delivered
// after the reboot.
struct QueueItem {
    int64_t  chatId;
    int64_t  eventTs;       // unix time of the event, not of the send attempt
    uint8_t  type;          // EventType
    uint8_t  attempts;
    uint16_t reserved;
    uint32_t nextAttemptMs; // millis-based, recomputed after a reboot
};

namespace Store {

void begin();

// --- subscribers ---
uint8_t           subCount();
const Subscriber *subs();
bool              isSubscribed(int64_t id);
bool              addSubscriber(int64_t id, const char *name);
bool              removeSubscriber(int64_t id);

// --- mute ---
int64_t muteUntil();
void    setMuteUntil(int64_t epoch);
bool    isMuted();

// Reset-reason histogram. A bare reboot counter says "it restarted 16600 times" without
// saying why; only the distribution distinguishes a brownout from a firmware hang.
#define RESET_SLOTS 16
void     noteResetReason(uint8_t reason);
uint32_t resetReasonCount(uint8_t reason);

// --- counters and stats ---
uint32_t bootCount();
uint32_t alarmCount();
void     noteAlarm(int64_t epoch);
void     noteMotion(int64_t epoch);
int64_t  lastAlarmTs();
int64_t  lastMotionTs();

// Last time we told the owner about a reboot, and the day-of-year of the last heartbeat.
// Both live in flash so a reboot loop cannot turn into a notification loop.
int64_t lastBootNotifyTs();
void    setLastBootNotifyTs(int64_t epoch);
int16_t heartbeatDay();
void    setHeartbeatDay(int16_t yday);

// Version of the command list already registered with Telegram, so we don't re-register
// (two blocking HTTP round-trips per command) on every boot.
uint32_t menuVersion();
void     setMenuVersion(uint32_t v);

// --- persisted notification queue ---
void   saveQueue(const QueueItem *items, uint8_t count);
uint8_t loadQueue(QueueItem *items, uint8_t maxItems);

} // namespace Store
