#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
// The two inputs have OPPOSITE polarity. Getting this wrong inverts the alarm.
#define PIN_ALARM         17          // smoke/alarm contact: INPUT_PULLUP,   active LOW
#define PIN_ALARM_ACTIVE_HIGH   false
#define PIN_MOTION        18          // motion detector:     INPUT_PULLDOWN, active HIGH
#define PIN_MOTION_ACTIVE_HIGH  true
// The generic esp32 variant does not define LED_BUILTIN; most dev boards wire it to GPIO2.
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif
#define PIN_LED           LED_BUILTIN // status indicator

// ---------------------------------------------------------------------------
// Debounce and rate limiting
// ---------------------------------------------------------------------------
// An input must hold the active level continuously for this long to count. Long cable
// runs in a garage pick up plenty of noise; a single sample is not evidence.
#define ALARM_DEBOUNCE_MS       200
#define MOTION_DEBOUNCE_MS      400

// After a trigger the same input is ignored for this long, so one real event produces one
// notification instead of a stream.
#define ALARM_COOLDOWN_MS       (2UL * 60 * 1000)    // 2 min
#define MOTION_COOLDOWN_MS      (5UL * 60 * 1000)    // 5 min

// A PIR holds its output high for the first half-minute or so after power-up while it
// settles, which is not motion. The smoke input gets no such grace period: a contact that is
// already closed at boot is a real alarm and must be reported immediately.
#define MOTION_WARMUP_MS        (60UL * 1000)

// A smoke contact that stays closed is not a broken sensor — it is an alarm that is still
// happening, so it gets a reminder rather than a fault report.
#define ALARM_REPEAT_MS         (15UL * 60 * 1000)
// A motion input held continuously for this long is almost certainly a stuck or miswired
// detector; a real person does not trip a PIR without pause for two hours.
#define MOTION_STUCK_MS         (2UL * 60 * 60 * 1000)

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
#define TZ_INFO           "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1      "time.google.com"
#define NTP_SERVER_2      "pool.ntp.org"

#define WIFI_RETRY_INTERVAL_MS  (15UL * 1000)        // between reconnect attempts
#define WIFI_DEAD_REBOOT_MS     (10UL * 60 * 1000)   // no WiFi link this long -> reboot
// Telegram itself can be down or blocked while WiFi is perfectly fine; rebooting every ten
// minutes would not help and would just churn. Give that case a much longer leash.
#define BOT_DEAD_REBOOT_MS      (30UL * 60 * 1000)
#define WDT_TIMEOUT_S           30                   // hardware watchdog on the loop task

// ---------------------------------------------------------------------------
// Notification queue
// ---------------------------------------------------------------------------
#define MAX_CLIENTS             20
#define NOTIFY_QUEUE_MAX        48
#define NOTIFY_MAX_ATTEMPTS     12
#define NOTIFY_RETRY_BASE_MS    3000                 // doubles per attempt, capped below
#define NOTIFY_RETRY_MAX_MS     (5UL * 60 * 1000)
#define NOTIFY_MIN_SEND_GAP_MS  350                  // stay under Telegram's rate limit

// ---------------------------------------------------------------------------
// Pushover (second delivery channel)
// ---------------------------------------------------------------------------
#define PUSHOVER_TIMEOUT_MS      8000
#define PUSHOVER_REPLY_IDLE_MS   800

// Priority 2 is emergency: Pushover repeats the alert until it is acknowledged and ignores
// the phone's quiet hours. Both alarm types use it by choice — drop the motion one to 1 if
// night-time movement in the garage starts waking you for nothing.
#define PUSHOVER_PRIORITY_ALARM  2
#define PUSHOVER_PRIORITY_MOTION 2
#define PUSHOVER_RETRY_SEC       60      // >= 30 required by the API
#define PUSHOVER_EXPIRE_SEC      3600    // <= 10800 required by the API
#define PUSHOVER_SOUND_ALARM     "siren"
#define PUSHOVER_SOUND_MOTION    "echo"

// ---------------------------------------------------------------------------
// Event journal (LittleFS on the reclaimed 12 MB)
// ---------------------------------------------------------------------------
#define JOURNAL_PATH         "/journal.log"
#define JOURNAL_PREV_PATH    "/journal.1.log"
#define JOURNAL_MAX_BYTES    (2UL * 1024 * 1024)   // rotate at 2 MB, keep one old file
#define JOURNAL_TAIL_BYTES   1400                  // how much /log shows

// ---------------------------------------------------------------------------
// Housekeeping
// ---------------------------------------------------------------------------
// Silence is ambiguous for an alarm system: is it quiet, or is the box dead? A daily
// heartbeat makes "no news" mean something.
#define HEARTBEAT_INTERVAL_MS   (24UL * 60 * 60 * 1000)
#define HEARTBEAT_HOUR          9                    // local hour to send it at

#define BOT_POLL_INTERVAL_MS    1000
#define MAX_MUTE_HOURS          24
