#include "Journal.h"
#include "config.h"
#include "Util.h"
#include <stdarg.h>

namespace {

fs::FS *g_fs = nullptr;
bool    g_ready = false;

void stamp(char *out, size_t n)
{
    if (Util::clockReady()) {
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        strftime(out, n, "%Y-%m-%d %H:%M:%S", &tmv);
    } else {
        // Before NTP there is no wall clock, but uptime still orders the lines.
        snprintf(out, n, "+%10lu ms      ", (unsigned long)millis());
    }
}

// Keep one previous file so a rotation never loses the whole history at once.
void rotateIfNeeded()
{
    File f = g_fs->open(JOURNAL_PATH, FILE_READ);
    const size_t sz = f ? f.size() : 0;
    if (f) f.close();
    if (sz < JOURNAL_MAX_BYTES)
        return;

    g_fs->remove(JOURNAL_PREV_PATH);
    g_fs->rename(JOURNAL_PATH, JOURNAL_PREV_PATH);
}

} // namespace

namespace Journal {

bool begin(fs::FS &fs)
{
    g_fs = &fs;
    g_ready = true;
    return true;
}

bool ready() { return g_ready && g_fs; }

void log(const char *tag, const char *fmt, ...)
{
    if (!ready())
        return;

    char when[32];
    stamp(when, sizeof(when));

    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    rotateIfNeeded();

    File f = g_fs->open(JOURNAL_PATH, FILE_APPEND);
    if (!f)
        return;
    f.printf("%s %-7s %s\n", when, tag ? tag : "-", msg);
    f.close();
}

String tail(size_t maxBytes)
{
    if (!ready())
        return String("журнал недоступен");

    File f = g_fs->open(JOURNAL_PATH, FILE_READ);
    if (!f || f.size() == 0)
        return String("журнал пуст");

    const size_t sz = f.size();
    const size_t want = sz > maxBytes ? maxBytes : sz;
    f.seek(sz - want);

    String out;
    out.reserve(want + 16);
    while (f.available())
        out += (char)f.read();
    f.close();

    // Drop a partial first line so the output starts cleanly.
    if (want < sz) {
        const int nl = out.indexOf('\n');
        if (nl > -1)
            out = out.substring(nl + 1);
    }
    return out;
}

size_t size()
{
    if (!ready())
        return 0;
    File f = g_fs->open(JOURNAL_PATH, FILE_READ);
    const size_t sz = f ? f.size() : 0;
    if (f) f.close();
    return sz;
}

void clear()
{
    if (!ready())
        return;
    g_fs->remove(JOURNAL_PATH);
    g_fs->remove(JOURNAL_PREV_PATH);
}

} // namespace Journal
