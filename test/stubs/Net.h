#pragma once
#include <Arduino.h>
namespace Net {
enum class State : uint8_t { Idle, Connecting, Online };
void begin();
void loop();
bool isOnline();
State state();
int  rssi();
uint64_t uptimeMs();
bool timeSynced();
bool wasUnexpectedReset();
const char *resetReasonText();
void noteBotOk();
void setPendingAlerts(bool pending);
}
// test hooks
void test_setOnline(bool v);
int  test_botOkCount();
bool test_pendingAlerts();
