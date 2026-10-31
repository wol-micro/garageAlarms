#pragma once
#include <Arduino.h>
// The journal is deliberately inert in tests: it is best-effort on the device too, and a
// failure to log must never change delivery behaviour.
namespace Journal {
inline void log(const char *, const char *, ...) {}
}
