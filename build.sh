#!/usr/bin/env bash
# Build Odin 2 for MPC OS (an armhf VST2 instrument) and package it as an installer zip.
#
#   ./build.sh                  -> vst/build/odin2.so and its skin, then dist/Odin-2-<version>-mpc-armv7.zip
#   VERSION=1.4.1 ./build.sh    -> a different version number in the zip
#   SLOW=1 ./build.sh           -> the kit's own build (tools/build_port.sh: the compiler under ARM emulation)
#
# Needs Docker and a checkout of mpc-vst-plugins next to this folder (or MPC_VST=/path/to/mpc-vst-plugins). Run it
# from Ubuntu (WSL2 is fine), with Ubuntu's own docker.
#
# The fast build (default, 1.4.1): the plugin is compiled with Debian bullseye's ARM cross compiler (same glibc 2.31
# as the kit's image, so it loads on MPC OS 2.x and 3.x) at full speed on every core, and only what changed is
# recompiled (objects in vst/build/obj-arm). The skin is rebuilt only when the pages or their artwork changed.
# The first run downloads Debian bullseye and the compiler once (a few minutes); after that a full build takes a
# few minutes and a small change seconds.
set -euo pipefail
P="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST:-$P/../mpc-vst-plugins}"
VERSION="${VERSION:-1.4.4}"
if [ ! -x "$MV/tools/build_port.sh" ]; then
  echo "mpc-vst-plugins not found at $MV -- put it next to this folder or set MPC_VST=/path/to/it" >&2
  exit 1
fi
SKIN="$P/vst/build/skin/3dthc - VST - Odin 2"

if [ -n "${SLOW:-}" ]; then
  "$MV/tools/build_port.sh" "$P/vst/vst.json"
else
  U="$(id -u):$(id -g)"
  mkdir -p "$P/vst/build"
  # 1. skin, params.h, plugin-list entry (the kit's generator, in its browser-renderer image) -- only when its inputs
  #    changed; otherwise just params.h
  docker build -q -t mpc-vst-html-art "$MV/tools/html_art" >/dev/null
  STAMP="$( (cd "$P" && find vst/layout.conf vst/params.json vst/vst.json vst/skin_post.py vst/art -type f | sort | xargs sha1sum;
             cd "$MV" && sha1sum tools/gen_vst.py tools/shadow_skin.py tools/html_art.py tools/html_art/default.css \
               tools/skin_assets.py tools/skin_check.py wrapper/vst2_wrap.c) | sha1sum | cut -c1-40)"
  if [ -d "$SKIN" ] && [ -f "$P/vst/build/pluginlist-entry.xml" ] && [ "$(cat "$P/vst/build/.skin_stamp" 2>/dev/null)" = "$STAMP" ]; then
    echo "skin: unchanged, kept"
    docker run --rm -u "$U" -e HOME=/tmp -v "$P":/w -v "$MV":/mv:ro -w /w mpc-vst-html-art \
      python3 /mv/tools/gen_vst.py vst/vst.json --params-h
  else
    echo "skin: building (a few minutes)"
    docker run --rm -u "$U" -e HOME=/tmp -v "$P":/w -v "$MV":/mv:ro -w /w mpc-vst-html-art \
      python3 /mv/tools/gen_vst.py vst/vst.json
    echo "$STAMP" > "$P/vst/build/.skin_stamp"
  fi
  # 2. the plugin, cross-compiled on every core, only what changed
  docker build -q -t odin2-mpc-cross "$P/tools/cross" >/dev/null
  eval "$(python3 "$MV/tools/gen_vst.py" "$P/vst/vst.json" --shell)"
  docker run --rm -u "$U" -v "$P":/b -v "$MV":/mv:ro -w /b odin2-mpc-cross \
    make -f tools/arm.mk -j"$(nproc)" SOURCES="$SOURCES" CFLAGS_PORT="$CFLAGS" CFLAGS_ARM="$CFLAGS_ARM" \
      PORT=vst SO="$SO" MV=/mv
  md5sum "$P/vst/build/$SO"
fi

python3 "$MV/tools/release.py" \
  --so "$P/vst/build/odin2.so" \
  --skin "$SKIN" \
  --entry "$P/vst/build/pluginlist-entry.xml" \
  --version "$VERSION" \
  --about "Odin 2, TheWaveWarden's synthesizer, on MPC OS: Odin's own engine, 261 factory patches, voices sized to the MPC." \
  -o "$P/dist"
echo
echo "Installer zip: $(ls -t "$P"/dist/*.zip | head -n 1)"
