// Host-side tests for the logic that decides whether an alarm reaches a person.
// Hardware is replaced by stubs; the clock and the pins are driven by the test.
#include <Arduino.h>
#include <Preferences.h>
#include <cstdio>
#include "Store.h"
#include "Sensors.h"
#include "Notifier.h"
#include "Events.h"
#include "Net.h"
#include "Util.h"
#include "Pushover.h"
#include <vector>
#include <string>
#include "src/AsyncTelegram2/AsyncTelegram2.h"

uint32_t   g_fakeMillis = 0;
int64_t    g_fakeEpoch  = 0;
int        g_pinState[64];
NvsStore   g_nvs;
FakeSerial Serial;
FakeESP    ESP;

extern std::vector<std::string> g_pushSent;
extern bool g_pushConfigured;
extern bool g_pushAccepts;
extern int  g_pushLastPriority;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (cond) { g_pass++; }                                                  \
        else { g_fail++; printf("  FAIL  %s  (%s:%d)\n", what, __FILE__, __LINE__); } \
    } while (0)

static void section(const char *n) { printf("\n== %s ==\n", n); }

// Epoch well past the "clock is synced" threshold used throughout the firmware.
static const int64_t T0 = 1760000000;

static void advance(uint32_t ms) { g_fakeMillis += ms; g_fakeEpoch = T0 + g_fakeMillis / 1000; }

// Mirrors the firmware: loop() polls update() every couple of milliseconds. Testing in one
// big jump would hide the fact that the debouncer needs repeated samples.
static bool run(DebouncedInput &in, uint32_t ms)
{
    bool fired = false;
    for (uint32_t t = 0; t < ms; t += 2) { advance(2); if (in.update()) fired = true; }
    return fired;
}

static void resetWorld(bool wipeFlash)
{
    if (wipeFlash) g_nvs.wipe();
    g_pushSent.clear();
    g_pushConfigured = true;
    g_pushAccepts    = true;
    g_pushLastPriority = -99;
    g_fakeMillis = 0;
    g_fakeEpoch  = T0;
    for (int i = 0; i < 64; i++) g_pinState[i] = LOW;
    test_setOnline(true);
}

// ---------------------------------------------------------------- debounce --
static void testDebounce()
{
    section("DebouncedInput");

    // Active-high input, 400 ms debounce, 5 min cooldown, no warm-up.
    resetWorld(true);
    DebouncedInput in;
    in.begin(18, true, 400, 300000, 0);

    // A spike shorter than the debounce window is noise, not an event.
    g_pinState[18] = HIGH;
    CHECK(!run(in, 200), "glitch: 200ms spike does not trigger");
    g_pinState[18] = LOW;
    CHECK(!run(in, 2000), "glitch: nothing after it clears");

    // A level held past the debounce window triggers exactly once.
    g_pinState[18] = HIGH;
    CHECK(!run(in, 300), "sustained: not yet at 300ms");
    CHECK(run(in, 300), "sustained: fires once past 400ms");
    CHECK(!run(in, 5000), "sustained: does not fire again while held");

    // Cooldown swallows a second event, then lets one through.
    g_pinState[18] = LOW;  run(in, 1000);
    g_pinState[18] = HIGH;
    CHECK(!run(in, 1000), "cooldown: second event suppressed");
    g_pinState[18] = LOW;  run(in, 1000);
    run(in, 300000);                       // let the cooldown expire
    g_pinState[18] = HIGH;
    CHECK(run(in, 1000), "cooldown: fires again once elapsed");
}

static void testWarmup()
{
    section("DebouncedInput warm-up");

    resetWorld(true);
    DebouncedInput pir;
    pir.begin(18, true, 400, 300000, 60000);   // 60 s warm-up, 5 min cooldown

    // A PIR holds its output high while it settles: that must not be an event...
    g_pinState[18] = HIGH;
    CHECK(!run(pir, 2000), "warmup: edge during warm-up is swallowed");

    // ...and it must not arm the cooldown, or the first real event afterwards is lost.
    g_pinState[18] = LOW;
    run(pir, 60000);                       // ride out the warm-up window
    g_pinState[18] = HIGH;
    CHECK(run(pir, 1000), "warmup: first real event after warm-up is reported");
}

