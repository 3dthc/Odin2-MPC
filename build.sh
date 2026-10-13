#!/usr/bin/env bash
# Build Odin 2 for MPC OS (an armhf VST2 instrument) and package it as an installer zip.
#
#   ./build.sh                  -> vst/build/odin2.so and its skin, then dist/Odin-2-<version>-mpc-armv7.zip
#   VERSION=1.0.1 ./build.sh    -> a different version number in the zip
#
# Needs Docker (with QEMU for the arm32v7 image, the same setup as any mpc-vst-plugins port) and a checkout of
# mpc-vst-plugins next to this folder, or MPC_VST=/path/to/mpc-vst-plugins. Run it from Ubuntu (WSL2 is fine), with
# Ubuntu's own docker. The first build takes a while: the compiler runs under emulation and Odin's wavetable data
# is one very large source file.
set -euo pipefail
P="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST:-$P/../mpc-vst-plugins}"
VERSION="${VERSION:-1.3.0}"
if [ ! -x "$MV/tools/build_port.sh" ]; then
  echo "mpc-vst-plugins not found at $MV -- put it next to this folder or set MPC_VST=/path/to/it" >&2
  exit 1
fi

"$MV/tools/build_port.sh" "$P/vst/vst.json"

python3 "$MV/tools/release.py" \
  --so "$P/vst/build/odin2.so" \
  --skin "$P/vst/build/skin/3dthc - VST - Odin 2" \
  --entry "$P/vst/build/pluginlist-entry.xml" \
  --version "$VERSION" \
  --about "Odin 2, TheWaveWarden's synthesizer, on MPC OS: Odin's own engine, 261 factory patches, voices sized to the MPC." \
  -o "$P/dist"
echo
echo "Installer zip: $(ls -t "$P"/dist/*.zip | head -n 1)"
