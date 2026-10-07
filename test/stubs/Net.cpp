#include "Net.h"
static bool s_online = true;
static int  s_botOk = 0;
static bool s_pending = false;

namespace Net {
void begin() {}
void loop() {}
bool isOnline() { return s_online; }
State state() { return s_online ? State::Online : State::Idle; }
int  rssi() { return -60; }
uint64_t uptimeMs() { return millis(); }
bool timeSynced() { return true; }
bool wasUnexpectedReset() { return false; }
const char *resetReasonText() { return "test"; }
void noteBotOk() { s_botOk++; }
void setPendingAlerts(bool p) { s_pending = p; }
}
void test_setOnline(bool v) { s_online = v; }
int  test_botOkCount() { return s_botOk; }
bool test_pendingAlerts() { return s_pending; }