static void testAlarmActiveAtBoot()
{
    section("Alarm already active at boot");

    resetWorld(true);
    DebouncedInput alarm;
    g_pinState[17] = LOW;                 // active-low contact already closed
    alarm.begin(17, false, 200, 120000, 0);
    CHECK(run(alarm, 1000), "boot: a contact closed at power-up still alerts");
}

// ------------------------------------------------------------------- mute --
static void testMute()
{
    section("Mute window");

    resetWorld(true);
    Store::begin();
    CHECK(!Store::isMuted(), "fresh device is not muted");

    Store::setMuteUntil(g_fakeEpoch + 3600);
    CHECK(Store::isMuted(), "mute set for an hour is active");

    Store::setMuteUntil(0);
    CHECK(!Store::isMuted(), "/unmute clears it");

    Store::setMuteUntil(g_fakeEpoch + 3600);
    advance(3601 * 1000);
    CHECK(!Store::isMuted(), "mute expires on its own");

    // An unsynced clock must fail open: a silent box is worse than a noisy one.
    Store::setMuteUntil(T0 + 999999);
    g_fakeEpoch = 100;
    CHECK(!Store::isMuted(), "unsynced clock fails open");
    g_fakeEpoch = T0;

    // Mute must survive a power cut.
    Store::setMuteUntil(g_fakeEpoch + 7200);
    Store::begin();                        // simulate reboot against the same flash
    CHECK(Store::isMuted(), "mute survives a reboot");
    Store::setMuteUntil(0);
}

// -------------------------------------------------------------- notifier --
static void testNotifierMuteGating()
{
    section("Notifier: what mute may silence");

    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot;
    Notifier::begin(&bot);
    g_pushConfigured = false;   // this test is about the queue, not the fan-out
    Store::setMuteUntil(g_fakeEpoch + 3600);

    Notifier::broadcast(EventType::Motion, g_fakeEpoch);
    CHECK(Notifier::pending() == 0, "muted: motion is dropped");

    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    CHECK(Notifier::pending() == 1, "muted: a smoke alarm is NOT dropped");

    Notifier::enqueue(1001, EventType::Test, g_fakeEpoch);
    CHECK(Notifier::pending() == 2, "muted: /test still reaches the user");

    Store::setMuteUntil(0);
}

static void testNotifierDelivery()
{
    section("Notifier: delivery, retry, persistence");

    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot;
    Notifier::begin(&bot);
    g_pushConfigured = false;   // this test is about the queue, not the fan-out

    // Offline: the alert must be kept, not sent and not lost.
    test_setOnline(false);
    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    for (int i = 0; i < 20; i++) { advance(1000); Notifier::loop(); }
    CHECK(bot.delivered.empty(), "offline: nothing is transmitted");
    CHECK(Notifier::pending() == 1, "offline: the alert stays queued");

    // Back online: delivered exactly once.
    test_setOnline(true);
    advance(1000); Notifier::loop();
    CHECK(bot.delivered.size() == 1, "online: delivered exactly once");
    CHECK(Notifier::pending() == 0, "online: queue drains");

    // A failing send must retry, and must not deliver twice.
    bot.transportOk = false;
    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    for (int i = 0; i < 5; i++) { advance(5000); Notifier::loop(); }
    CHECK(bot.delivered.size() == 1, "failing send: nothing extra delivered");
    CHECK(Notifier::pending() == 1, "failing send: still queued for retry");

    bot.transportOk = true;
    advance(400000); Notifier::loop();
    CHECK(bot.delivered.size() == 2, "recovered: delivered once, not once per attempt");
    CHECK(Notifier::pending() == 0, "recovered: queue drains");
}

static void testQueueSurvivesReboot()
{
    section("Notifier: queue survives a power cut");

    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot;
    Notifier::begin(&bot);
    g_pushConfigured = false;   // this test is about the queue, not the fan-out

    test_setOnline(false);
    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    CHECK(Notifier::pending() == 1, "queued while offline");

    // Power cut: same flash, fresh RAM.
    g_fakeMillis = 0;
    Store::begin();
    AsyncTelegram2 bot2;
    Notifier::begin(&bot2);
    CHECK(Notifier::pending() == 1, "alert restored from flash after reboot");

    test_setOnline(true);
    advance(1000); Notifier::loop();
    CHECK(bot2.delivered.size() == 1, "restored alert is delivered");
}


