#!/usr/bin/env bash
# Profile the engine with callgrind on this machine: tools/profile.sh <eco> <seconds> <preset>...
# Prints the functions that take the most time (inclusive and self). Proportions only: absolute times differ on the MPC.
set -euo pipefail
P="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST:-$P/../mpc-vst-plugins}"
O="${OBJ:-/tmp/odin_prof}"
mkdir -p "$O"
python3 "$MV/tools/gen_vst.py" "$P/vst/vst.json" --params-h >/dev/null
I="-I$P/src -I$P/src/odin/Source -I$P/src/odin/JuceLibraryCode -I$P/src/odin/libs/tuning-library -I$P/vst/build -I$MV/wrapper"
SRCS=$(cd "$P" && python3 -c "import json;print(' '.join(json.load(open('vst/vst.json'))['build']['sources']))")
for f in $SRCS; do echo "$P/$f"; done | xargs -P "$(nproc)" -I{} sh -c 'o="'"$O"'/$(basename "{}").o"; [ "$o" -nt "{}" ] || g++ -std=gnu++17 -O2 -g -w -c "{}" '"$I"' -o "$o"'
g++ -std=gnu++17 -O2 -g -w $I "$P/tools/profile.cpp" "$O"/*.o -lpthread -o "$O/profile"
cd "$P"
ODIN2MPC_USER_DIR=/nonexistent valgrind --tool=callgrind --callgrind-out-file="$O/cg.out" "$O/profile" "$@" 2>/dev/null
callgrind_annotate --inclusive=yes "$O/cg.out" 2>/dev/null | sed -n '1,60p'
