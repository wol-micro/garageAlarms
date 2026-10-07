# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An ESP32-S3 sketch that watches two dry-contact inputs in a garage (smoke and motion) and
relays alerts to Telegram chats that subscribed to the bot.

It is an alarm system, so the ordering of concerns is: **never miss or silently drop an
event > never flood the user > everything else.** Most of the non-obvious code exists to
serve that ordering.

User-facing bot text is Russian; code, comments and logs are English.

## Build & flash

```bash
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=min_spiffs"
arduino-cli compile --fqbn "$FQBN" .
arduino-cli upload  --fqbn "$FQBN" -p /dev/ttyACM0 .
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

The target is an ESP32-S3 (N16R8, 16 MB flash). Plain `esp32:esp32:esp32` is **not**
supported: the code relies on `INPUT_PULLDOWN` and on the S3's USB/JTAG reset reasons.
`CDCOnBoot=cdc` is required, or `Serial` goes to the UART pins and nothing shows up over USB.

Verified against ESP32 core **3.3.11** and ArduinoJson **7.4.2**. Current size: ~53% of the
1.9 MB app partition.

`secrets.h` is gitignored. Copy `secrets.example.h` to `secrets.h` and fill it in (WiFi
credentials, bot token, `OWNER_CHAT_ID`, `SUBSCRIBE_PIN`), or the build fails on the first
include.

## Tests

```bash
./test/run.sh          # builds and runs on the host, no board needed
```

`test/` stages the hardware-independent sources into a scratch directory, overlays
`test/stubs/` (Arduino, Preferences/NVS, WiFi/Net, the Telegram client) and compiles with
plain g++. A fake clock and fake pin levels are what make debounce windows, cooldowns, PIR
warm-up, mute expiry and retry backoff testable at all — on hardware most of these take hours
to observe.

The bot stub models **"written to the transport"** and **"confirmed by Telegram"** as separate
things. An earlier version returned true for the act of sending, which encoded the same wrong
assumption the production code had, and so could never have caught the bug where alarms were
dropped from the queue without being delivered. A stub built on your belief can only confirm
your belief.

Covered: `DebouncedInput` (glitch rejection, cooldown, warm-up not arming the cooldown, a
contact already closed at boot), the mute window (expiry, persistence, fail-open on an
unsynced clock), what mute may silence, the `/mute` argument parser, the mute screen wording,
and `Notifier` delivery (offline retention, exactly-once, retry without duplicates, an
unconfirmed send never counting as delivery, queue survival across a reboot).

Not covered: anything needing the real radio or transport — `Net`, `BotUI` dispatch, and the
vendored library's HTTP parsing.

The stub for `secrets.h` is checked in as `test/stubs/secrets.h.in`, because `.gitignore`
excludes any file named `secrets.h` at any depth.

## Layout

| File | Role |
|---|---|
| `garageAlarms.ino` | wiring only: setup/loop, sensor polling, boot notice, heartbeat |
| `config.h` | every tunable (pins, debounce, cooldowns, timeouts). No secrets. |
| `AppState.h` | hooks implemented in the `.ino` so `BotUI` can read sensor state / request a reboot without depending on it |
| `Net.*` | non-blocking WiFi state machine, NTP, reset reason, dead-link reboot, LED |
| `Store.*` | everything that must survive a reboot (NVS via `Preferences`) |
| `Sensors.*` | `DebouncedInput`: time-based debounce + per-input cooldown |
| `Events.*` | event types and their Telegram HTML bodies (all alert text lives here) |
| `Notifier.*` | persisted outbound queue with retry/backoff |
| `BotUI.*` | commands, inline menus, native `/` command list |
| `Util.*` | time/duration formatting (Russian), HTML escaping |
| `src/AsyncTelegram2/` | **vendored and patched** copy of the library — see below |
| `test/` | host-side test suite and hardware stubs |

`src/` is compiled recursively by the Arduino build, which is why the vendored library lives
there. Nothing includes `<AsyncTelegram2.h>` with angle brackets, so the copy in
`~/Arduino/libraries` is not picked up. ArduinoJson is still a normal global library.

## The vendored library is patched — do not replace it blindly

`src/AsyncTelegram2/` is AsyncTelegram2 2.3.3 with fixes marked `PATCHED (garageAlarms)`
(`grep -rn "PATCHED" src` lists them). Re-vendoring upstream without re-applying them brings
back a reboot loop and duplicate deliveries:

- **`getUpdates()` body read** — upstream's loop condition was
  `(millis() - timeout > 1000) || pos < len`, which stays true forever once a second has
  elapsed. Any slow or truncated response spun an infinite loop with no yield → task
  watchdog → reboot. On a weak WiFi link this fired constantly, and since the old sketch
  sent "bot is online" from `setup()`, each reboot became a Telegram message. That was the
  message flood.
- **`getUpdates()` header skip** — looped `while (connected())` and treated a
  `readStringUntil` timeout (an empty String) as an ordinary header line, so a socket that
  stayed open but silent also spun forever.
- **`sendCommand()` blocking wait** — tight spin with no yield and a rollover-unsafe
  `millis() < timeout` compare.
- **`sendCommand()` non-blocking return** — discarded the write result and always returned
  `false`, so a retrying caller (the `Notifier`) delivered every message many times. It now
  returns whether the request was fully written to a live connection.
- **`sendCommand()` blocking reply read** — drained only the bytes that had already arrived,
  so on a slow link `available()` could go false mid-body and a delivered message was read as
  a failure. It now reads until the verdict appears or the stream stalls.
- **`sendCommand()` stale input** — `getUpdates()` and `sendMessage()` share one TCP
  connection, so leftovers from an earlier reply sat in the receive buffer and were read as
  the answer to the next request. A confirmed send therefore reported failure and the retry
  delivered a duplicate alarm. The socket is drained before a blocking send; the first run on
  hardware logged 400 stale bytes.
- **ArduinoJson 7** — several call sites still used `DynamicJsonDocument` /
  `StaticJsonDocument`, removed in v7. Added a `JSON_DOC_NAMED` compat macro.
- **`editMessage()`** — never sent `parse_mode`, so HTML rendered as literal tags when a
  menu edited itself in place.
- **`endQuery()`** — sent `callback_query_id` as a JSON number; the Bot API documents a
  string.

All blocking reads are now bounded by `TELEGRAM_READ_TIMEOUT` (5 s without *progress*).

## Architecture notes

**Nothing blocks.** `setup()` never waits for WiFi — the original spun in
`while (WiFi.status() != WL_CONNECTED)` with no timeout, so a router that was down at
power-up left the box silent and disarmed forever. WiFi is a state machine in `Net::loop()`.
The bot is started only once WiFi is up **and** NTP has synced, because TLS certificate
validation fails without a real clock. If WiFi stays down `WIFI_DEAD_REBOOT_MS`, or Telegram
is unreachable `BOT_DEAD_REBOOT_MS` (tracked via `Net::noteBotOk()`), the box reboots itself.

**Debounce is time-based, not a latch.** `DebouncedInput` requires the active level to hold
continuously for `*_DEBOUNCE_MS` before it counts, then ignores the input for `*_COOLDOWN_MS`.
The original `latched = latched || digitalRead(pin) == LOW` accepted a single sample and held
it forever, which made it a noise amplifier on long garage cabling. Motion also has a
`MOTION_WARMUP_MS` grace period after boot (PIR settling); smoke deliberately has none.

**The two inputs have opposite polarity** and each is pulled toward its *inactive* level, so
a cut wire reads as quiet rather than as an alarm:
- `PIN_ALARM` (17): `INPUT_PULLUP`, active **LOW**
- `PIN_MOTION` (18): `INPUT_PULLDOWN`, active **HIGH**

**Delivery is a persisted queue, and "sent" means confirmed.** `Notifier` removes an item
only once Telegram has answered `"ok":true` — writing the bytes is not delivery, because on a
weak link the TLS write succeeds while the request never arrives, and treating that as success
loses the alarm without a trace. Sends are therefore blocking, and `Notifier` holds off while
a `getUpdates` reply is outstanding (`isWaitingReply()`), since on the shared connection it
would otherwise read that reply as its own answer. It retries with exponential backoff (up to `NOTIFY_MAX_ATTEMPTS`), paces sends by
`NOTIFY_MIN_SEND_GAP_MS`, and stores the queue in NVS — so an alarm raised seconds before a
brownout is still delivered after the reboot. When full, it evicts the oldest non-critical
item first. Messages that sat in the queue are rendered with a "late" marker. The original
popped the recipient *before* sending and ignored the return value, losing the alert exactly
when the link was bad.

**NVS blobs are raw structs.** `Subscriber` and `QueueItem` are stored with
`putBytes(..., n * sizeof(T))`; changing their layout makes existing stored data fail the size
check and be discarded on the next boot. The Telegram `/` command list is registered only
when `MENU_VERSION` in `BotUI.cpp` differs from the stored one — bump it when commands change.

**A held smoke contact is an ongoing alarm, not a fault** (`AlarmOngoing`, re-sent every
`ALARM_REPEAT_MS`). A held *motion* input is treated as a wiring fault (`SensorStuck`, sent
once after `MOTION_STUCK_MS`), because sustained continuous motion is not a real-world signal.

**Reboot notices are owner-only and rate limited** (15 min for an unexpected reset, 6 h for a
routine power-up; last-sent time lives in NVS). Without the limit, a box in a reset loop
recreates the flood it is meant to warn about. `esp_reset_reason()` is reported in the notice
and in `/status` — that is the fastest way to tell a brownout from a firmware hang. A daily
heartbeat (`HEARTBEAT_HOUR`) makes silence from the bot meaningful.

**Mute never silences an alarm, nor `/test`.** `Events::bypassesMute()` decides, and it is
deliberately wider than `isCritical()`: a delivery check that is itself silently dropped makes
the bot look broken at the exact moment someone is trying to find out whether it works.

**`/mute 0` means off.** `Util::parseMuteHours` distinguishes "no argument" from "zero";
collapsing them turned the most natural way to disable mute into a command that re-armed it
for four hours. Anything unparseable falls back to the default window rather than to silence.
The mute screen text lives in `Events::muteScreen` so a test can render it — it used to append
"motion notifications do not arrive" unconditionally, so switching mute off still claimed it
was on.

**Diagnostics.** `/status` and the boot log carry the raw pin levels and a reset-reason
histogram kept in NVS; incoming commands, mute changes, events suppressed by mute and the
outgoing Telegram payload are logged to Serial. The payload dump is what established that no
request carries `disable_notification`, so a message arriving without a sound is a Telegram
client setting rather than something the firmware asks for.

### Known limitation

Sensors are sampled from `loop()`, and a Telegram send blocks for up to a few hundred ms, so
an input pulse shorter than one send could be missed. This is deliberate: latching edges in
an ISR would catch microsecond noise spikes, which is the failure mode the debounce exists to
prevent. It is safe here because both inputs are latching contacts that hold for seconds.
