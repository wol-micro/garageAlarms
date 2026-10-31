#include "Sensors.h"

void DebouncedInput::begin(uint8_t pin, bool activeHigh, uint32_t debounceMs, uint32_t cooldownMs,
                           uint32_t warmupMs)
{
    pin_        = pin;
    activeHigh_ = activeHigh;
    debounceMs_ = debounceMs;
    cooldownMs_ = cooldownMs;
    warmupMs_   = warmupMs;

    // Pull toward the inactive level so a disconnected wire reads "quiet", not "alarm".
    pinMode(pin_, activeHigh_ ? INPUT_PULLDOWN : INPUT_PULLUP);

    lastRaw_      = (digitalRead(pin_) == (activeHigh_ ? HIGH : LOW));
    lastSampleMs_ = millis();
    observedMs_   = 0;
    // stable_ deliberately starts inactive even if the pin is already active: an input that
    // is held at boot should still produce one alert once it has debounced, because we have
    // no idea how long it has been that way.
    stable_ = false;
}

bool DebouncedInput::update()
{
    const uint32_t now = millis();

    // Only time we were actually here to see counts toward the window. A long gap means
    // loop() was blocked in a send, not that the line was quiet: whatever the level did in
    // between was never sampled, so it cannot be used as evidence of a steady contact.
    uint32_t credit = now - lastSampleMs_;
    lastSampleMs_ = now;
    if (credit > SAMPLE_CREDIT_MAX_MS)
        credit = SAMPLE_CREDIT_MAX_MS;

    const bool raw = (digitalRead(pin_) == (activeHigh_ ? HIGH : LOW));

    if (raw != lastRaw_) {
        lastRaw_    = raw;
        observedMs_ = 0;
        return false;               // level just moved, wait for it to settle
    }

    observedMs_ += credit;
    if (observedMs_ < debounceMs_)
        return false;               // not observed at the same level long enough yet
    observedMs_ = debounceMs_;      // saturate; counting past the window has no meaning

    if (stable_ == raw)
        return false;               // nothing new

    stable_ = raw;
    if (!stable_) {
        activeSinceMs_ = 0;
        return false;               // released; only the active edge is an event
    }
    activeSinceMs_ = now;

    // Inside the warm-up window the edge is swallowed without arming the cooldown, so the
    // first genuine event right after warm-up is still reported.
    if (warmupMs_ && now < warmupMs_)
        return false;

    if (everTriggered_ && (now - lastTriggerMs_) < cooldownMs_)
        return false;               // real, but we already reported one just now

    everTriggered_ = true;
    lastTriggerMs_ = now;
    return true;
}

uint32_t DebouncedInput::msSinceTrigger() const
{
    if (!everTriggered_)
        return 0;
    return millis() - lastTriggerMs_;
}

uint32_t DebouncedInput::cooldownLeftMs() const
{
    if (!everTriggered_)
        return 0;
    const uint32_t elapsed = millis() - lastTriggerMs_;
    return elapsed >= cooldownMs_ ? 0 : cooldownMs_ - elapsed;
}

uint32_t DebouncedInput::activeForMs() const
{
    if (!stable_ || activeSinceMs_ == 0)
        return 0;
    return millis() - activeSinceMs_;
}
