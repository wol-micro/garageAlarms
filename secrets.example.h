#pragma once
//
// Copy this file to `secrets.h` and fill in real values.
// `secrets.h` is gitignored — keep it that way.
//
#define WIFI_SSID       "your-wifi-ssid"
#define WIFI_PASS       "your-wifi-password"

// From @BotFather.
#define BOT_TOKEN       "000000000:XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"

// Your own Telegram chat id (get it from @JsonDumpBot). The owner is auto-subscribed on
// first boot, is never dropped by /unsubscribe, and is the only one who may use /who
// and /reboot.
#define OWNER_CHAT_ID   0

// Shared secret for /subscribe. Without it anyone who finds the bot can watch your garage.
#define SUBSCRIBE_PIN   "change-me"

// Pushover needs BOTH values. The user key is on the dashboard home page; the application
// token comes from "Create an Application/API Token" there. Leave them as-is to disable the
// Pushover channel entirely — Telegram keeps working on its own.
#define PUSHOVER_TOKEN  "your-pushover-app-token"
#define PUSHOVER_USER   "your-pushover-user-key"
