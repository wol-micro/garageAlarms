#include "Pushover.h"
#include <vector>
#include <string>

// Mirrors the real transport's contract: send() reports whether Pushover *accepted* the
// message, not whether the bytes left the socket.
std::vector<std::string> g_pushSent;
bool g_pushConfigured = true;
bool g_pushAccepts    = true;
int  g_pushLastPriority = -99;

namespace Pushover {

bool configured() { return g_pushConfigured; }

bool send(const String &title, const String &message, int priority,
          const char *sound, uint32_t retrySec, uint32_t expireSec)
{
    (void)sound; (void)retrySec; (void)expireSec;
    if (!g_pushConfigured || !g_pushAccepts)
        return false;
    g_pushLastPriority = priority;
    g_pushSent.push_back(std::string(title.c_str()) + "|" + message.c_str());
    return true;
}

} // namespace Pushover
