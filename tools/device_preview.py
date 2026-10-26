#!/usr/bin/env python3
"""Dev tool: draw the built skin's pages the way MPC will show them for a given patch and theme, live text included
(the kit's studio.py preview leaves names and values out). Values come from the engine itself (tools/dump_values),
so knob positions, value text, mode panels (oscillator and filter types), lists and the look are the patch's.

    device_preview.py <skin folder> <dump_values binary> "<patch name>" <theme 0..3> <out_%d.png> [key=value ...]

MPC draws live text in Titillium Web; a JUCE font height is ascent + descent, which for Titillium Web is 1.521 em.
An approximation for checking layout, sizes and legibility offline, not a screenshot. GPL-3.0-or-later."""
import json
import os
import re
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, "..", "vst", "art", "fonts")
EM = 1000.0 / 1521.0


def main():
    skin, dumper, patch, theme, out = sys.argv[1:6]
    extra = sys.argv[6:]
    tui_path = None
    for root, _, files in os.walk(skin):
        if "TUI.json" in files:
            tui_path = os.path.join(root, "TUI.json")
    sdir = os.path.dirname(tui_path)
    t = json.load(open(tui_path))["pageData"]
    defs = {d["key"]: d["value"] for d in t["componentDefinitions"]["localComponentDefinitions"]}
    params = json.load(open(os.path.join(HERE, "..", "vst", "params.json")))["params"]
    keys = [p["key"] for p in params]
    res = subprocess.run([dumper, patch, theme] + extra, input="\n".join(keys) + "\n", capture_output=True, text=True,
                         cwd=os.path.join(HERE, ".."), check=True).stdout
    val = {}
    for ln in res.splitlines():
        k, v, disp, name, on = (ln.split("\t") + [""] * 5)[:5]
        val[k] = (v, disp, name, on)

    def norm(i):
        p = params[i] if i < len(params) else None
        if not p:
            return 0.0
        v = val.get(p["key"], ("0",))[0]
        try:
            f = float(v)
        except ValueError:
            return 0.0
        if p.get("options"):
            return f / max(1, len(p["options"]) - 1)
        lo, hi = p.get("min", 0), p.get("max", 1)
        return (f - lo) / (hi - lo) if hi > lo else 0.0

    def option(i):
        p = params[i] if i < len(params) else None
        if not p or not p.get("options"):
            return 0
        try:
            return int(round(float(val[p["key"]][0])))
        except (KeyError, ValueError):
            return 0

    def text(i, kind):
        if i >= len(params):
            return ""
        p = params[i]
        v, disp, name, on = val.get(p["key"], ("", "", "", ""))
        if kind == "Name":
            return name if p.get("dynamic_name") and name else p["name"]
        if p.get("display") == "string":
            return v
        if p.get("options"):
            return p["options"][option(i)]
        if disp:
            return disp
        return v

    state = {i: option(i) for i, p in enumerate(params) if p.get("options")}
    xywh = lambda b: [int(float(v)) for v in b["bounds"].split()]

    def handles(c):
        return [tuple(int(g) for g in m.groups()) for m in
                (re.match(r"IndexedEnabling/(\d+)/(\d+)/Parameter (\d+)$", h) for h in c["bounds"].get("additionalInvalidatingHandles", []))
                if m]

    fonts = {}

    def fnt(style, height):
        f = os.path.join(FONTS, "TitilliumWeb-%s.ttf" % ("Bold" if style == "Bold" else "SemiBold"))
        k = (f, height)
        if k not in fonts:
            fonts[k] = ImageFont.truetype(f, max(6, int(round(height * EM))))
        return fonts[k]

    def param_of(c, handle):
        for m in c.get("handle remapping", {}).get("map", []):
            if m["key"] == handle:
                g = re.match(r"Parameter (\d+)", m["value"])
                if g:
                    return int(g.group(1))
        return -1

    for n, tab in enumerate(t["tabs"]):
        page = tab["componentDefinition"] if "componentDefinition" in tab else defs[tab["componentName"]]
        im = Image.new("RGB", (1280, 628), (0, 0, 0))
        dr = ImageDraw.Draw(im)
        for c in page["componentsData"]:
            if not all(state.get(p, 0) == i for i, _, p in handles(c)):
                continue
            cd = c["componentData"]
            x, y, w, h = xywh(c["bounds"])
            if cd["type"] == "Image":
                img = Image.open(os.path.join(sdir, cd["data"]["image"])).convert("RGBA")
                im.paste(img, (x, y), img)
                continue
            for s in defs[cd["type"]]["componentsData"]:
                sd = s["componentData"]
                sx, sy, sw, sh = xywh(s["bounds"])
                hname = sd.get("data", {}).get("handleName", "Data")
                pi = param_of(c, hname)
                if sd["type"] == "Image":
                    img = Image.open(os.path.join(sdir, sd["data"]["image"])).convert("RGBA")
                    im.paste(img, (x + sx, y + sy), img)
                elif sd["type"] == "Knob":
                    st = Image.open(os.path.join(sdir, sd["data"]["filmStrip"])).convert("RGBA")
                    nf = int(sd["data"].get("numFrames") or 127)
                    n_img = nf + 1 if st.size[1] == (nf + 1) * st.size[0] else nf
                    fw, fh = st.size[0], st.size[1] // n_img
                    k = min(n_img - 1, max(0, round(norm(pi) * (n_img - 1))))
                    fr = st.crop((0, k * fh, fw, (k + 1) * fh))
                    im.paste(fr, (x + sx, y + sy), fr)
                elif sd["type"] == "Button":
                    p = params[pi] if 0 <= pi < len(params) else None
                    on = False
                    if p and p.get("display") == "string":
                        on = val.get(p["key"], ("", "", "", "0"))[3] == "1"
                    elif p and p.get("options"):
                        nb = sd["data"].get("numButtonsInGroup", 1)
                        bid = sd["data"].get("buttonId", 1)
                        on = (option(pi) == bid - 1) if nb > 1 else option(pi) > 0
                    img = sd["data"]["onImage" if on else "offImage"]
                    if img:
                        img = Image.open(os.path.join(sdir, img)).convert("RGBA")
                        im.paste(img, (x + sx, y + sy), img)
                elif sd["type"] == "Label":
                    ts = sd["data"]["textStyle"]
                    kind = sd["data"].get("type")
                    s_ = text(pi, "Name" if kind == "Name" else "Value")
                    if ts.get("case") == "Upper Case":
                        s_ = s_.upper()
                    col = ts["colour"]
                    rgb = tuple(int(col[i:i + 2], 16) for i in (2, 4, 6))
                    f = fnt(ts["font"].get("style"), float(ts["font"]["height"]))
                    just = ts.get("justification", "")
                    tw = dr.textlength(s_, font=f)
                    if tw > sw - 8 and s_.strip():
                        print("OVERFLOW %s: %r is %d px in a %d px box" % (tab["tabName"], s_, tw, sw), file=sys.stderr)
                    tx = x + sx + (sw - tw) / 2 if "horizontallyCentred" in just else x + sx + (sw - tw if "right" in just else 0)
                    dr.text((tx, y + sy + sh / 2), s_, font=f, fill=rgb, anchor="lm")
        im.save(out % n)
        print(out % n, tab["tabName"])


if __name__ == "__main__":
    main()
