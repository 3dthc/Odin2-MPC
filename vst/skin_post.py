#!/usr/bin/env python3
"""Touch-ups to the built skin that the layout can't express (vst.json "skin_post"; run by the kit's gen_vst.py with
the plugin's skin folder as its argument). Fails when what it fixes is missing, so a kit change can't skip it.

Q-Link column outline: the kit sizes a column's outline for its default knob text (cy + r + 40). Odin's pages draw
MPC's names and values larger (24 / 28 px, vst/make_skin.py), so the outline's bottom edge ran through the bottom
row's values; it is moved down by the extra text height."""
import json
import os
import sys

EXTRA = 34          # (r + 2 + 1.2 x 24 + 2 + 1.2 x 28 + 2) - (r + 40), rounded up (make_skin.knob_geom)
PLUGIN_H = 628


def main():
    skin = sys.argv[1]
    tui = None
    for root, _, files in os.walk(skin):
        if "TUI.json" in files:
            tui = os.path.join(root, "TUI.json")
    if not tui:
        sys.exit("skin_post: no TUI.json under %s" % skin)
    data = json.load(open(tui))
    n = 0

    def walk(o):
        nonlocal n
        if isinstance(o, dict):
            for k, v in o.items():
                if k == "qlinkBoundsData" and isinstance(v, list):
                    out = []
                    for r in v:
                        x, y, w, h = (int(p) for p in r.split())
                        if w and h:
                            h = min(PLUGIN_H - y, h + EXTRA)
                            n += 1
                        out.append("%d %d %d %d" % (x, y, w, h))
                    o[k] = out
                else:
                    walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(data)
    if not n:
        sys.exit("skin_post: no Q-Link column outlines found in %s" % tui)
    with open(tui, "w") as f:
        json.dump(data, f, indent=4)
    print("skin_post: %d Q-Link column outlines made taller by %d px" % (n, EXTRA))


if __name__ == "__main__":
    main()