// The regression that silently lost alarms: the transport accepted the bytes, Telegram never
// confirmed, and the queue dropped the item anyway.
static void testUnconfirmedSendIsNotDelivery()
{
    section("Notifier: an unconfirmed send is not a delivery");

    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot;
    Notifier::begin(&bot);
    g_pushConfigured = false;   // this test is about the queue, not the fan-out

    bot.transportOk = true;
    bot.confirmOk   = false;          // weak link: written, never acknowledged

    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    for (int i = 0; i < 4; i++) { advance(5000); Notifier::loop(); }

    CHECK(bot.attempted.size() >= 1, "it did try to send");
    CHECK(bot.delivered.empty(),     "nothing actually reached Telegram");
    CHECK(Notifier::pending() == 1,  "an unconfirmed alarm stays queued, never dropped");

    bot.confirmOk = true;
    advance(400000); Notifier::loop();
    CHECK(bot.delivered.size() == 1, "once confirmed, delivered exactly once");
    CHECK(Notifier::pending() == 0,  "and only then does the queue drain");
}

// Sending while a getUpdates reply is outstanding would read that reply as our own answer.
static void testNoSendWhileReplyOutstanding()
{
    section("Notifier: never sends into an outstanding reply");

    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot;
    Notifier::begin(&bot);
    g_pushConfigured = false;   // this test is about the queue, not the fan-out

    bot.waitingReply = true;
    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    for (int i = 0; i < 3; i++) { advance(5000); Notifier::loop(); }
    CHECK(bot.attempted.empty(), "holds off while a reply is in flight");

    bot.waitingReply = false;
    advance(5000); Notifier::loop();
    CHECK(bot.delivered.size() == 1, "sends once the connection is free");
}

// The screen that made the user press /unmute four times.
static void testMuteScreenText()
{
    section("Mute screen wording");

    String off = Events::muteScreen(false, 0);
    CHECK(off.indexOf("уведомления включены") > -1, "unmuted: says notifications are on");
    CHECK(off.indexOf("не приходят") == -1,
          "unmuted: must NOT still claim motion is suppressed");

    String on = Events::muteScreen(true, T0 + 3600);
    CHECK(on.indexOf("тишина до") > -1,   "muted: says until when");
    CHECK(on.indexOf("не приходят") > -1, "muted: explains what is suppressed");
}



// An outage longer than the sensor cooldown used to accumulate one queue entry per period.
static void testCoalescing()
{
    section("Coalescing while the link is down");

    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot;
    Notifier::begin(&bot);
    g_pushConfigured = false;

    test_setOnline(false);
    const int64_t first = g_fakeEpoch;
    Notifier::broadcast(EventType::Motion, first);
    for (int i = 0; i < 4; i++) {
        advance(600000);                    // a cooldown period apart
        Notifier::broadcast(EventType::Motion, g_fakeEpoch);
    }
    CHECK(Notifier::pending() == 1, "five occurrences collapse into one queue entry");

    test_setOnline(true);
    advance(1000); Notifier::loop();
    CHECK(bot.delivered.size() == 1, "one message is sent, not five");

    const std::string &body = bot.delivered[0].body;
    CHECK(body.find("Срабатываний: 5") != std::string::npos,
          "the message reports how many were folded in");

    // Different event types must not be folded together.
    resetWorld(false);
    g_pushConfigured = false;          // resetWorld re-enables it; keep this check single-subject
    test_setOnline(false);
    Notifier::broadcast(EventType::Motion, g_fakeEpoch);
    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    CHECK(Notifier::pending() == 2, "motion and smoke stay separate entries");

    // Coalescing must not hand an entry an endless retry budget.
    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot2;
    Notifier::begin(&bot2);
    g_pushConfigured = false;
    bot2.transportOk = false;
    Notifier::broadcast(EventType::Motion, g_fakeEpoch);
    for (int i = 0; i < 60; i++) {
        advance(60000);
        Notifier::broadcast(EventType::Motion, g_fakeEpoch);   // keeps folding in
        Notifier::loop();
    }
    // A fresh entry is created after each give-up, so the queue is not expected to be empty;
    // what matters is that folding never bought an entry an unlimited retry budget.
    CHECK(Notifier::droppedCount() >= 1, "it still gives up after the retry budget");
}

