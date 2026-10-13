#!/usr/bin/env python3
"""Odin 2's MPC page: writes vst/layout.conf and every image it uses (vst/art/gen/), from Odin's own artwork.

The look is Odin 2's GUI: the grey chassis and its logo bar, the blue textured module panels with their titles in
Odin's runic lettering (taken from the panel images), the dark striped panel for an oscillator or filter that is
off, Odin's knob filmstrips, faders, power buttons and arrow buttons. The source images are in vst/art/odin/
(copied from Odin 2's assets/graphics, GPL-3.0-or-later like the rest of Odin) and Odin's UI font, Aldrich
(SIL Open Font License, vst/art/fonts/OFL.txt).

Geometry lives here only: the script writes the layout, so the art and the controls can't drift apart.
Coordinates below are in plugin pixels (1280 x 628); the layout adds the 86 px Force Shadow offset.
Needs Pillow. Run it after changing anything here; build.sh does.
"""
import json
import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "art", "odin")
GEN = os.path.join(HERE, "art", "gen")
FONT = os.path.join(HERE, "art", "fonts", "Aldrich-Regular.ttf")
W, H, YOFF = 1280, 628, 86

# Odin's colours (GlobalIncludes.h and its artwork)
ODIN_BLUE = (60, 155, 199)
TEXT_BLUE = (134, 203, 246)
TITLE_GREY = (154, 154, 154)
LABEL = (200, 205, 208)

PARAMS = {p["key"]: p for p in json.load(open(os.path.join(HERE, "params.json")))["params"]}
LINES = []          # layout lines
ASSETS = set()


def img(name):
    return Image.open(os.path.join(SRC, name)).convert("RGBA")


def font(size):
    return ImageFont.truetype(FONT, size)


def save(im, name):
    os.makedirs(GEN, exist_ok=True)
    im.save(os.path.join(GEN, name), optimize=True)
    ASSETS.add(name)
    return "art/gen/" + name


def Y(py):
    return py + YOFF


def line(s):
    LINES.append(s)


