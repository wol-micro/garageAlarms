#include "Store.h"
#include "secrets.h"
#include <Preferences.h>

namespace {

Preferences prefs;
const char *NS = "garage";

Subscriber g_subs[MAX_CLIENTS];
uint8_t    g_subCount = 0;

int64_t  g_muteUntil   = 0;
uint32_t g_bootCount   = 0;
uint32_t g_alarmCount  = 0;
int64_t  g_lastAlarm   = 0;
int64_t  g_lastMotion  = 0;
int16_t  g_hbDay       = -1;
uint32_t g_resetHist[RESET_SLOTS] = {0};
uint32_t g_txOk[2]   = {0, 0};
uint32_t g_txFail[2] = {0, 0};
uint32_t g_txRetry   = 0;

void persistSubs()
{
    prefs.putBytes("subs", g_subs, g_subCount * sizeof(Subscriber));
    prefs.putUChar("subN", g_subCount);
}

} // namespace

namespace Store {

void begin()
{
    prefs.begin(NS, false);

    g_subCount = prefs.getUChar("subN", 0);
    if (g_subCount > MAX_CLIENTS)
        g_subCount = 0;  // corrupt / schema change
    if (g_subCount)
        prefs.getBytes("subs", g_subs, g_subCount * sizeof(Subscriber));

    g_muteUntil  = prefs.getLong64("mute", 0);
    g_alarmCount = prefs.getUInt("alarmN", 0);
    g_lastAlarm  = prefs.getLong64("lastA", 0);
    g_lastMotion = prefs.getLong64("lastM", 0);
    g_hbDay      = prefs.getShort("hbDay", -1);

    g_bootCount = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", g_bootCount);

    if (prefs.getBytes("rhist", g_resetHist, sizeof(g_resetHist)) != sizeof(g_resetHist))
        memset(g_resetHist, 0, sizeof(g_resetHist));
    if (prefs.getBytes("txok", g_txOk, sizeof(g_txOk)) != sizeof(g_txOk))
        memset(g_txOk, 0, sizeof(g_txOk));
    if (prefs.getBytes("txfail", g_txFail, sizeof(g_txFail)) != sizeof(g_txFail))
        memset(g_txFail, 0, sizeof(g_txFail));
    g_txRetry = prefs.getUInt("txretry", 0);

    // The owner is always a subscriber. Without this, a fresh flash has nobody to alert.
    if (OWNER_CHAT_ID != 0 && !isSubscribed(OWNER_CHAT_ID))
        addSubscriber(OWNER_CHAT_ID, "owner");
}

uint8_t           subCount()  { return g_subCount; }
const Subscriber *subs()      { return g_subs; }

bool isSubscribed(int64_t id)
{
    for (uint8_t i = 0; i < g_subCount; i++)
        if (g_subs[i].id == id)
            return true;
    return false;
}

bool addSubscriber(int64_t id, const char *name)
{
    for (uint8_t i = 0; i < g_subCount; i++) {
        if (g_subs[i].id == id) {
            strlcpy(g_subs[i].name, name ? name : "", sizeof(g_subs[i].name));
            persistSubs();
            return false;  // already there, name refreshed
        }
    }
    if (g_subCount >= MAX_CLIENTS)
        return false;

    g_subs[g_subCount].id = id;
    strlcpy(g_subs[g_subCount].name, name ? name : "", sizeof(g_subs[g_subCount].name));
    g_subCount++;
    persistSubs();
    return true;
}

bool removeSubscriber(int64_t id)
{
    if (id == OWNER_CHAT_ID)
        return false;  // the owner cannot leave; otherwise nobody is left to re-add anyone

    for (uint8_t i = 0; i < g_subCount; i++) {
        if (g_subs[i].id == id) {
            for (uint8_t j = i; j + 1 < g_subCount; j++)
                g_subs[j] = g_subs[j + 1];
            g_subCount--;
            persistSubs();
            return true;
        }
    }
    return false;
}

int64_t muteUntil() { return g_muteUntil; }

void setMuteUntil(int64_t epoch)
{
    g_muteUntil = epoch;
    prefs.putLong64("mute", epoch);
}

bool isMuted()
{
    if (g_muteUntil == 0)
        return false;
    time_t now = time(nullptr);
    if (now < 1700000000)   // clock not synced yet; fail open, an alarm beats a silent box
        return false;
    return (int64_t)now < g_muteUntil;
}

void noteDelivery(uint8_t channel, bool ok)
{
    const uint8_t ch = channel < 2 ? channel : 0;
    if (ok) {
        g_txOk[ch]++;
        prefs.putBytes("txok", g_txOk, sizeof(g_txOk));
    } else {
        g_txFail[ch]++;
        prefs.putBytes("txfail", g_txFail, sizeof(g_txFail));
    }
}

void noteRetry()
{
    g_txRetry++;
    prefs.putUInt("txretry", g_txRetry);
}

uint32_t deliverySent(uint8_t channel)   { return g_txOk[channel < 2 ? channel : 0]; }
uint32_t deliveryFailed(uint8_t channel) { return g_txFail[channel < 2 ? channel : 0]; }
uint32_t deliveryRetries()               { return g_txRetry; }

void resetStats()
{
    g_bootCount = 0;
    memset(g_resetHist, 0, sizeof(g_resetHist));
    memset(g_txOk, 0, sizeof(g_txOk));
    memset(g_txFail, 0, sizeof(g_txFail));
    g_txRetry   = 0;
    g_alarmCount = 0;

    prefs.putUInt("boots", 0);
    prefs.putBytes("rhist", g_resetHist, sizeof(g_resetHist));
    prefs.putBytes("txok", g_txOk, sizeof(g_txOk));
    prefs.putBytes("txfail", g_txFail, sizeof(g_txFail));
    prefs.putUInt("txretry", 0);
    prefs.putUInt("alarmN", 0);
}

void noteResetReason(uint8_t reason)
{
    if (reason >= RESET_SLOTS)
        reason = 0;
    g_resetHist[reason]++;
    prefs.putBytes("rhist", g_resetHist, sizeof(g_resetHist));
}

uint32_t resetReasonCount(uint8_t reason)
{
    return reason < RESET_SLOTS ? g_resetHist[reason] : 0;
}

uint32_t bootCount()  { return g_bootCount; }
uint32_t alarmCount() { return g_alarmCount; }
int64_t  lastAlarmTs()  { return g_lastAlarm; }
int64_t  lastMotionTs() { return g_lastMotion; }

void noteAlarm(int64_t epoch)
{
    g_alarmCount++;
    g_lastAlarm = epoch;
    prefs.putUInt("alarmN", g_alarmCount);
    prefs.putLong64("lastA", epoch);
}

void noteMotion(int64_t epoch)
{
    g_lastMotion = epoch;
    prefs.putLong64("lastM", epoch);
}

int64_t lastBootNotifyTs()             { return prefs.getLong64("bootNt", 0); }
void    setLastBootNotifyTs(int64_t e) { prefs.putLong64("bootNt", e); }
int16_t heartbeatDay()                 { return g_hbDay; }
void    setHeartbeatDay(int16_t yday)  { g_hbDay = yday; prefs.putShort("hbDay", yday); }

uint32_t menuVersion() { return prefs.getUInt("menuV", 0); }
void     setMenuVersion(uint32_t v) { prefs.putUInt("menuV", v); }

// Telegram's update offset. Kept in flash because it is the only thing that stops an
// update being handed to us a second time after a reset: the server holds an update as
// pending for 24 h until a later poll acknowledges it with a higher offset.
int32_t botOffset()             { return (int32_t)prefs.getUInt("botOff", 0); }
void    setBotOffset(int32_t v) { prefs.putUInt("botOff", (uint32_t)v); }

void saveQueue(const QueueItem *items, uint8_t count)
{
    if (count == 0) {
        prefs.remove("queue");
        prefs.putUChar("queueN", 0);
        return;
    }
    prefs.putBytes("queue", items, count * sizeof(QueueItem));
    prefs.putUChar("queueN", count);
}

uint8_t loadQueue(QueueItem *items, uint8_t maxItems)
{
    uint8_t n = prefs.getUChar("queueN", 0);
    if (n == 0 || n > maxItems)
        return 0;
    size_t got = prefs.getBytes("queue", items, n * sizeof(QueueItem));
    if (got != n * sizeof(QueueItem))
        return 0;
    return n;
}

} // namespace Store