// ------------------------------------------------------------- pushover --
static void testPushoverDuplicatesAlarms()
{
    section("Pushover: duplicates alarms, nothing else");

    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot;
    Notifier::begin(&bot);

    // An alarm is queued for the Telegram subscriber *and* once for Pushover.
    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    CHECK(Notifier::pending() == 2, "smoke: queued for telegram and pushover");

    for (int i = 0; i < 6; i++) { advance(1000); Notifier::loop(); }
    CHECK(bot.delivered.size() == 1,  "smoke: telegram got it once");
    CHECK(g_pushSent.size() == 1,     "smoke: pushover got it once");
    CHECK(g_pushLastPriority == PUSHOVER_PRIORITY_ALARM, "smoke: sent at alarm priority");

    // Motion is duplicated too.
    resetWorld(false);
    Notifier::broadcast(EventType::Motion, g_fakeEpoch);
    CHECK(Notifier::pending() == 2, "motion: queued for both channels");
    for (int i = 0; i < 6; i++) { advance(1000); Notifier::loop(); }
    CHECK(g_pushLastPriority == PUSHOVER_PRIORITY_MOTION, "motion: sent at motion priority");

    // Housekeeping events stay on Telegram.
    resetWorld(false);
    Notifier::broadcast(EventType::Heartbeat, g_fakeEpoch);
    CHECK(Notifier::pending() == 1, "heartbeat: telegram only, no pushover copy");

    // Drain it: the next check counts queue entries, so a leftover would be read as a
    // second channel that is not there.
    for (int i = 0; i < 6 && Notifier::pending(); i++) { advance(1000); Notifier::loop(); }
    CHECK(Notifier::pending() == 0, "heartbeat drained before the next check");

    // /test must exercise both channels, or it is not a delivery check — but at normal
    // priority, so checking delivery does not nag until acknowledged.
    resetWorld(false);
    Notifier::enqueueBothChannels(1001, EventType::Test, g_fakeEpoch);
    CHECK(Notifier::pending() == 2, "/test: queued for both channels");
    for (int i = 0; i < 6; i++) { advance(1000); Notifier::loop(); }
    CHECK(g_pushSent.size() == 1,  "/test: pushover got it");
    CHECK(g_pushLastPriority == 0, "/test: sent at normal priority, not emergency");
}

static void testPushoverFailureDoesNotLoseTelegram()
{
    section("Pushover: one channel failing never costs the other");

    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot;
    Notifier::begin(&bot);

    g_pushAccepts = false;                 // Pushover rejects everything
    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    for (int i = 0; i < 6; i++) { advance(2000); Notifier::loop(); }

    CHECK(bot.delivered.size() == 1, "telegram still delivered");
    CHECK(g_pushSent.empty(),        "pushover delivered nothing");
    CHECK(Notifier::pending() == 1,  "the pushover copy stays queued for retry");

    g_pushAccepts = true;
    advance(400000); Notifier::loop();
    CHECK(g_pushSent.size() == 1,   "pushover delivered once it recovers");
    CHECK(Notifier::pending() == 0, "queue drains");
}

static void testPushoverUnconfigured()
{
    section("Pushover: absent credentials change nothing");

    resetWorld(true);
    Store::begin();
    AsyncTelegram2 bot;
    Notifier::begin(&bot);

    g_pushConfigured = false;
    Notifier::broadcast(EventType::AlarmSmoke, g_fakeEpoch);
    CHECK(Notifier::pending() == 1, "no pushover copy is queued");
    for (int i = 0; i < 4; i++) { advance(1000); Notifier::loop(); }
    CHECK(bot.delivered.size() == 1, "telegram unaffected");
}