# ---------------------------------------------------------------------------------------------- textures and panels
def chassis(w, h):
    """Odin's grey chassis: the brushed texture of its logo bar, tiled with mirroring."""
    bg = img("background.png")
    tile = bg.crop((64, 2, 560, 54))     # left of the logo, under the top edge
    out = Image.new("RGBA", (w, h))
    tw, th = tile.size
    flip = tile.transpose(Image.FLIP_TOP_BOTTOM)
    for y in range(0, h, th):
        for x in range(0, w, tw):
            t = tile if (y // th) % 2 == 0 else flip
            if (x // tw) % 2:
                t = t.transpose(Image.FLIP_LEFT_RIGHT)
            out.paste(t, (x, y))
    return out


_TEXTURES = {}


CLEAN = {   # panels whose usual plain area holds a display or a decoration: where their plain texture is
    "Oscillator_Chiptune.png": (60, 228, 450, 272),
    "Oscillator_WaveDraw.png": (200, 48, 470, 86),
    "Oscillator_SpecDraw.png": (200, 48, 470, 86),
    "Oscillator_ChipDraw.png": (404, 92, 470, 260),
}


def clean_crop(png):
    """a plain stretch of an Odin panel's texture: right of its controls, above its bottom edge"""
    src = img(png)
    w, h = src.size
    return src.crop(CLEAN.get(png, (int(w * 0.52), int(h * 0.48), int(w * 0.9), int(h * 0.84))))


def blue_texture(w, h, png="FX_Delay.png"):
    """An Odin panel texture (by default the blue of its delay panel), mirrored to any size."""
    if png not in _TEXTURES:
        _TEXTURES[png] = img("FX_Delay.png").crop((230, 24, 460, 168)) if png == "FX_Delay.png" else clean_crop(png)
    t = _TEXTURES[png]
    tw, th = t.size
    out = Image.new("RGBA", (w, h))
    for y in range(0, h, th):
        for x in range(0, w, tw):
            q = t
            if (x // tw) % 2:
                q = q.transpose(Image.FLIP_LEFT_RIGHT)
            if (y // th) % 2:
                q = q.transpose(Image.FLIP_TOP_BOTTOM)
            out.paste(q, (x, y))
    return out


def rounded_mask(w, h, r):
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), r, fill=255)
    return m


def frame_edge(panel, dark=(10, 10, 10), light=(78, 104, 120)):
    w, h = panel.size
    d = ImageDraw.Draw(panel)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 10, outline=dark + (255,), width=4)
    d.rounded_rectangle((4, 4, w - 5, h - 5), 7, outline=light + (150,), width=1)
    return panel


def screws(panel):
    """the two little screws Odin's panels have in their bottom corners"""
    w, h = panel.size
    d = ImageDraw.Draw(panel)
    for x in (14, w - 15):
        d.ellipse((x - 5, h - 19, x + 5, h - 9), fill=(8, 10, 12, 255), outline=(60, 80, 92, 255))
        d.ellipse((x - 2, h - 16, x + 2, h - 12), fill=(30, 36, 40, 255))
    return panel


_KNOT = None


def watermark(panel, x, y, scale=1.0, alpha=0.55):
    """the Celtic knot that sits faintly behind Odin's panels (from its delay panel)"""
    global _KNOT
    if _KNOT is None:
        k = img("FX_Delay.png").crop((6, 6, 176, 168))   # above the panel's bottom edge
        # paint over the panel's screw (top-left) with the plain texture
        k.paste(blue_texture(26, 26), (4, 4), rounded_mask(26, 26, 13))
        base = blue_texture(k.size[0], k.size[1])
        # keep only what differs from the plain texture: the darker knot lines
        px, bp = k.load(), base.load()
        out = Image.new("RGBA", k.size, (0, 0, 0, 0))
        op = out.load()
        for j in range(k.size[1]):
            for i in range(k.size[0]):
                r, g, b, a = px[i, j]
                lum = (r + g + b) / 3
                d = max(0, 62 - lum)   # the texture averages ~64, the knot lines are darker
                op[i, j] = (18, 36, 48, int(min(255, d * 9)))
        _KNOT = out
    k = _KNOT
    if scale != 1.0:
        k = k.resize((int(k.size[0] * scale), int(k.size[1] * scale)), Image.LANCZOS)
    k = k.copy()
    k.putalpha(k.getchannel("A").point(lambda v: int(v * alpha)))
    panel.alpha_composite(k, (x, y))
    return panel


def blue_panel(w, h, title=None, knot=True, png="FX_Delay.png"):
    """an Odin module panel: the texture (and so the colour) of the given Odin panel image, title, knot, screws"""
    p = blue_texture(w, h, png)
    if knot:
        watermark(p, 4, max(4, h // 2 - 100), 1.15)
    if title is not None:
        if isinstance(title, Image.Image):
            p.alpha_composite(title, (14, 10))
        else:
            ImageDraw.Draw(p).text((16, 10), title, font=font(24), fill=TITLE_GREY + (255,))
    screws(p)
    frame_edge(p)
    p.putalpha(rounded_mask(w, h, 10))
    return p


def dark_panel(w, h, label):
    """Odin's empty slot: dark grey with diagonal stripes and a big grey name"""
    p = Image.new("RGBA", (w, h), (46, 46, 47, 255))
    d = ImageDraw.Draw(p)
    for k in range(-h, w + h, 56):
        d.polygon([(k, h), (k + 28, h), (k + 28 + h, 0), (k + h, 0)], fill=(52, 52, 53, 255))
    # the name where a module's title would be (its controls stay on the panel below)
    d.text((18, 10), label, font=font(30), fill=(92, 92, 94, 255))
    frame_edge(p, (14, 14, 14), (70, 70, 72))
    p.putalpha(rounded_mask(w, h, 10))
    return p


def odin_title(png, scale):
    """Odin's runic module title (flat grey letters over the panel's colour), cut out of its panel image: each pixel
    is taken as a blend of the grey and the panel colour, and kept only where it fits that blend"""
    src = img(png)
    t = clean_crop(png)
    n = t.size[0] * t.size[1]
    data = list(t.convert("RGB").tobytes())
    bg = [sum(data[k::3]) / n for k in range(3)]
    gd = [TITLE_GREY[k] - bg[k] for k in range(3)]
    gg = sum(v * v for v in gd) or 1.0
    reg = src.crop((8, 6, 330, 52))
    px = reg.load()
    out = Image.new("RGBA", reg.size, (0, 0, 0, 0))
    op = out.load()
    for j in range(reg.size[1]):
        for i in range(reg.size[0]):
            p = px[i, j]
            d = [p[k] - bg[k] for k in range(3)]
            a = sum(d[k] * gd[k] for k in range(3)) / gg
            res = sum((d[k] - a * gd[k]) ** 2 for k in range(3)) ** 0.5
            if a < 0.2 or res > 28:
                continue
            op[i, j] = TITLE_GREY + (int(min(1.0, a) * 255),)
    bbox = out.getbbox()
    if bbox:
        out = out.crop((0, 0, bbox[2] + 2, reg.size[1]))
    return out.resize((max(1, int(out.size[0] * scale)), max(1, int(out.size[1] * scale))), Image.LANCZOS)


def caption(d, x, y, text, size=15, fill=LABEL, anchor="mm", spacing=1):
    f = font(size)
    if spacing:
        text = " ".join(text) if spacing > 1 else text
    d.text((x, y), text, font=f, fill=fill + (255,), anchor=anchor)


# ---------------------------------------------------------------------------------------------- control images
def knob_strip(src, r, name):
    """Odin's knob filmstrip, each frame centred in a square of the widget's own 2r+8 box"""
    s = 2 * r + 8
    strip = img(src)
    fw = strip.size[0]
    fh = strip.size[1] // 129
    scale = (2 * r) / fw * 1.05
    out = Image.new("RGBA", (s, s * 129), (0, 0, 0, 0))
    for i in range(129):
        f = strip.crop((0, i * fh, fw, (i + 1) * fh))
        f = f.resize((max(1, round(fw * scale)), max(1, round(fh * scale))), Image.LANCZOS)
        ox = (s - f.size[0]) // 2
        oy = (s - round(fw * scale)) // 2          # the knob's round body sits at the top of Odin's frame
        tile = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        tile.alpha_composite(f, (ox, oy)) if oy >= 0 else tile.paste(f, (ox, oy), f)
        out.paste(tile, (0, i * s))
    return save(out, name)


def text_button(label, on, w=96, h=32, size=15, color=None):
    b = img("bttn_12x4_off.png").resize((w, h), Image.LANCZOS)
    d = ImageDraw.Draw(b)
    c = color or ((TEXT_BLUE if on else (150, 154, 156)))
    if on and not color:
        glow = Image.new("RGBA", b.size, (0, 0, 0, 0))
        ImageDraw.Draw(glow).text((w / 2, h / 2), label, font=font(size), fill=ODIN_BLUE + (255,), anchor="mm")
        b.alpha_composite(glow.filter(ImageFilter.GaussianBlur(3)))
    d.text((w / 2, h / 2 + 1), label, font=font(size), fill=c + (255,), anchor="mm")
    return b


def square_button(label, on, s=40, size=16):
    b = img("bttn_5x5_arrow_L_off.png")
    # wipe the arrow: paint the face with its own centre colour
    face = b.getpixel((10, 20))
    ImageDraw.Draw(b).rectangle((12, 12, 28, 28), fill=face)
    b = b.resize((s, s), Image.LANCZOS)
    d = ImageDraw.Draw(b)
    if on:
        glow = Image.new("RGBA", b.size, (0, 0, 0, 0))
        ImageDraw.Draw(glow).text((s / 2, s / 2), label, font=font(size), fill=ODIN_BLUE + (255,), anchor="mm")
        b.alpha_composite(glow.filter(ImageFilter.GaussianBlur(3)))
    d.text((s / 2, s / 2 + 1), label, font=font(size), fill=(TEXT_BLUE if on else (140, 144, 146)) + (255,), anchor="mm")
    return b


def cpu_meter_strip(w, h):
    """21 frames, 0..100 % in 5 % steps: a segmented bar, green to amber to red, with the number"""
    frames = 21
    out = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for i in range(frames):
        pct = i * 5
        f = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(f)
        d.rounded_rectangle((0, 0, w - 1, h - 1), 5, fill=(10, 40, 50, 255), outline=(4, 14, 18, 255), width=2)
        segs = 10
        bx, by, bw, bh = 54, 8, w - 62, h - 16
        sw = bw / segs
        lit = round(pct / 10.0)
        for s in range(segs):
            x0 = bx + s * sw + 1
            col = (64, 190, 120) if s < 6 else (232, 178, 58) if s < 8 else (226, 70, 52)
            if s >= lit:
                col = tuple(int(c * 0.22) for c in col)
            d.rectangle((x0, by, x0 + sw - 3, by + bh), fill=col + (255,))
        tcol = (226, 70, 52) if pct >= 80 else TEXT_BLUE
        d.text((28, h / 2 + 1), "%d" % pct, font=font(20), fill=tcol + (255,), anchor="mm")
        out.paste(f, (0, i * h))
    return save(out, "cpu_meter.png")


def arc_knob_strip(src, body, r, name, bipolar=False):
    """Odin's knob filmstrip at (or below) its native size, so it stays sharp, inside a value ring in Odin blue that
    can be read from a distance: a 270-degree track, lit from the minimum (or from the top, for bipolar controls)
    to the knob's position. Frames are the widget's 2r+8 box; 129 of them, as Odin's own strips."""
    s = 2 * r + 8
    strip = img(src)
    fw = strip.size[0]
    fh = strip.size[1] // 129
    scale = body / float(fw)
    SS = 4                                  # draw the ring 4x and scale down: smooth edges
    ring = r - 1.5
    width = 4 if r >= 34 else 3
    out = Image.new("RGBA", (s, s * 129), (0, 0, 0, 0))
    for i in range(129):
        t = i / 128.0
        big = Image.new("RGBA", (s * SS, s * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        box = [(s / 2 - ring) * SS, (s / 2 - ring) * SS, (s / 2 + ring) * SS, (s / 2 + ring) * SS]
        d.arc(box, 135, 405, fill=(14, 18, 20, 255), width=(width + 2) * SS)
        d.arc(box, 135, 405, fill=(38, 46, 52, 255), width=width * SS)
        a0, a1 = (270, 135 + 270 * t) if bipolar else (135, 135 + 270 * t)
        if abs(a1 - a0) > 0.5:
            d.arc(box, min(a0, a1), max(a0, a1), fill=(86, 190, 240, 255), width=width * SS)
        tile = big.resize((s, s), Image.LANCZOS)
        f = strip.crop((0, i * fh, fw, (i + 1) * fh))
        if scale != 1.0:
            f = f.resize((max(1, round(fw * scale)), max(1, round(fh * scale))), Image.LANCZOS)
        ox = (s - f.size[0]) // 2
        oy = (s - round(fw * scale)) // 2   # the knob's round body sits at the top of Odin's frame
        layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        layer.paste(f, (ox, oy), f)
        tile.alpha_composite(layer)
        out.paste(tile, (0, i * s))
    return save(out, name)


def fader_track(h):
    t = Image.new("RGBA", (40, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(t)
    d.rounded_rectangle((16, 4, 24, h - 5), 4, fill=(8, 9, 10, 255), outline=(70, 72, 74, 255))
    for k in range(9):   # scale ticks
        y = 10 + k * (h - 20) / 8
        d.line((4, y, 11, y), fill=(120, 124, 126, 255))
        d.line((29, y, 36, y), fill=(120, 124, 126, 255))
    return t




# ---------------------------------------------------------------------------------------------- the pages
# Built for a standing player on a 4-knob MPC (Live II): every page is four columns of four big knobs, and each column
# is one Q-Link column (one press of the Q-Link button, outlined on screen), read left to right, top to bottom.
# Set-and-forget choices (types, routing, play mode) sit in the right-hand panel, which MPC's Q-Link panel covers
# while a Q-Link is turned anyway. Live names and values are drawn large by MPC; captions are baked in Aldrich.
OSC_PANEL = {"Off": None, "Analog": "Oscillator_Analog.png", "Wavetable": "Oscillator_Wavetable.png",
             "Multi": "Oscillator_Multi.png", "Vector": "Oscillator_Vector.png", "Chiptune": "Oscillator_Chiptune.png",
             "FM": "Oscillator_FM.png", "PM": "Oscillator_PM.png", "Noise": "Oscillator_Noise.png",
             "WaveDraw": "Oscillator_WaveDraw.png", "ChipDraw": "Oscillator_ChipDraw.png",
             "SpecDraw": "Oscillator_SpecDraw.png"}
FIL_PANEL = {"Bypass": None, "LP 24": "Filter_Lowpass24.png", "LP 12": "Filter_Lowpass12.png",
             "BP 24": "Filter_Bandpass24.png", "BP 12": "Filter_Bandpass12.png", "HP 24": "Filter_Highpass24.png",
             "HP 12": "Filter_Highpass12.png", "SEM 12": "Filter_SEM12.png", "Diode": "Filter_DiodeLadder.png",
             "Korg LP": "Filter_KRG35LP.png", "Korg HP": "Filter_KRG35HP.png", "Comb": "Filter_Combfilter.png",
             "Formant": "Filter_Formant.png", "RingMod": "Filter_Ringmod.png"}

HEAD = 78                        # top bar height
TOP, PH = 84, 538                # module panels: top, height
SX = [8, 259, 510, 761]          # the four Q-Link columns
SW = 245
RX, RW = 1016, 256               # the right-hand panel
ROW1, ROW2, ROW3 = 140, 296, 442  # knob centres, from a panel's top: two big rows, one small row
SPREAD = (150, 352)               # the two rows on pages with nothing in a third row (ENV, FX)
BAND = 40                         # an oscillator/filter column's title band, above the type's own panel picture
ROWS = [ROW1, ROW2]
BIPOLAR = {"fil1_env", "fil2_env", "fil3_env", "amp_pan", "osc1_posmod", "osc2_posmod", "osc3_posmod",
           "fil1_sem", "fil2_sem", "fil3_sem", "tune"}
KNOB = {   # size: (radius, source strip, body px, name px, value px, touch width)
    "big": (40, "knob_8x8_a_strip.png", 64, 22, 26, 118),
    "mid": (32, "knob_6x6_a_strip.png", 48, 20, 24, 112),
    "small": (26, "knob_4x4_a_strip.png", 48, 18, 22, 112),
    "head": (24, "knob_4x4_a_strip.png", 40, 0, 38, 132),
}
_STRIPS = {}


def strip_for(size, bipolar):
    k = (size, bipolar)
    if k not in _STRIPS:
        r, src, body = KNOB[size][:3]
        _STRIPS[k] = arc_knob_strip(src, body, r, "knob_%s%s.png" % (size, "_bip" if bipolar else ""), bipolar)
    return _STRIPS[k]


def W_(when):
    """'osc1_type:analog' -> 'osc1_type:1' (the option's index: option names can hold spaces)"""
    if not when:
        return when
    key, opt = when.split(":")
    names = [o.replace(" ", "").lower() for o in PARAMS[key]["options"]]
    return "%s:%d" % (key, names.index(opt.replace(" ", "").lower()))


def picture(x, y, w, h, key, files):
    line('picture x=%d y=%d w=%d h=%d key=%s files="%s"' % (x, Y(y), w, h, key, ",".join(files)))


def knob(cx, cy, key, label, size="big", when=None):
    r, _, _, ns, vs, bw = KNOB[size]
    wh = " when=%s" % W_(when) if when else ""
    line('knob cx=%d cy=%d r=%d label="%s" key=%s strip=%s ns=%d vs=%d bw=%d%s' % (
        cx, Y(cy), r, label, key, strip_for(size, key in BIPOLAR), ns, vs, bw, wh))


def popup(cx, cy, w, h, key, when=None, cols=None):
    wh = " when=%s" % W_(when) if when else ""
    c = " cols=%d" % cols if cols else ""
    line('popup cx=%d cy=%d w=%d h=%d label="" key=%s%s%s' % (cx, Y(cy), w, h, key, c, wh))


def toggle_img(cx, cy, key, pair, w, h, when=None):
    wh = " when=%s" % W_(when) if when else ""
    line('toggle cx=%d cy=%d label="" key=%s img=%s img_on=%s ns=0 w=%d h=%d%s' % (cx, Y(cy), key, pair[0], pair[1], w, h, wh))


def text(d, x, y, s, size=18, fill=LABEL, anchor="mm"):
    d.text((x, y), s, font=font(size), fill=fill + (255,), anchor=anchor)


def caption_line(x, y, s, size=1.8, color="c8cdd0", when=None):
    """free text drawn by the skin builder over the panel pictures (MPC never redraws it)"""
    wh = " when=%s" % W_(when) if when else ""
    line('text cx=%d cy=%d label="%s" size=%g color=%s font="Aldrich" fontfile=art/fonts/Aldrich-Regular.ttf%s' % (
        x, Y(y), s, size, color, wh))


def qcol(sx):
    """the two knob columns of a Q-Link column"""
    return sx + 63, sx + 182


def plain(bg, x, y, w, h, title):
    bg.alpha_composite(blue_panel(w, h, None, knot=True), (x, y))
    text(ImageDraw.Draw(bg), x + 16, y + 26, title, 24, TITLE_GREY, "lm")


def four(sx, keys, labels, size="big"):
    """a 2 x 2 knob block in a Q-Link column ("-" leaves a cell empty); rows from ROWS (set per page)"""
    a, b = qcol(sx)
    r1, r2 = ROWS
    for (k, lab), (cx, cy) in zip(zip(keys, labels), [(a, TOP + r1), (b, TOP + r1), (a, TOP + r2), (b, TOP + r2)]):
        if k != "-":
            knob(cx, cy, k, lab, size)


def typed_strip(bg, sx, key, kinds, name):
    """an oscillator or filter column: a title band in the colour of Odin's own panel for the type (dark when it is
    off), the type picker under it. Only the band changes with the type, so the skin stays small in memory."""
    plain(bg, sx, TOP, SW, PH, "")
    picture(sx, TOP, SW, BAND + 8, key, band_pictures(name, kinds))
    popup(sx + SW // 2, TOP + BAND + 30, SW - 24, 40, key, cols=2)


def header(bg):
    """top bar (on every page). What you watch while turning Q-Links (patch, voices, CPU) stays left of x ~1025,
    where MPC's Q-Link panel slides in."""
    src = img("background.png")
    bar = src.crop((560, 0, 1040, 60)).resize((232, 29), Image.LANCZOS)
    bg.alpha_composite(bar, (8, 26))
    d = ImageDraw.Draw(bg)
    d.line((0, HEAD - 2, W, HEAD - 2), fill=(20, 20, 20, 255), width=2)
    d.line((0, HEAD, W, HEAD), fill=(92, 92, 92, 255), width=1)
    text(d, 300, 12, "ECO", 17, (160, 164, 166))
    popup(300, 46, 104, 44, "eco")
    picture(357, 35, 22, 22, "eco_active", [LED_OFF, LED_ON])
    line('button cx=416 cy=%d label="" key=preset_prev img=%s img_on=%s w=52 h=52' % (Y(42), ARROW_L, ARROW_L_ON))
    line('readout cx=601 cy=%d w=304 h=54 label="" key=preset_name tsize=28 tcolor=86cbf6' % Y(42))
    line('button cx=786 cy=%d label="" key=preset_next img=%s img_on=%s w=52 h=52' % (Y(42), ARROW_R, ARROW_R_ON))
    text(d, 878, 11, "VOICES", 17, (160, 164, 166))
    # 1.3: how many voices the budget lets sound now (read-only; the VOICES knob is gone, Odin picks the voices)
    line('readout cx=878 cy=%d w=110 h=50 label="" key=voices tsize=30 tcolor=86cbf6' % Y(46))
    text(d, 1002, 12, "CPU", 17, (160, 164, 166))
    line('meter cx=1002 cy=%d w=130 h=40 key=cpu strip=%s frames=21' % (Y(46), CPU_STRIP))
    line('button cx=1190 cy=%d label="" key=panic img=%s img_on=%s w=150 h=54' % (Y(42), STOP_OFF, STOP_ON))


def page(name):
    bg = chassis(W, H)
    line("[tab %s]" % name)
    line("art file=art/gen/bg_%s.png x=0 y=%d w=%d h=%d" % (name.lower(), YOFF, W, H))
    header(bg)
    return bg


def right_panel(bg, title):
    plain(bg, RX, TOP, RW, PH, title)
    return ImageDraw.Draw(bg)


def tab_main():
    bg = page("MAIN")
    plain(bg, SX[0], TOP, SW, PH, "VOICE")
    four(SX[0], ["master", "glide", "uni_detune", "uni_width"], ["MASTER", "GLIDE", "DETUNE", "WIDTH"])
    typed_strip(bg, SX[1], "fil1_type", FIL_PANEL, "FILTER 1")
    four(SX[1], ["fil1_freq", "fil1_res", "fil1_env", "fil1_kbd"], ["CUTOFF", "RESO", "ENV", "KEY"])
    plain(bg, SX[2], TOP, SW, PH, "AMP ENVELOPE")
    four(SX[2], ["env1_a", "env1_d", "env1_s", "env1_r"], ["ATTACK", "DECAY", "SUSTAIN", "RELEASE"])
    plain(bg, SX[3], TOP, SW, PH, "SPACE")
    four(SX[3], ["delay_wet", "delay_fb", "reverb_mix", "reverb_size"], ["DELAY", "FEEDBACK", "REVERB", "HALL"])
    a, b = qcol(SX[3])
    toggle_img(a, TOP + ROW3 + 6, "delay_on", DELAY_TG, 112, 46)
    toggle_img(b, TOP + ROW3 + 6, "reverb_on", REVERB_TG, 112, 46)
    d = right_panel(bg, "PLAY")
    text(d, RX + RW // 2, TOP + 70, "MODE", 17, (150, 154, 156))
    line('enum_v cx=%d cy=%d label="" key=playmode sw=200 sh=44' % (RX + RW // 2, Y(TOP + 160)))
    text(d, RX + RW // 2, TOP + 262, "UNISON", 17, (150, 154, 156))
    popup(RX + RW // 2, TOP + 300, 200, 44, "unison")
    knob(RX + 66, TOP + 384, "amp_vel", "VELOCITY", "mid")
    knob(RX + 190, TOP + 384, "bend", "BEND", "mid")
    toggle_img(RX + RW // 2, TOP + 506, "arp_on", ARP_TG, 200, 48)
    line('qlinks "MAIN" = master,glide,uni_detune,uni_width,fil1_freq,fil1_res,fil1_env,fil1_kbd,'
         'env1_a,env1_d,env1_s,env1_r,delay_wet,delay_fb,reverb_mix,reverb_size')
    save(bg, "bg_main.png")


def tab_osc():
    bg = page("OSC")
    for n, sx in zip((1, 2, 3), SX):
        o = "osc%d" % n
        typed_strip(bg, sx, o + "_type", OSC_PANEL, "OSC %d" % n)
        four(sx, [o + "_vol", o + "_shape", o + "_oct", o + "_semi"], ["VOLUME", "SHAPE", "OCTAVE", "SEMI"])
        a, b = qcol(sx)
        cy = TOP + ROW3 + 6
        line('enum_h cx=%d cy=%d sw=108 rows=2 label="" key=%s_wave when=%s' % (sx + SW // 2, Y(cy), o, W_(o + "_type:analog")))
        for mode in ("wavetable", "multi"):
            popup(sx + SW // 2, cy, SW - 24, 44, o + "_table", when="%s_type:%s" % (o, mode), cols=3)
        knob(a, cy - 6, o + "_vecy", "VECTOR Y", "small", when=o + "_type:vector")
        popup(a + 18, cy, 150, 44, o + "_chip", when=o + "_type:chiptune")
        toggle_img(b + 30, cy, o + "_chipnoise", NOISE_TG, 70, 44, when=o + "_type:chiptune")
        for mode in ("fm", "pm"):
            knob(a, cy - 6, o + "_cratio", "CARRIER", "small", when="%s_type:%s" % (o, mode))
            knob(b, cy - 6, o + "_mratio", "MODULATOR", "small", when="%s_type:%s" % (o, mode))
        knob(a, cy - 6, o + "_hp", "HIGHPASS", "small", when=o + "_type:noise")
        for mode in ("wavedraw", "chipdraw", "specdraw"):
            caption_line(sx + SW // 2, cy, "DRAWN IN THE PATCH", 1.4, "86cbf6", when="%s_type:%s" % (o, mode))
    plain(bg, SX[3], TOP, SW, PH, "FINE TUNE")
    # 1.3: MASTER TUNE (A4 in Hz, the plugin's, kept across patches) in place of GLIDE, which MAIN has
    four(SX[3], ["osc1_fine", "osc2_fine", "osc3_fine", "tune"], ["OSC 1", "OSC 2", "OSC 3", "TUNE A4"])
    d = right_panel(bg, "UNISON")
    popup(RX + RW // 2, TOP + 96, 200, 44, "unison")
    knob(RX + 66, TOP + 230, "uni_detune", "DETUNE", "mid")
    knob(RX + 190, TOP + 230, "uni_width", "WIDTH", "mid")
    text(d, RX + RW // 2, TOP + 360, "SHAPE turns what", 17, (160, 164, 166))
    text(d, RX + RW // 2, TOP + 382, "matters for the type:", 17, (160, 164, 166))
    text(d, RX + RW // 2, TOP + 410, "PW, POSITION, FM...", 15, TEXT_BLUE)
    line('qlinks "OSC" = osc1_vol,osc1_shape,osc1_oct,osc1_semi,osc2_vol,osc2_shape,osc2_oct,osc2_semi,'
         'osc3_vol,osc3_shape,osc3_oct,osc3_semi,osc1_fine,osc2_fine,osc3_fine,tune')
    save(bg, "bg_osc.png")


def tab_filter():
    bg = page("FILTER")
    for n, sx in zip((1, 2, 3), SX):
        f = "fil%d" % n
        typed_strip(bg, sx, f + "_type", FIL_PANEL, "FILTER %d" % n)
        four(sx, [f + "_freq", f + "_res", f + "_env", f + "_kbd"], ["CUTOFF", "RESO", "ENV", "KEY"])
        a, b = qcol(sx)
        cy = TOP + ROW3
        knob(a, cy, f + "_vel", "VELOCITY", "small")
        knob(b, cy, f + "_sem", "SHAPE", "small", when=f + "_type:sem 12")
        knob(b, cy, f + "_formant", "VOWEL", "small", when=f + "_type:formant")
        knob(b, cy, f + "_ring", "AMOUNT", "small", when=f + "_type:ringmod")
    plain(bg, SX[3], TOP, SW, PH, "DRIVE")
    four(SX[3], ["fil1_sat", "fil2_sat", "dist_boost", "dist_mix"], ["SAT 1", "SAT 2", "DISTORT", "DIST MIX"])
    d = right_panel(bg, "ROUTING")
    y0 = TOP + 76
    for row, (lab, srcs) in enumerate([("INTO FILTER 1", ["osc1", "osc2", "osc3"]), ("INTO FILTER 2", ["osc1", "osc2", "osc3", "fil1"])]):
        y = y0 + row * 106
        text(d, RX + 16, y, lab, 17, (160, 164, 166), "lm")
        for i, sname in enumerate(srcs):
            lab2 = {"osc1": "1", "osc2": "2", "osc3": "3", "fil1": "F1"}[sname]
            toggle_img(RX + 40 + i * 58, y + 48, "fil%d_%s" % (row + 1, sname), ROUTE[lab2], 52, 52)
    y = y0 + 212
    text(d, RX + 16, y, "TO AMP", 17, (160, 164, 166), "lm")
    toggle_img(RX + 40, y + 48, "fil1_amp", ROUTE["F1"], 52, 52)
    toggle_img(RX + 98, y + 48, "fil2_amp", ROUTE["F2"], 52, 52)
    y = y0 + 330
    text(d, RX + 16, y, "DISTORTION", 17, (160, 164, 166), "lm")
    toggle_img(RX + 66, y + 42, "dist_on", DIST_TG, 104, 44)
    line('enum_h cx=%d cy=%d label="" key=dist_algo sw=78 sh=42' % (RX + RW // 2, Y(y + 100)))
    line('qlinks "FILTER" = fil1_freq,fil1_res,fil1_env,fil1_kbd,fil2_freq,fil2_res,fil2_env,fil2_kbd,'
         'fil3_freq,fil3_res,fil3_env,fil3_kbd,fil1_sat,fil2_sat,dist_boost,dist_mix')
    save(bg, "bg_filter.png")


def tab_env():
    global ROWS
    ROWS = list(SPREAD)
    bg = page("ENV")
    for n, sx, title in zip((1, 2, 3), SX, ("AMP ENV", "FILTER ENV", "MOD ENV")):
        plain(bg, sx, TOP, SW, PH, title)
        e = "env%d" % n
        four(sx, [e + "_a", e + "_d", e + "_s", e + "_r"], ["ATTACK", "DECAY", "SUSTAIN", "RELEASE"])
    plain(bg, SX[3], TOP, SW, PH, "LFO RATE")
    four(SX[3], ["lfo1_rate", "lfo2_rate", "lfo3_rate", "lfo4_rate"], ["LFO 1", "LFO 2", "LFO 3", "GLOBAL"])
    d = right_panel(bg, "LFO WAVE")
    for i, (k, lab) in enumerate([("lfo1_wave", "LFO 1"), ("lfo2_wave", "LFO 2"), ("lfo3_wave", "LFO 3"), ("lfo4_wave", "GLOBAL")]):
        y = TOP + 80 + i * 110
        text(d, RX + 16, y, lab, 17, (160, 164, 166), "lm")
        popup(RX + RW // 2, y + 40, 220, 44, k, cols=3)
    line('qlinks "ENV" = env1_a,env1_d,env1_s,env1_r,env2_a,env2_d,env2_s,env2_r,'
         'env3_a,env3_d,env3_s,env3_r,lfo1_rate,lfo2_rate,lfo3_rate,lfo4_rate')
    save(bg, "bg_env.png")


def fx_strip(bg, sx, title, on_key, keys, labels, tg):
    plain(bg, sx, TOP, SW, PH, title)
    toggle_img(sx + SW - 60, TOP + 28, on_key, tg, 96, 40)
    four(sx, keys, labels)


def tab_fx():
    global ROWS
    ROWS = list(SPREAD)
    bg = page("FX")
    fx_strip(bg, SX[0], "DELAY", "delay_on", ["delay_time", "delay_fb", "delay_wet", "-"], ["TIME", "FEEDBACK", "WET", ""], ON_TG)
    a, b = qcol(SX[0])
    toggle_img(b, TOP + ROWS[1] - 10, "delay_pp", PP_TG, 112, 46)
    fx_strip(bg, SX[1], "REVERB", "reverb_on", ["reverb_size", "reverb_damp", "reverb_mix", "-"], ["HALL", "DAMP", "MIX", ""], ON_TG)
    fx_strip(bg, SX[2], "CHORUS", "chorus_on", ["chorus_rate", "chorus_amt", "chorus_mix", "-"], ["RATE", "AMOUNT", "MIX", ""], ON_TG)
    fx_strip(bg, SX[3], "PHASER", "phaser_on", ["phaser_rate", "phaser_amt", "phaser_mix", "-"], ["RATE", "AMOUNT", "MIX", ""], ON_TG)
    d = right_panel(bg, "FLANGER")
    toggle_img(RX + RW - 60, TOP + 28, "flanger_on", ON_TG, 96, 40)
    knob(RX + 66, TOP + 150, "flanger_rate", "RATE", "mid")
    knob(RX + 190, TOP + 150, "flanger_amt", "AMOUNT", "mid")
    knob(RX + 66, TOP + 352, "flanger_mix", "MIX", "mid")
    line('qlinks "FX" = delay_time,delay_fb,delay_wet,-,reverb_size,reverb_damp,reverb_mix,-,'
         'chorus_rate,chorus_amt,chorus_mix,-,phaser_rate,phaser_amt,phaser_mix,-')
    save(bg, "bg_fx.png")


def band_pictures(title, kinds):
    """a column's title band, one per type: the texture of Odin's panel for that type, or dark grey when off"""
    files = []
    for name, src in kinds.items():
        h = BAND + 8
        if src is None:
            b = Image.new("RGBA", (SW, h), (40, 40, 41, 255))
        else:
            b = blue_texture(SW, h, src)
        d = ImageDraw.Draw(b)
        d.text((16, h // 2 + 1), title, font=font(24), fill=(TITLE_GREY if src else (110, 110, 112)) + (255,), anchor="lm")
        d.line((4, h - 2, SW - 5, h - 2), fill=(10, 10, 10, 255), width=3)
        frame_edge(b)
        m = Image.new("L", (SW, h), 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, SW - 1, h + 12), 10, fill=255)   # round top corners only
        b.putalpha(m)
        files.append(save(b, "band_%s_%s.png" % (title.replace(" ", "").lower(), name.replace(" ", "").lower())))
    return files


def pair(name, label, w, h, size=18, on_color=None):
    return (save(text_button(label, False, w, h, size), "%s.png" % name),
            save(text_button(label, True, w, h, size, on_color), "%s_on.png" % name))


def main():
    global ARROW_L, ARROW_L_ON, ARROW_R, ARROW_R_ON, STOP_OFF, STOP_ON, CPU_STRIP, LED_OFF, LED_ON
    global NOISE_TG, ARP_TG, DIST_TG, ON_TG, PP_TG, DELAY_TG, REVERB_TG, ROUTE, OSC_FILES, FIL_FILES
    random.seed(1)
    if os.path.isdir(GEN):
        for f in os.listdir(GEN):
            os.remove(os.path.join(GEN, f))
    ARROW_L = save(img("bttn_5x5_arrow_L_off.png").resize((52, 52), Image.LANCZOS), "arrow_l.png")
    ARROW_L_ON = save(img("bttn_5x5_arrow_L_pressed.png").resize((52, 52), Image.LANCZOS), "arrow_l_on.png")
    ARROW_R = save(img("bttn_5x5_arrow_R_off.png").resize((52, 52), Image.LANCZOS), "arrow_r.png")
    ARROW_R_ON = save(img("bttn_5x5_arrow_R_off_pressed.png").resize((52, 52), Image.LANCZOS), "arrow_r_on.png")
    STOP_OFF = save(text_button("STOP", False, 150, 54, 26, (238, 104, 88)), "stop.png")
    stop_on = text_button("STOP", True, 150, 54, 26, (255, 255, 255))
    red = Image.new("RGBA", stop_on.size, (200, 40, 30, 120))
    red.putalpha(Image.composite(red.getchannel("A"), Image.new("L", red.size, 0), rounded_mask(150, 54, 6)))
    stop_on.alpha_composite(red)
    STOP_ON = save(stop_on, "stop_on.png")
    CPU_STRIP = cpu_meter_strip(130, 40)
    LED_OFF = save(img("LED_off.png").resize((22, 22), Image.LANCZOS), "led_off.png")
    LED_ON = save(img("LED_on.png").resize((22, 22), Image.LANCZOS), "led_on.png")
    NOISE_TG = pair("noise", "NOISE", 70, 44, 16)
    ARP_TG = pair("arp", "ARPEGGIATOR", 200, 48, 20)
    DIST_TG = pair("dist", "ON", 104, 44, 20)
    ON_TG = pair("fx_on", "ON", 96, 40, 20)
    PP_TG = pair("pingpong", "PINGPONG", 112, 46, 17)
    DELAY_TG = pair("delay_tg", "DELAY", 112, 46, 19)
    REVERB_TG = pair("reverb_tg", "REVERB", 112, 46, 19)
    ROUTE = {}
    for lab in ("1", "2", "3", "F1", "F2"):
        ROUTE[lab] = (save(square_button(lab, False, 52, 20), "route_%s.png" % lab.lower()),
                      save(square_button(lab, True, 52, 20), "route_%s_on.png" % lab.lower()))

    LINES.extend([
        "# Odin 2 for MPC: generated by vst/make_skin.py -- edit that script, not this file.",
        "art_css=art/skin.css",
        "qlink_bounds=column",
        "theme_bg=2e2f30", "theme_ink=dfe3e6", "theme_ink_dim=86cbf6", "theme_accent=86cbf6",
        "theme_accent_hi=86cbf6", "theme_line=0b0c0d", "theme_lcd=0a2832", "theme_box=1b1d1f",
        "theme_seg_active=3c9bc7", "theme_seg_inactive=17191b", "theme_seg_active_tx=06141b",
        "theme_btn_bg=2a2c2e", "theme_btn_text=cfe3ee", "theme_tile_on=12384a",
        "qlinks_track = master,glide,uni_detune,uni_width,fil1_freq,fil1_res,fil1_env,fil1_kbd,"
        "env1_a,env1_d,env1_s,env1_r,delay_wet,delay_fb,reverb_mix,reverb_size",
    ])
    tab_main()
    tab_osc()
    tab_filter()
    tab_env()
    tab_fx()
    with open(os.path.join(HERE, "layout.conf"), "w") as f:
        f.write("\n".join(LINES) + "\n")
    print("layout.conf: %d lines, %d images" % (len(LINES), len(ASSETS)))


if __name__ == "__main__":
    main()
