#include "BotUI.h"
#include "Store.h"
#include "Net.h"
#include "Util.h"
#include "Events.h"
#include "Notifier.h"
#include "AppState.h"
#include "config.h"
#include "secrets.h"
#include "src/AsyncTelegram2/AsyncTelegram2.h"
#include "src/AsyncTelegram2/InlineKeyboard.h"

namespace {

AsyncTelegram2 *g_bot = nullptr;

// Bump whenever the command list below changes, so it gets re-registered once.
const uint32_t MENU_VERSION = 3;

bool g_menuChecked = false;

bool isOwner(int64_t id) { return OWNER_CHAT_ID != 0 && id == (int64_t)OWNER_CHAT_ID; }

// --- keyboards -------------------------------------------------------------

String kbMain(int64_t chatId)
{
    InlineKeyboard kb;
    kb.addRow();
    kb.addButton("📟 Статус", "s", KeyboardButtonQuery);
    kb.addButton("🧪 Тест",   "t", KeyboardButtonQuery);
    kb.addRow();
    kb.addButton("🔕 Тишина",  "k", KeyboardButtonQuery);
    kb.addButton("❓ Справка", "h", KeyboardButtonQuery);
    if (isOwner(chatId)) {
        kb.addRow();
        kb.addButton("👥 Подписчики", "w", KeyboardButtonQuery);
    }
    return kb.getJSON();
}

String kbBack()
{
    InlineKeyboard kb;
    kb.addRow();
    kb.addButton("🔄 Обновить", "s", KeyboardButtonQuery);
    kb.addButton("← Назад",     "m", KeyboardButtonQuery);
    return kb.getJSON();
}

String kbJustBack()
{
    InlineKeyboard kb;
    kb.addRow();
    kb.addButton("← Назад", "m", KeyboardButtonQuery);
    return kb.getJSON();
}

String kbMute()
{
    InlineKeyboard kb;
    kb.addRow();
    kb.addButton("1 ч",  "k1", KeyboardButtonQuery);
    kb.addButton("4 ч",  "k4", KeyboardButtonQuery);
    kb.addButton("8 ч",  "k8", KeyboardButtonQuery);
    kb.addRow();
    kb.addButton("🔔 Снять тишину", "k0", KeyboardButtonQuery);
    kb.addRow();
    kb.addButton("← Назад", "m", KeyboardButtonQuery);
    return kb.getJSON();
}

// --- screens ---------------------------------------------------------------

String txtMain()
{
    String s;
    s += "🏠 <b>Garage Bot</b>\n\n";
    s += Net::isOnline() ? "🟢 На связи" : "🔴 Нет сети";
    s += " · ";
    if (Store::isMuted())
        s += "🔕 тишина до " + Util::fmtShort(Store::muteUntil());
    else
        s += "🔔 уведомления включены";
    s += "\n\n<i>Выбери действие:</i>";
    return s;
}

String txtStatus()
{
    const int r = Net::rssi();
    String s;
    s += "📟 <b>Состояние системы</b>\n\n";

    s += "📶 Связь: ";
    if (Net::isOnline())
        s += String(r) + " dBm (" + Util::rssiWord(r) + ")\n";
    else
        s += "нет\n";
    s += "🕒 Время: " + Util::fmtTime((int64_t)time(nullptr), false) + "\n";
    s += "⏱ Аптайм: " + Util::fmtUptime(Net::uptimeMs()) + "\n";
    s += "🔁 Перезагрузок: " + String(Store::bootCount()) + "\n";
    s += "   последняя: <i>" + String(Net::resetReasonText()) + "</i>\n";

    // The distribution is the diagnostic: a bare count cannot tell a flaky power supply
    // from a firmware hang.
    String hist;
    for (uint8_t i = 0; i < RESET_SLOTS; i++) {
        uint32_t n = Store::resetReasonCount(i);
        if (!n) continue;
        if (hist.length()) hist += ", ";
        hist += String(Net::resetReasonName(i)) + " " + String(n);
    }
    if (hist.length())
        s += "   причины: <code>" + hist + "</code>\n";
    s += "\n";

    s += "🚨 Тревог всего: " + String(Store::alarmCount()) + "\n";
    s += "   последняя: " + Util::fmtAgo(Store::lastAlarmTs()) + "\n";
    s += "👁 Движение: " + Util::fmtAgo(Store::lastMotionTs()) + "\n\n";

    s += "🔌 Уровни: GPIO" + String(PIN_ALARM) + "=" + String(digitalRead(PIN_ALARM))
       + " (норма 1), GPIO" + String(PIN_MOTION) + "=" + String(digitalRead(PIN_MOTION))
       + " (норма 0)\n";
    s += "🔌 Датчики: дым — ";
    s += appAlarmActive() ? "<b>СРАБОТАЛ</b>" : "норма";
    s += ", движение — ";
    s += appMotionActive() ? "<b>активно</b>" : "норма";
    s += "\n";

    s += "🔔 Уведомления: ";
    s += Store::isMuted() ? ("тишина до " + Util::fmtShort(Store::muteUntil()))
                          : String("включены");
    s += "\n";
    s += "👥 Подписчиков: " + String(Store::subCount()) + "\n";

    s += "📨 Очередь: ";
    s += Notifier::pending() ? String(Notifier::pending()) + " в ожидании" : String("пусто");
    s += " <i>(доставлено " + String(Notifier::deliveredCount());
    s += ", потеряно " + String(Notifier::droppedCount()) + ")</i>\n";

    s += "💾 Память: " + String(ESP.getFreeHeap() / 1024) + " КБ свободно";
    return s;
}

String txtHelp(int64_t chatId)
{
    String s;
    s += "❓ <b>Справка</b>\n\n";
    s += "/status — состояние системы\n";
    s += "/mute 4 — тишина на 4 часа\n";
    s += "/unmute — включить уведомления\n";
    s += "/test — проверить доставку\n";
    s += "/menu — главное меню\n";
    s += "/unsubscribe — отписаться\n";
    if (isOwner(chatId)) {
        s += "\n<b>Только для владельца:</b>\n";
        s += "/who — список подписчиков\n";
        s += "/reboot — перезагрузить контроллер\n";
    }
    s += "\n🚨 <i>Тревога о дыме приходит всегда, режим тишины на неё не влияет.</i>";
    return s;
}

String txtWho()
{
    String s = "👥 <b>Подписчики (" + String(Store::subCount()) + ")</b>\n\n";
    const Subscriber *list = Store::subs();
    for (uint8_t i = 0; i < Store::subCount(); i++) {
        s += String(i + 1) + ". ";
        s += Util::htmlEscape(String(list[i].name));
        s += " <code>" + String((long long)list[i].id) + "</code>";
        if (isOwner(list[i].id))
            s += " 👑";
        s += "\n";
    }
    return s;
}

String txtMuted()
{
    return Events::muteScreen(Store::isMuted(), Store::muteUntil());
}

// --- helpers ---------------------------------------------------------------

void show(const TBMessage &msg, const String &text, const String &kb)
{
    // For a button press we edit the existing message in place instead of posting a new one,
    // so the chat does not fill up with menu copies.
    if (msg.messageType == MessageQuery && msg.messageID)
        g_bot->editMessage(msg.chatId, msg.messageID, text, kb);
    else
        g_bot->sendMessage(msg, text.c_str(), (char *)kb.c_str());
    Net::noteBotOk();
}

void applyMute(const TBMessage &msg, uint32_t hours)
{
    Serial.printf("[bot] mute request: %u h\n", hours);
    if (hours == 0) {
        Store::setMuteUntil(0);
        Serial.println("[bot] mute OFF");
    } else {
        if (!Util::clockReady()) {
            show(msg, "⚠️ Время ещё не синхронизировано, режим тишины недоступен.", kbJustBack());
            return;
        }
        Store::setMuteUntil((int64_t)time(nullptr) + (int64_t)hours * 3600);
        Serial.printf("[bot] mute ON until epoch %lld\n", (long long)Store::muteUntil());
    }
    show(msg, txtMuted(), kbMute());
}

String senderName(const TBMessage &msg)
{
    String n = msg.sender.firstName;
    if (msg.sender.username.length())
        n += " @" + msg.sender.username;
    n.trim();
    return n.length() ? n : String("user");
}

// --- dispatch --------------------------------------------------------------

void handleCallback(const TBMessage &msg)
{
    const String &d = msg.callbackQueryData;
    Serial.printf("[bot] <- %lld button [%s]\n", (long long)msg.chatId, d.c_str());

    if (!Store::isSubscribed(msg.chatId)) {
        g_bot->endQuery(msg, "Нет доступа", true);
        return;
    }

    if      (d == "m")  show(msg, txtMain(),   kbMain(msg.chatId));
    else if (d == "s")  show(msg, txtStatus(), kbBack());
    else if (d == "h")  show(msg, txtHelp(msg.chatId), kbJustBack());
    else if (d == "k")  show(msg, txtMuted(),  kbMute());
    else if (d == "k0") applyMute(msg, 0);
    else if (d == "k1") applyMute(msg, 1);
    else if (d == "k4") applyMute(msg, 4);
    else if (d == "k8") applyMute(msg, 8);
    else if (d == "w") {
        if (isOwner(msg.chatId)) show(msg, txtWho(), kbJustBack());
        else                     g_bot->endQuery(msg, "Только для владельца", true);
        return;
    }
    else if (d == "t") {
        Notifier::enqueue(msg.chatId, EventType::Test,
                          Util::clockReady() ? (int64_t)time(nullptr) : 0);
        g_bot->endQuery(msg, "Тест поставлен в очередь");
        return;
    }

    g_bot->endQuery(msg, "");
}

void handleText(const TBMessage &msg)
{
    String t = msg.text;
    t.trim();
    Serial.printf("[bot] <- %lld: %s\n", (long long)msg.chatId, t.c_str());

    // --- subscribe: the only command a stranger may use ---
    if (t.startsWith("/subscribe") || t.startsWith("/start")) {
        String pin = "";
        int sp = t.indexOf(' ');
        if (sp > 0) {
            pin = t.substring(sp + 1);
            pin.trim();
        }

        if (Store::isSubscribed(msg.chatId)) {
            show(msg, txtMain(), kbMain(msg.chatId));
            return;
        }
        if (pin.length() == 0) {
            g_bot->sendMessage(msg,
                "🔐 <b>Garage Bot</b>\n\n"
                "Бот присылает тревоги из гаража: дым и движение.\n\n"
                "Доступ по коду. Отправь:\n"
                "<code>/subscribe КОД</code>");
            return;
        }
        if (pin != SUBSCRIBE_PIN) {
            Serial.printf("[bot] wrong PIN from %lld\n", (long long)msg.chatId);
            g_bot->sendMessage(msg, "⛔️ Неверный код.");
            return;
        }
        if (Store::addSubscriber(msg.chatId, senderName(msg).c_str())) {
            Serial.printf("[bot] subscribed %lld\n", (long long)msg.chatId);
            g_bot->sendMessage(msg, "✅ <b>Подписка оформлена.</b>\nТеперь тревоги приходят сюда.");
            show(msg, txtMain(), kbMain(msg.chatId));
        } else {
            g_bot->sendMessage(msg, "⚠️ Список подписчиков заполнен.");
        }
        return;
    }

    // --- everything else requires a subscription ---
    if (!Store::isSubscribed(msg.chatId)) {
        g_bot->sendMessage(msg,
            "🔐 Доступ по коду.\nОтправь <code>/subscribe КОД</code>");
        return;
    }

    if (t == "/menu") {
        show(msg, txtMain(), kbMain(msg.chatId));
    }
    else if (t == "/status") {
        show(msg, txtStatus(), kbBack());
    }
    else if (t == "/help") {
        show(msg, txtHelp(msg.chatId), kbJustBack());
    }
    else if (t == "/test") {
        Notifier::enqueue(msg.chatId, EventType::Test,
                          Util::clockReady() ? (int64_t)time(nullptr) : 0);
        g_bot->sendMessage(msg, "🧪 Тестовое сообщение поставлено в очередь.");
    }
    else if (t.startsWith("/mute")) {
        // "no argument" and "zero" are different intents: a bare /mute means the default
        // window, while /mute 0 is how people write "turn it off". Collapsing the two made
        // the most natural way to disable mute switch it back on instead.
        applyMute(msg, Util::parseMuteHours(t, 4, MAX_MUTE_HOURS));
    }
    else if (t == "/unmute") {
        applyMute(msg, 0);
    }
    else if (t == "/unsubscribe") {
        if (Store::removeSubscriber(msg.chatId))
            g_bot->sendMessage(msg, "👋 Отписка выполнена. Чтобы вернуться — <code>/subscribe КОД</code>");
        else
            g_bot->sendMessage(msg, "⚠️ Владельца отписать нельзя.");
    }
    else if (t == "/who") {
        if (isOwner(msg.chatId)) show(msg, txtWho(), kbJustBack());
        else                     g_bot->sendMessage(msg, "⛔️ Только для владельца.");
    }
    else if (t == "/reboot") {
        if (isOwner(msg.chatId)) {
            g_bot->sendMessage(msg, "♻️ Перезагружаюсь…");
            appRequestReboot();
        } else {
            g_bot->sendMessage(msg, "⛔️ Только для владельца.");
        }
    }
    else {
        show(msg, txtMain(), kbMain(msg.chatId));
    }
}

// Register the native "/" command list once per firmware menu revision. Each call is two
// blocking round-trips, so doing it on every boot would be a needless stall.
void ensureCommandMenu()
{
    if (g_menuChecked || !Net::isOnline())
        return;
    g_menuChecked = true;

    if (Store::menuVersion() == MENU_VERSION)
        return;

    g_bot->setMyCommands("menu",        "Главное меню");
    g_bot->setMyCommands("status",      "Состояние системы");
    g_bot->setMyCommands("mute",        "Тишина на N часов");
    g_bot->setMyCommands("unmute",      "Включить уведомления");
    g_bot->setMyCommands("test",        "Проверить доставку");
    g_bot->setMyCommands("help",        "Справка");
    g_bot->setMyCommands("unsubscribe", "Отписаться от тревог");

    Store::setMenuVersion(MENU_VERSION);
    Serial.println("[bot] command menu registered");
}

} // namespace

namespace BotUI {

void begin(AsyncTelegram2 *bot)
{
    g_bot = bot;
    g_bot->setFormattingStyle(AsyncTelegram2::FormatStyle::HTML);
}

void loop()
{
    if (!g_bot)
        return;

    ensureCommandMenu();

    TBMessage msg;
    if (!g_bot->getNewMessage(msg))
        return;

    Net::noteBotOk();

    if (msg.messageType == MessageQuery)
        handleCallback(msg);
    else if (msg.messageType == MessageText)
        handleText(msg);
}

} // namespace BotUI
