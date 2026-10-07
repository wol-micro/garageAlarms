// Garage alarm -> Telegram bridge for ESP32.
//
// Two dry-contact inputs (smoke and motion) are debounced and broadcast to subscribers who
// registered with the bot. Everything that matters survives a reboot: the subscriber list,
// the mute window, and any notification that had not been delivered yet.
//
// See CLAUDE.md for the architecture and for what was wrong with the first version.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "config.h"
#include "secrets.h"
#include "Util.h"
#include "Store.h"
#include "Net.h"
#include "Sensors.h"
#include "Events.h"
#include "Notifier.h"
#include "BotUI.h"
#include "AppState.h"
#include "src/AsyncTelegram2/AsyncTelegram2.h"

static WiFiClientSecure secureClient;
static AsyncTelegram2   bot(secureClient);

static DebouncedInput alarmInput;
static DebouncedInput motionInput;

static bool     g_botStarted      = false;
static bool     g_bootNotified    = false;
static bool     g_rebootRequested = false;
static uint32_t g_rebootAtMs      = 0;

// While the smoke contact stays closed we re-send a reminder; the motion input instead gets
// a single "probably broken" warning, because sustained motion is not a real-world signal.
static int64_t  g_alarmStartedTs   = 0;
static uint32_t g_lastAlarmRepeatMs = 0;
static bool     g_motionStuckSent   = false;

// A reboot notice goes to the owner only, and is rate limited — otherwise a box stuck in a
// reset loop reproduces exactly the message flood it is meant to warn about. An unexpected
// reset is worth hearing about reasonably often; a routine power-up is not news unless it
// has been quiet for a long while.
static const int64_t BOOT_NOTIFY_GAP_UNEXPECTED_SEC = 15 * 60;
static const int64_t BOOT_NOTIFY_GAP_NORMAL_SEC     = 6 * 60 * 60;

// ---------------------------------------------------------------------------
// AppState hooks used by BotUI
// ---------------------------------------------------------------------------
bool appAlarmActive()  { return alarmInput.isActive(); }
bool appMotionActive() { return motionInput.isActive(); }

void appRequestReboot()
{
    g_rebootRequested = true;
    g_rebootAtMs = millis() + 2000;  // let the reply leave first
}

// ---------------------------------------------------------------------------

static int64_t nowEpoch()
{
    return Util::clockReady() ? (int64_t)time(nullptr) : 0;
}

static void startBotIfReady()
{
    if (g_botStarted || !Net::isOnline() || !Net::timeSynced())
        return;  // TLS certificate validation needs a synced clock

    secureClient.setCACert(telegram_cert);
    secureClient.setTimeout(10);

    bot.setUpdateTime(BOT_POLL_INTERVAL_MS);
    bot.setTelegramToken(BOT_TOKEN);
    bot.setFormattingStyle(AsyncTelegram2::FormatStyle::HTML);

    if (bot.begin()) {
        Serial.printf("[bot] connected as @%s\n", bot.getBotName());
        Net::noteBotOk();
        g_botStarted = true;
    } else {
        Serial.println("[bot] begin() failed, will retry");
    }
}

// Announce the boot once the clock is trustworthy, so the timestamp means something.
static void sendBootNoticeIfDue()
{
    if (g_bootNotified || !g_botStarted)
        return;
    if (!Util::clockReady() && Net::uptimeMs() < 60000)
        return;  // give NTP a minute before giving up on an accurate timestamp

    g_bootNotified = true;

    const int64_t now  = nowEpoch();
    const int64_t last = Store::lastBootNotifyTs();
    const bool unexpected = Net::wasUnexpectedReset();

    const int64_t gap = unexpected ? BOOT_NOTIFY_GAP_UNEXPECTED_SEC
                                   : BOOT_NOTIFY_GAP_NORMAL_SEC;
    if (now && last && (now - last) < gap) {
        Serial.println("[boot] notice suppressed (too soon after the last one)");
        return;
    }

    const EventType type = unexpected ? EventType::BootUnexpected : EventType::BootCold;
    // Owner only: subscribers care about the garage, not about our uptime.
    if (OWNER_CHAT_ID != 0)
        Notifier::enqueue((int64_t)OWNER_CHAT_ID, type, now);

    if (now)
        Store::setLastBootNotifyTs(now);

    Serial.printf("[boot] #%u, reason: %s\n", Store::bootCount(), Net::resetReasonText());
}

