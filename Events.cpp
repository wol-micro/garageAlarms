#include "Events.h"
#include "Util.h"
#include "Net.h"
#include "Store.h"
#include "config.h"

namespace Events {

const char *name(EventType type)
{
    switch (type) {
        case EventType::AlarmSmoke:     return "alarm";
        case EventType::Motion:         return "motion";
        case EventType::BootCold:       return "boot";
        case EventType::BootUnexpected: return "reboot";
        case EventType::Heartbeat:      return "heartbeat";
        case EventType::Test:           return "test";
        case EventType::AlarmOngoing:   return "alarm-ongoing";
        case EventType::SensorStuck:    return "sensor-stuck";
        default:                        return "none";
    }
}

bool isCritical(EventType type)
{
    return type == EventType::AlarmSmoke
        || type == EventType::AlarmOngoing
        || type == EventType::SensorStuck;
}

String muteScreen(bool muted, int64_t until)
{
    String s = "🔕 <b>Режим тишины</b>\n\n";
    if (muted) {
        s += "Сейчас: <b>тишина до " + Util::fmtShort(until) + "</b>\n\n";
        s += "<i>Сообщения о движении не приходят. Тревога о дыме приходит всегда.</i>";
    } else {
        s += "Сейчас: <b>уведомления включены</b>\n\n";
        s += "<i>Приходит всё: и движение, и тревога о дыме.</i>";
    }
    return s;
}

bool bypassesMute(EventType type)
{
    return isCritical(type) || type == EventType::Test;
}

String render(EventType type, int64_t ts, bool late)
{
    String s;
    s.reserve(320);

    switch (type) {

    case EventType::AlarmSmoke:
        s += "🚨 <b>ТРЕВОГА — ДЫМ</b>\n";
        s += "<i>Гараж, датчик дыма</i>\n\n";
        s += "🕒 " + Util::fmtTime(ts) + "\n";
        if (late)
            s += "\n⏳ <i>Доставлено с задержкой — в момент срабатывания связи не было.</i>";
        break;

    case EventType::Motion:
        s += "👁 <b>Движение в гараже</b>\n\n";
        s += "🕒 " + Util::fmtTime(ts) + "\n";
        s += "<i>Следующее сообщение о движении — не раньше чем через ";
        s += Util::fmtDuration(MOTION_COOLDOWN_MS);
        s += ".</i>";
        if (late)
            s += "\n\n⏳ <i>Доставлено с задержкой.</i>";
        break;

    case EventType::AlarmOngoing: {
        s += "🚨 <b>ТРЕВОГА ПРОДОЛЖАЕТСЯ</b>\n";
        s += "<i>Датчик дыма всё ещё замкнут</i>\n\n";
        s += "🕒 Началось: " + Util::fmtTime(ts, false) + "\n";
        if (Util::clockReady() && ts > 0) {
            const int64_t elapsed = (int64_t)time(nullptr) - ts;
            if (elapsed > 0)
                s += "⏳ Длится уже " + Util::fmtDuration((uint64_t)elapsed * 1000ULL);
        }
        break;
    }

    case EventType::SensorStuck:
        s += "⚠️ <b>Похоже, датчик движения неисправен</b>\n\n";
        s += "Вход удерживается активным дольше, чем возможно при реальном движении — "
             "вероятен обрыв, залипание реле или короткое замыкание в шлейфе.\n\n";
        s += "🕒 " + Util::fmtTime(ts) + "\n";
        s += "<i>До устранения детектор движения не даёт полезного сигнала.</i>";
        break;

    case EventType::BootCold:
        s += "🟢 <b>Система на связи</b>\n\n";
        s += "Причина запуска: " + String(Net::resetReasonText()) + "\n";
        s += "🕒 " + Util::fmtTime(ts, false) + "\n";
        s += "👥 Подписчиков: " + String(Store::subCount());
        break;

    case EventType::BootUnexpected:
        s += "⚠️ <b>Контроллер перезагрузился</b>\n\n";
        s += "Причина: <b>" + String(Net::resetReasonText()) + "</b>\n";
        s += "Перезагрузка №" + String(Store::bootCount()) + "\n";
        s += "🕒 " + Util::fmtTime(ts, false) + "\n\n";
        s += "<i>Подписчики и очередь уведомлений восстановлены из памяти.</i>";
        break;

    case EventType::Heartbeat:
        s += "💚 <b>Всё спокойно</b>\n\n";
        s += "🕒 " + Util::fmtTime(ts, false) + "\n";
        s += "📶 Связь: " + String(Net::rssi()) + " dBm (" + Util::rssiWord(Net::rssi()) + ")\n";
        s += "⏱ Аптайм: " + Util::fmtUptime(Net::uptimeMs()) + "\n";
        s += "🚨 Последняя тревога: " + Util::fmtAgo(Store::lastAlarmTs()) + "\n";
        s += "👁 Последнее движение: " + Util::fmtAgo(Store::lastMotionTs());
        break;

    case EventType::Test:
        s += "🧪 <b>Тестовое уведомление</b>\n\n";
        s += "Если ты это видишь — доставка работает.\n";
        s += "🕒 " + Util::fmtTime(ts);
        break;

    default:
        s += "…";
        break;
    }

    return s;
}

} // namespace Events
