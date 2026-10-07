#include "Notifier.h"
#include "Store.h"
#include "Net.h"
#include "Util.h"
#include "config.h"
#include "src/AsyncTelegram2/AsyncTelegram2.h"

namespace {

AsyncTelegram2 *g_bot = nullptr;

QueueItem g_queue[NOTIFY_QUEUE_MAX];
uint8_t   g_count = 0;

uint32_t g_lastSendMs   = 0;
uint32_t g_delivered    = 0;
uint32_t g_dropped      = 0;

// An event older than this by the time we deliver it gets a "sent late" note, so a
// timestamp from before an outage is not mistaken for something happening right now.
const int64_t LATE_AFTER_SEC = 120;

void persist()
{
    Store::saveQueue(g_queue, g_count);
    Net::setPendingAlerts(g_count > 0);
}

void removeAt(uint8_t idx)
{
    for (uint8_t i = idx; i + 1 < g_count; i++)
        g_queue[i] = g_queue[i + 1];
    g_count--;
}

// Make room by discarding the oldest non-critical item, falling back to the oldest item.
bool makeRoom()
{
    if (g_count < NOTIFY_QUEUE_MAX)
        return true;
    for (uint8_t i = 0; i < g_count; i++) {
        if (!Events::isCritical((EventType)g_queue[i].type)) {
            removeAt(i);
            g_dropped++;
            return true;
        }
    }
    removeAt(0);
    g_dropped++;
    return true;
}

uint32_t backoffMs(uint8_t attempts)
{
    uint32_t ms = NOTIFY_RETRY_BASE_MS;
    for (uint8_t i = 1; i < attempts && ms < NOTIFY_RETRY_MAX_MS; i++)
        ms *= 2;
    return ms > NOTIFY_RETRY_MAX_MS ? NOTIFY_RETRY_MAX_MS : ms;
}

} // namespace

namespace Notifier {

void begin(AsyncTelegram2 *bot)
{
    g_bot = bot;

    // Anything still queued when we lost power is delivered now.
    g_count = Store::loadQueue(g_queue, NOTIFY_QUEUE_MAX);
    for (uint8_t i = 0; i < g_count; i++) {
        g_queue[i].attempts      = 0;          // fresh budget after a reboot
        g_queue[i].nextAttemptMs = millis();
    }
    if (g_count)
        Serial.printf("[notify] restored %u queued message(s) from flash\n", g_count);
    Net::setPendingAlerts(g_count > 0);
}

// Returns true if the item was actually queued.
static bool enqueueNoPersist(int64_t chatId, EventType type, int64_t ts)
{
    // Mute silences chatter, never a real alarm and never an explicit delivery check.
    if (Store::isMuted() && !Events::bypassesMute(type)) {
        Serial.printf("[notify] %s suppressed by mute\n", Events::name(type));
        return false;
    }

    if (!makeRoom())
        return false;

    QueueItem &it    = g_queue[g_count++];
    it.chatId        = chatId;
    it.eventTs       = ts;
    it.type          = (uint8_t)type;
    it.attempts      = 0;
    it.reserved      = 0;
    it.nextAttemptMs = millis();
    return true;
}

void enqueue(int64_t chatId, EventType type, int64_t ts)
{
    if (enqueueNoPersist(chatId, type, ts)) {
        persist();
        Serial.printf("[notify] queued %s for %lld\n", Events::name(type), (long long)chatId);
    }
}

void broadcast(EventType type, int64_t ts)
{
    const Subscriber *list = Store::subs();
    const uint8_t n = Store::subCount();
    bool any = false;
    for (uint8_t i = 0; i < n; i++)
        any |= enqueueNoPersist(list[i].id, type, ts);
    if (any)
        persist();   // one flash write per event, not one per subscriber
    Serial.printf("[notify] queued %s for %u subscriber(s)\n", Events::name(type), n);
}

void loop()
{
    if (!g_bot || g_count == 0)
        return;
    if (!Net::isOnline())
        return;
    if (g_bot->isWaitingReply())
        return;   // a getUpdates reply is outstanding; ours would read its response instead

    const uint32_t now = millis();
    if (now - g_lastSendMs < NOTIFY_MIN_SEND_GAP_MS)
        return;   // stay well under Telegram's per-bot rate limit

    // One send per loop iteration keeps the message poller responsive.
    int idx = -1;
    for (uint8_t i = 0; i < g_count; i++) {
        if ((int32_t)(now - g_queue[i].nextAttemptMs) >= 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return;

    QueueItem &it = g_queue[idx];
    const int64_t nowEpoch = Util::clockReady() ? (int64_t)time(nullptr) : it.eventTs;
    const bool late = (nowEpoch - it.eventTs) > LATE_AFTER_SEC;

    const String body = Events::render((EventType)it.type, it.eventTs, late);

    g_lastSendMs = millis();

    // Confirmed send. Writing the bytes is not delivery: on a weak link the TLS write
    // succeeds into the socket buffer while the request never reaches Telegram, and treating
    // that as success deletes the alert without a trace. We wait for "ok":true instead.
    TBMessage out;
    out.chatId = it.chatId;
    const bool ok = g_bot->sendMessage(out, body.c_str(), nullptr, true);

    if (ok) {
        g_delivered++;
        Net::noteBotOk();
        removeAt(idx);
        persist();
        return;
    }

    Serial.printf("[notify] %s to %lld not confirmed (attempt %u), will retry\n",
                  Events::name((EventType)it.type), (long long)it.chatId, it.attempts + 1);
    it.attempts++;
    if (it.attempts >= NOTIFY_MAX_ATTEMPTS) {
        Serial.printf("[notify] giving up on %s for %lld after %u attempts\n",
                      Events::name((EventType)it.type), (long long)it.chatId, it.attempts);
        g_dropped++;
        removeAt(idx);
        persist();
    } else {
        it.nextAttemptMs = millis() + backoffMs(it.attempts);
    }
}

uint8_t  pending()        { return g_count; }
uint32_t deliveredCount() { return g_delivered; }
uint32_t droppedCount()   { return g_dropped; }

} // namespace Notifier
