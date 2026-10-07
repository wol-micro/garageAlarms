#include "Net.h"
#include "config.h"
#include "secrets.h"
#include "Util.h"
#include <WiFi.h>
#include <esp_system.h>
#include <time.h>

namespace {

Net::State g_state = Net::State::Idle;

uint32_t g_lastAttemptMs   = 0;
uint32_t g_offlineSinceMs  = 0;   // 0 => currently online
uint32_t g_lastBotOkMs     = 0;
bool     g_pendingAlerts   = false;
bool     g_timeConfigured  = false;

esp_reset_reason_t g_resetReason = ESP_RST_UNKNOWN;

// LED
uint32_t g_ledLastMs = 0;
bool     g_ledOn     = false;

void startConnect()
{
    g_lastAttemptMs = millis();
    g_state = Net::State::Connecting;
    WiFi.disconnect(false, false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[net] connecting to %s\n", WIFI_SSID);
}

void updateLed()
{
    const uint32_t now = millis();
    uint32_t period;

    if (g_state != Net::State::Online)
        period = 200;                    // hunting for WiFi: fast blink
    else if (g_pendingAlerts)
        period = 80;                     // alerts queued: very fast
    else
        period = g_ledOn ? 60 : 3000;    // online: brief heartbeat flash

    if (now - g_ledLastMs >= period) {
        g_ledLastMs = now;
        g_ledOn = !g_ledOn;
        digitalWrite(PIN_LED, g_ledOn ? HIGH : LOW);
    }
}

} // namespace

namespace Net {

void begin()
{
    g_resetReason = esp_reset_reason();

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    WiFi.persistent(false);       // don't rewrite flash with credentials on every boot
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);         // modem sleep adds latency and drops on a weak link

    g_offlineSinceMs = millis();
    g_lastBotOkMs    = millis();
    startConnect();
}

void loop()
{
    const uint32_t now = millis();
    const bool connected = (WiFi.status() == WL_CONNECTED);

    if (connected) {
        if (g_state != State::Online) {
            g_state = State::Online;
            g_offlineSinceMs = 0;
            Serial.printf("[net] online, ip=%s rssi=%d\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            // NTP must be (re)configured once we actually have a link.
            if (!g_timeConfigured) {
                configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);
                g_timeConfigured = true;
            }
        }
    } else {
        if (g_state == State::Online) {
            Serial.println("[net] link lost");
            g_offlineSinceMs = now;
        }
        if (g_offlineSinceMs == 0)
            g_offlineSinceMs = now;

        if (now - g_lastAttemptMs >= WIFI_RETRY_INTERVAL_MS) {
            g_state = State::Idle;
            startConnect();
        }

        // Software watchdog: a box that cannot reach the network is useless as an alarm.
        // A clean reboot is more likely to recover than sitting in a broken state.
        if (now - g_offlineSinceMs >= WIFI_DEAD_REBOOT_MS) {
            Serial.println("[net] offline too long, rebooting");
            Serial.flush();
            delay(100);
            ESP.restart();
        }
    }

    // WiFi up but Telegram unreachable for a long stretch (DNS poisoned, TLS wedged,
    // captive portal): also worth a clean restart.
    if (connected && (now - g_lastBotOkMs >= BOT_DEAD_REBOOT_MS)) {
        Serial.println("[net] telegram unreachable too long, rebooting");
        Serial.flush();
        delay(100);
        ESP.restart();
    }

    updateLed();
}

bool  isOnline() { return g_state == State::Online && WiFi.status() == WL_CONNECTED; }
State state()    { return g_state; }
int   rssi()     { return isOnline() ? WiFi.RSSI() : 0; }
uint64_t uptimeMs() { return (uint64_t)esp_timer_get_time() / 1000ULL; }

bool timeSynced() { return Util::clockReady(); }

void noteBotOk() { g_lastBotOkMs = millis(); }

void setPendingAlerts(bool pending) { g_pendingAlerts = pending; }

bool wasUnexpectedReset()
{
    switch (g_resetReason) {
        case ESP_RST_POWERON:
        case ESP_RST_EXT:
        case ESP_RST_SW:        // our own ESP.restart()
        case ESP_RST_USB:       // host replugged the cable or opened the port
        case ESP_RST_JTAG:      // debugger / flashing tool
        case ESP_RST_UNKNOWN:   // what a USB-Serial/JTAG reset reports on the S3
            return false;
        default:
            return true;
    }
}

uint8_t resetReasonCode() { return (uint8_t)g_resetReason; }

const char *resetReasonName(uint8_t code)
{
    switch ((esp_reset_reason_t)code) {
        case ESP_RST_POWERON:    return "питание";
        case ESP_RST_EXT:        return "RESET";
        case ESP_RST_SW:         return "софт";
        case ESP_RST_PANIC:      return "паника";
        case ESP_RST_INT_WDT:    return "int-WDT";
        case ESP_RST_TASK_WDT:   return "task-WDT";
        case ESP_RST_WDT:        return "WDT";
        case ESP_RST_BROWNOUT:   return "brownout";
        case ESP_RST_PWR_GLITCH: return "глитч";
        case ESP_RST_CPU_LOCKUP: return "lockup";
        case ESP_RST_DEEPSLEEP:  return "сон";
        case ESP_RST_USB:        return "USB";
        case ESP_RST_JTAG:       return "JTAG";
        case ESP_RST_EFUSE:      return "eFuse";
        case ESP_RST_SDIO:       return "SDIO";
        default:                 return "?";
    }
}

const char *resetReasonText()
{
    switch (g_resetReason) {
        case ESP_RST_POWERON:    return "включение питания";
        case ESP_RST_EXT:        return "внешний сброс (кнопка RESET)";
        case ESP_RST_SW:         return "программная перезагрузка";
        case ESP_RST_PANIC:      return "паника / исключение в прошивке";
        case ESP_RST_INT_WDT:    return "interrupt watchdog";
        case ESP_RST_TASK_WDT:   return "task watchdog (зависание loop)";
        case ESP_RST_WDT:        return "прочий watchdog";
        case ESP_RST_BROWNOUT:   return "просадка питания (brownout)";
        case ESP_RST_PWR_GLITCH: return "бросок питания (power glitch)";
        case ESP_RST_CPU_LOCKUP: return "зависание CPU (двойное исключение)";
        case ESP_RST_DEEPSLEEP:  return "выход из deep sleep";
        case ESP_RST_USB:        return "сброс по USB";
        case ESP_RST_JTAG:       return "сброс по JTAG";
        case ESP_RST_EFUSE:      return "ошибка eFuse";
        case ESP_RST_SDIO:       return "сброс по SDIO";
        default:                 return "не определена";
    }
}

} // namespace Net
