#!/usr/bin/env bash
# Host-side test run. Production sources are staged into a scratch directory and the
# hardware headers are overlaid with stubs, so the logic compiles with a plain g++ and the
# clock and pin levels become test inputs.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
STAGE="${TMPDIR:-/tmp}/garageAlarms-test"

rm -rf "$STAGE"
mkdir -p "$STAGE"

# Logic under test (no .ino, no WiFi, no Telegram transport).
for f in Store.cpp Store.h Sensors.cpp Sensors.h Notifier.cpp Notifier.h \
         Events.cpp Events.h Util.cpp Util.h config.h AppState.h; do
    cp "$ROOT/$f" "$STAGE/"
done

# Stubs win over anything the firmware would normally pull from the core.
cp -r "$HERE/stubs/." "$STAGE/"
# Kept as .in because .gitignore excludes any file named secrets.h at any depth, which would
# otherwise drop this stub from the repository and break the suite on a fresh clone.
cp "$HERE/stubs/secrets.h.in" "$STAGE/secrets.h"
cp "$HERE/tests.cpp" "$STAGE/"

g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O0 -g \
    -I"$STAGE" \
    "$STAGE"/tests.cpp "$STAGE"/Store.cpp "$STAGE"/Sensors.cpp \
    "$STAGE"/Notifier.cpp "$STAGE"/Events.cpp "$STAGE"/Util.cpp "$STAGE"/Net.cpp \
    -o "$STAGE/tests"

"$STAGE/tests"
