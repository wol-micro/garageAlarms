#pragma once
#include <Arduino.h>

// WiFi as a non-blocking state machine plus a software watchdog.
//
// The original sketch spun in `while (WiFi.status() != WL_CONNECTED)` inside setup() with no
// timeout, and never looked at WiFi again in loop(). If the router was down at power-up the
// box sat there forever, silent, with the alarm effectively disabled.

namespace Net {

enum class State : uint8_t { Idle, Connecting, Online };

void  begin();
void  loop();

bool  isOnline();
State state();
int   rssi();
uint64_t uptimeMs();

// Wall clock synced via NTP? TLS certificate validation fails without it, which surfaces as
// a confusing handshake error rather than a clock error.
bool timeSynced();

// Reset diagnostics. Reading these is the fastest way to find out why the box rebooted.
bool        wasUnexpectedReset();
const char *resetReasonText();
uint8_t     resetReasonCode();
const char *resetReasonName(uint8_t code);

// Called by the notifier whenever a Telegram request succeeds, so the software watchdog can
// tell "connected to WiFi but Telegram is unreachable" from "everything fine".
void noteBotOk();

// LED status hint from the rest of the app.
void setPendingAlerts(bool pending);

} // namespace Net
