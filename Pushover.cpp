#include "Pushover.h"
#include "config.h"
#include "secrets.h"
#include "pushover_cert.h"
#include <WiFiClientSecure.h>

namespace {

const char *HOST = "api.pushover.net";
const uint16_t PORT = 443;

// Percent-encode for application/x-www-form-urlencoded. The messages are UTF-8 with emoji
// and newlines, so anything outside the unreserved set has to be escaped byte by byte.
String urlEncode(const String &s)
{
    static const char *hex = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() * 3 / 2);
    for (size_t i = 0; i < s.length(); i++) {
        const uint8_t c = (uint8_t)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

} // namespace

namespace Pushover {

bool configured()
{
    return strlen(PUSHOVER_TOKEN) > 0 && strlen(PUSHOVER_USER) > 0 &&
           strcmp(PUSHOVER_TOKEN, "your-pushover-app-token") != 0;
}

bool send(const String &title, const String &message, int priority,
          const char *sound, uint32_t retrySec, uint32_t expireSec)
{
    if (!configured())
        return false;

    String body;
    body.reserve(message.length() * 2 + 256);
    body  = "token=" + urlEncode(PUSHOVER_TOKEN);
    body += "&user=" + urlEncode(PUSHOVER_USER);
    body += "&title=" + urlEncode(title);
    body += "&message=" + urlEncode(message);
    body += "&html=1";                       // the alert text already carries <b>/<i>
    body += "&priority=" + String(priority);
    if (sound && *sound)
        body += "&sound=" + urlEncode(sound);
    if (priority >= 2) {
        body += "&retry=" + String(retrySec);
        body += "&expire=" + String(expireSec);
    }

    WiFiClientSecure client;
    client.setCACert(pushover_root_ca);
    client.setTimeout(PUSHOVER_TIMEOUT_MS / 1000);

    if (!client.connect(HOST, PORT)) {
        Serial.println("[push] connect failed");
        return false;
    }

    String req;
    req.reserve(body.length() + 200);
    req  = "POST /1/messages.json HTTP/1.1\r\n";
    req += "Host: ";
    req += HOST;
    req += "\r\n";
    req += "Connection: close\r\n";
    req += "Content-Type: application/x-www-form-urlencoded\r\n";
    req += "Content-Length: " + String(body.length()) + "\r\n\r\n";
    req += body;

    if (client.print(req) != req.length()) {
        Serial.println("[push] short write");
        client.stop();
        return false;
    }

    // Bounded read: never spin, never block the loop longer than the budget.
    String reply;
    reply.reserve(512);
    const uint32_t start = millis();
    uint32_t lastRx = millis();
    while (millis() - start < PUSHOVER_TIMEOUT_MS) {
        if (client.available()) {
            reply += (char)client.read();
            lastRx = millis();
            if (reply.length() > 2048)
                break;
            continue;
        }
        if (!client.connected())
            break;
        if (millis() - lastRx > PUSHOVER_REPLY_IDLE_MS)
            break;
        delay(1);
    }
    client.stop();

    const bool ok = reply.indexOf("\"status\":1") > -1;
    if (!ok) {
        const int b = reply.indexOf("\r\n\r\n");
        Serial.printf("[push] rejected: %s\n", b > -1 ? reply.substring(b + 4).c_str()
                                                      : reply.c_str());
    }
    return ok;
}

} // namespace Pushover
