#!/usr/bin/env bash
# Regenerate src/presets/ (the factory bank compiled into the plugin) from presets_src/. Needs a host g++
# (C++17). Only needed when the presets or the engine's state format change; the result is committed.
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"
O="$(mktemp -d)"
trap 'rm -rf "$O"' EXIT
I="-I$P/src -I$P/src/odin/Source -I$P/src/odin/JuceLibraryCode -I$P/src/odin/libs/tuning-library"
SRCS=$(cd "$P" && ls src/odin/Source/audio/*.cpp src/odin/Source/audio/*/*.cpp src/odin/Source/audio/Oscillators/Wavetables/Tables/*.cpp src/odin/Source/gui/*.cpp src/odin/Source/PluginProcessor.cpp)
for f in $SRCS; do echo "$P/$f"; done | xargs -P "$(nproc)" -I{} sh -c 'g++ -std=gnu++17 -O1 -w -c "{}" '"$I"' -o "'"$O"'/$(basename "{}").o"'
g++ -std=gnu++17 -O1 -w $I "$P/tools/make_bank.cpp" "$O"/*.o -o "$O/make_bank"
"$O/make_bank" "$P/presets_src/Soundbanks/Factory Presets" "$P/src/presets"
