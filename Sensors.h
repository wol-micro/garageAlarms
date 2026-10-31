#pragma once
#include <Arduino.h>

// Time-based debounce plus a per-input cooldown.
//
// The original sketch used `latched = latched || digitalRead(pin) == LOW`, which accepts a
// single sample and holds it forever — a noise spike on a long garage cable was
// indistinguishable from a real event. Here an input must hold the active level
// continuously for `debounceMs` before it counts, and after firing it is ignored for
// `cooldownMs` so one event yields one notification.
class DebouncedInput {
public:
    // `warmupMs` is measured from boot: during that window the level is tracked normally but
    // an active edge is not reported. Detectors such as PIRs hold their output high while
    // they settle after power-up, which is not an event.
    void begin(uint8_t pin, bool activeHigh, uint32_t debounceMs, uint32_t cooldownMs,
               uint32_t warmupMs = 0);

    // Call every loop. Returns true exactly once per accepted trigger.
    bool update();

    bool     isActive() const   { return stable_; }
    bool     everTriggered() const { return everTriggered_; }
    uint32_t msSinceTrigger() const;

    // How long the input has been continuously active; 0 when inactive. A contact held
    // active for hours is far more likely to be a broken wire than a real event.
    uint32_t activeForMs() const;

    // Remaining cooldown, 0 if ready to fire.
    uint32_t cooldownLeftMs() const;

private:
    uint8_t  pin_          = 0;
    bool     activeHigh_   = false;
    uint32_t debounceMs_   = 50;
    uint32_t cooldownMs_   = 0;
    uint32_t warmupMs_     = 0;

    // Elapsed time is only credited to the debounce window when it was actually observed.
    // loop() blocks for seconds inside a Telegram or Pushover send, and the old code
    // compared the two samples either side of that stall and called the gap a continuous
    // hold — so a line that was noisy throughout, but happened to be active at both
    // sampling instants, passed a debounce it never satisfied. A single gap may contribute
    // at most this much.
    static const uint32_t SAMPLE_CREDIT_MAX_MS = 50;

    bool     lastRaw_      = false;
    bool     stable_       = false;
    bool     everTriggered_ = false;
    uint32_t lastSampleMs_ = 0;
    uint32_t observedMs_   = 0;
    uint32_t lastTriggerMs_ = 0;
    uint32_t activeSinceMs_ = 0;
};
