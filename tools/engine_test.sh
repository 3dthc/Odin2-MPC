#!/usr/bin/env bash
# Build and run tools/engine_test.cpp on this machine (host g++, C++17): the engine against Odin's own patch loader.
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST:-$P/../mpc-vst-plugins}"
O="${OBJ:-$(mktemp -d)}"
python3 "$MV/tools/gen_vst.py" "$P/vst/vst.json" --params-h >/dev/null
I="-I$P/src -I$P/src/odin/Source -I$P/src/odin/JuceLibraryCode -I$P/src/odin/libs/tuning-library -I$P/vst/build -I$MV/wrapper"
SRCS=$(cd "$P" && python3 -c "import json;print(' '.join(json.load(open('vst/vst.json'))['build']['sources']))")
# an object is rebuilt when its source or any header/include under src/ is newer
H="$(find "$P/src" "$P/vst/build" \( -name '*.h' -o -name '*.inc' \) -newer "$O/.built" 2>/dev/null | head -n 1 || true)"
[ -f "$O/.built" ] && [ -z "$H" ] || rm -f "$O"/*.o
for f in $SRCS; do echo "$P/$f"; done | xargs -P "$(nproc)" -I{} sh -c 'o="'"$O"'/$(basename "{}").o"; [ "$o" -nt "{}" ] || g++ -std=gnu++17 -O2 -w -c "{}" '"$I"' -o "$o"'
touch "$O/.built"
g++ -std=gnu++17 -O2 -w $I "$P/tools/engine_test.cpp" "$O"/*.o -lpthread -o "$O/engine_test"
cd "$P" && "$O/engine_test" "presets_src/Soundbanks/Factory Presets"