static void pollSensors()
{
    if (alarmInput.update()) {
        const int64_t ts = nowEpoch();
        Serial.println("[sensor] ALARM");
        Store::noteAlarm(ts);
        Notifier::broadcast(EventType::AlarmSmoke, ts);
        g_alarmStartedTs    = ts;
        g_lastAlarmRepeatMs = millis();
    }
    if (motionInput.update()) {
        const int64_t ts = nowEpoch();
        Serial.println("[sensor] motion");
        Store::noteMotion(ts);
        Notifier::broadcast(EventType::Motion, ts);
    }

    // Smoke contact still closed: remind, don't call it a fault. An alarm that is still
    // happening is exactly when a single notification is not enough.
    if (alarmInput.isActive()) {
        if (millis() - g_lastAlarmRepeatMs >= ALARM_REPEAT_MS) {
            g_lastAlarmRepeatMs = millis();
            Notifier::broadcast(EventType::AlarmOngoing, g_alarmStartedTs);
        }
    } else {
        g_alarmStartedTs = 0;
    }

    // Motion held active for hours is a wiring fault, and a fault that reads as "quiet" is
    // the dangerous kind — say so once, then stay silent until it clears.
    if (motionInput.activeForMs() > MOTION_STUCK_MS) {
        if (!g_motionStuckSent) {
            g_motionStuckSent = true;
            Notifier::broadcast(EventType::SensorStuck, nowEpoch());
        }
    } else if (!motionInput.isActive()) {
        g_motionStuckSent = false;
    }
}

// Daily "still alive" message, so silence from the bot is unambiguous.
static void heartbeat()
{
    if (!g_botStarted || !Util::clockReady())
        return;

    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);

    if (tmv.tm_hour != HEARTBEAT_HOUR)
        return;
    if (Store::heartbeatDay() == (int16_t)tmv.tm_yday)
        return;

    Store::setHeartbeatDay((int16_t)tmv.tm_yday);
    Notifier::broadcast(EventType::Heartbeat, (int64_t)now);
}

// ---------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(50);
    Serial.println("\n=== garageAlarms ===");

    Store::begin();
    Net::begin();   // non-blocking: we never wait here for WiFi
    Store::noteResetReason(Net::resetReasonCode());

    alarmInput.begin(PIN_ALARM,  PIN_ALARM_ACTIVE_HIGH,  ALARM_DEBOUNCE_MS,  ALARM_COOLDOWN_MS);
    motionInput.begin(PIN_MOTION, PIN_MOTION_ACTIVE_HIGH, MOTION_DEBOUNCE_MS, MOTION_COOLDOWN_MS,
                      MOTION_WARMUP_MS);

    Notifier::begin(&bot);
    BotUI::begin(&bot);

    // Raw levels at boot. The alarm input is pulled up and active LOW, so a quiet, correctly
    // wired contact must read 1 here; a 0 means the line is held down, not that there is smoke.
    Serial.printf("[boot] #%u, reset reason: %s, subscribers: %u\n",
                  Store::bootCount(), Net::resetReasonText(), Store::subCount());
    Serial.printf("[pins] alarm(GPIO%d)=%d (1=норма), motion(GPIO%d)=%d (0=норма)\n",
                  PIN_ALARM, digitalRead(PIN_ALARM), PIN_MOTION, digitalRead(PIN_MOTION));
}

void loop()
{
    Net::loop();
    pollSensors();          // sampled every iteration, never gated on the network

    startBotIfReady();

    if (g_botStarted) {
        sendBootNoticeIfDue();
        Notifier::loop();
        BotUI::loop();
        heartbeat();
    }

    if (g_rebootRequested && (int32_t)(millis() - g_rebootAtMs) >= 0) {
        Serial.println("[sys] reboot requested");
        Serial.flush();
        ESP.restart();
    }

    delay(2);   // yield to the WiFi and IDLE tasks; keeps the task watchdog happy
}
