// Fake Telegram client.
//
// The previous version returned true as soon as a send was "made", which quietly encoded the
// same wrong assumption the production code had: that handing bytes to the transport means
// the message was delivered. A stub built on your belief can only ever confirm your belief.
// This one separates the two, so a test can reproduce the real failure on a weak link —
// the TLS write succeeds and Telegram never sees the request.
#pragma once
#include <Arduino.h>
#include <vector>
#include <string>

struct SentMsg { int64_t chatId; std::string body; };

struct TBMessage { int64_t chatId = 0; };

class AsyncTelegram2 {
public:
    std::vector<SentMsg> delivered;   // reached Telegram and came back "ok":true
    std::vector<SentMsg> attempted;   // written to the socket, confirmed or not

    bool transportOk   = true;        // the socket accepts the bytes
    bool confirmOk     = true;        // Telegram answers "ok":true
    bool waitingReply  = false;       // a getUpdates reply is outstanding

    bool isWaitingReply() const { return waitingReply; }

    bool sendMessage(const TBMessage &m, const char *body, char * = nullptr, bool wait = false)
    {
        if (!transportOk)
            return false;
        attempted.push_back({m.chatId, body ? body : ""});
        if (wait && !confirmOk)
            return false;             // bytes went out, Telegram never confirmed
        delivered.push_back({m.chatId, body ? body : ""});
        return true;
    }

    bool sendTo(int64_t userid, const char *message)
    {
        TBMessage m; m.chatId = userid;
        return sendMessage(m, message);
    }
};