// ------------------------------------------------- /mute command parsing --
static void testMuteParsing()
{
    section("/mute argument parsing");
    CHECK(Util::parseMuteHours("/mute", 4, 24) == 4,      "bare /mute uses the default window");
    CHECK(Util::parseMuteHours("/mute 8", 4, 24) == 8,    "/mute 8 is eight hours");
    CHECK(Util::parseMuteHours("/mute 100", 4, 24) == 24, "/mute 100 is capped");
    CHECK(Util::parseMuteHours("/mute 0", 4, 24) == 0,    "/mute 0 turns mute OFF");
    CHECK(Util::parseMuteHours("/mute off", 4, 24) == 0,  "/mute off turns mute OFF");
    CHECK(Util::parseMuteHours("/mute abc", 4, 24) == 4,  "a typo falls back, never silences");
    CHECK(!String("/unmute").startsWith("/mute"),         "/unmute is not swallowed by /mute");
}


// A stalled loop must not be mistaken for a steady contact. loop() blocks for seconds
// inside a Telegram or Pushover send, and the debouncer only ever sees the samples either
// side of that gap.
static void testDebounceAcrossStall()
{
    section("Debounce across a blocked loop");

    resetWorld(true);
    DebouncedInput in;
    in.begin(18, true, 400, 300000, 0);

    g_pinState[18] = HIGH;
    advance(10);
    CHECK(!in.update(), "stall: the edge itself is not an event");

    // Five seconds inside a send. Whatever the line did meanwhile was never sampled, so it
    // is not evidence: the old code credited the whole gap to the debounce window and fired.
    advance(5000);
    CHECK(!in.update(), "stall: an unobserved gap does not satisfy the debounce window");

    // The guard only withholds unobserved time, so a real contact still gets through as
    // soon as it has actually been watched for the window. Nothing is lost, only delayed.
    CHECK(run(in, 500), "stall: fires once the level has really been observed");
}

// One physical episode, one message — unless the box restarts in the middle of it.
static void testRebootDuringHeldContact()
{
    section("Reboot while the contact is held");

    resetWorld(true);
    DebouncedInput alarm;
    g_pinState[17] = LOW;                      // smoke contact closed and latched
    alarm.begin(17, false, 200, 120000, 0);
    CHECK(run(alarm, 1000),   "held contact alerts once");
    CHECK(!run(alarm, 60000), "and stays quiet for the cooldown");

    // A brownout wipes the debouncer, and the contact is still closed. We alert again on
    // purpose: a box that has just booted cannot know whether the smoke in front of it was
    // already reported, and for an alarm a repeat beats a silence. The consequence is that
    // an unstable supply turns one episode into one message per restart — which is a power
    // problem, not a notification one, and the reset-reason histogram is what names it.
    DebouncedInput afterReboot;
    afterReboot.begin(17, false, 200, 120000, 0);
    CHECK(run(afterReboot, 1000), "reboot with the contact still closed alerts again");
}

// Telegram re-delivers an update until a later poll acknowledges it with a higher offset,
// and it keeps the unacknowledged ones for 24 h. Holding that offset in RAM alone meant a
// reset replayed every command the user had sent since.
static void testBotOffsetSurvivesReboot()
{
    section("Telegram update offset");

    resetWorld(true);
    CHECK(Store::botOffset() == 0, "fresh flash starts from the beginning");

    Store::setBotOffset(4312);
    resetWorld(false);                         // reboot, flash intact
    Store::begin();
    CHECK(Store::botOffset() == 4312, "offset survives a restart, so updates are not replayed");
}

int main()
{
    printf("garageAlarms host tests\n");
    testDebounce();
    testWarmup();
    testAlarmActiveAtBoot();
    testDebounceAcrossStall();
    testRebootDuringHeldContact();
    testMute();
    testNotifierMuteGating();
    testNotifierDelivery();
    testQueueSurvivesReboot();
    testUnconfirmedSendIsNotDelivery();
    testNoSendWhileReplyOutstanding();
    testMuteScreenText();
    testCoalescing();
    testPushoverDuplicatesAlarms();
    testPushoverFailureDoesNotLoseTelegram();
    testPushoverUnconfigured();
    testMuteParsing();
    testBotOffsetSurvivesReboot();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
