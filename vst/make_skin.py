#!/usr/bin/env python3
"""Odin 2's MPC pages (1.4): writes vst/layout.conf and every image it uses (vst/art/gen/).

Six pages (MAIN, OSC, FILTER, ENV, FX, BROWSE) in four looks the player switches live with THEME (BROWSE page):
  Odin Blue  Odin 2's own look, refined: its textured blue module panels on a deep chassis, crisp edges
  2000       a warm light-grey chassis with dark label bands and red accents, after the MPC2000 era
  2500       a charcoal chassis with silver bevels and blue-lit displays, after the MPC2500
  3000       a cream chassis with taupe panels, red accents, a violin and music notes, after the MPC3000
The looks are homages drawn here from scratch: no maker's logo, badge or artwork.

How a live look switch works with MPC's skin format: each page's background is the Odin look; every other look is
one `art ... when=theme:N` picture over the whole page (MPC shows the one the THEME parameter picks). What MPC draws
live on top (knobs, names, values, buttons, lists) is the same in every look, so it is made to sit on dark glass:
every knob's name and value has a dark plate under it in every look. Parts the skin builder bakes per mode (the
type-specific controls at the bottom of the OSC and FILTER columns, the ECO light) sit on insets drawn identically
in every look, so they match whichever look is showing. Text boxes and popup fields are drawn here (box=0 /
field=none), in each look's style.

Text system: one family per look for baked text (Aldrich, Odin's font, in Odin Blue; Titillium Web in the others),
titles 21 px, labels 16 px; MPC's own live names (24 px) and values (28 px) under every knob, the same everywhere.

Geometry lives here only: the script writes the layout, so the art and the controls can't drift apart. Coordinates
are plugin pixels (1280 x 628); the layout adds the 86 px Force Shadow offset. Needs Pillow. Run it after changing
anything here (and after vst/make_params.py).

From Odin 2 (vst/art/odin/, GPL-3.0-or-later): its panel texture and knot, knob filmstrips, LEDs and logo bar.
Fonts: Aldrich and Titillium Web (SIL Open Font License, vst/art/fonts/).
"""
import json
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "art", "odin")
GEN = os.path.join(HERE, "art", "gen")
FONTS = os.path.join(HERE, "art", "fonts")
ALDRICH = os.path.join(FONTS, "Aldrich-Regular.ttf")
TITILLIUM = os.path.join(FONTS, "TitilliumWeb-Bold.ttf")
TITILLIUM_SB = os.path.join(FONTS, "TitilliumWeb-SemiBold.ttf")
W, H, YOFF = 1280, 628, 86
SS = 2                      # backgrounds are drawn at 2x and scaled down: smooth edges and text

PARAMS = {p["key"]: p for p in json.load(open(os.path.join(HERE, "params.json")))["params"]}
THEME_NAMES = PARAMS["theme"]["options"]
LINES = []
ASSETS = set()

ODIN_BLUE = (60, 155, 199)
TEXT_BLUE = (134, 203, 246)

# ------------------------------------------------------------------------------------------------- geometry
HEAD = 80                        # top bar
TOP, BOT = 88, 620               # module panels
PH = BOT - TOP
SX = [8, 259, 510, 761]          # the four Q-Link columns
SW = 245
RX, RW = 1016, 256               # the right-hand panel
TITLE_H = 40                     # a panel's title band
NS, VS = 24, 28                  # MPC's live name / value text under every knob (px)
KNOB = {   # size: (radius, Odin strip, knob body px, touch/text width)
    "big": (40, "knob_8x8_a_strip.png", 64, 118),
    "mid": (32, "knob_6x6_a_strip.png", 48, 112),
    "small": (26, "knob_4x4_a_strip.png", 48, 112),
}
BIPOLAR = {"fil1_env", "fil2_env", "fil3_env", "tune"}
TYPED_ROWS = (TOP + 127, TOP + 285)   # knob rows of a column with a type picker under its title
TYPED_R3 = TOP + 430                  # the small knobs in its mode inset
INSET = (TOP + 400, BOT - 6)          # the mode inset (top, bottom)
SPREAD_ROWS = (TOP + 150, TOP + 350)  # two rows spread over a plain column
DECORATED = ("FINE TUNE", "DRIVE")    # panels whose lower part is empty: the 3000 look's violin and notes go there
FREE_TOP = 404                        # ... from here (below their two knob rows' plates), relative to the panel


def Y(py):
    return py + YOFF


def line(s):
    LINES.append(s)


def knob_geom(size):
    """(touch box top, bottom, name/value plate top, bottom) from the knob's centre, as the skin builder places MPC's
    name and value labels (tools/shadow_skin.py: name 1.2 x ns tall, value 1.2 x vs, under the knob)"""
    r = KNOB[size][0]
    s = 2 * r + 10
    name_h, value_h = round(NS * 1.2), round(VS * 1.2)
    name_y = s // 2 + r + 2
    value_y = name_y + name_h + 2
    ch = value_y + value_h + 6
    top = -(s // 2)
    return top, top + ch, r + 5, top + value_y + value_h + 2   # the plate starts clear of the knob's ring


# ------------------------------------------------------------------------------------------------- image helpers
def img(name):
    return Image.open(os.path.join(SRC, name)).convert("RGBA")


def font(path, size):
    return ImageFont.truetype(path, int(round(size)))


def save(im, name):
    os.makedirs(GEN, exist_ok=True)
    im.save(os.path.join(GEN, name), optimize=True)
    ASSETS.add(name)
    return "art/gen/" + name


def rgba(c, a=255):
    return tuple(c[:3]) + (a,)


def mix(a, b, t):
    return tuple(int(round(a[i] * (1 - t) + b[i] * t)) for i in range(3))


def vgradient(w, h, top, bottom):
    g = Image.new("RGBA", (1, h))
    for y in range(h):
        g.putpixel((0, y), rgba(mix(top, bottom, y / max(1, h - 1))))
    return g.resize((w, h))


def noise(w, h, amount, seed=1, stretch=1):
    """fine grain (an alpha layer, amount = its strongest alpha); stretch > 1 streaks it sideways (brushed metal)"""
    rnd = random.Random(seed)
    sw = max(1, w // stretch)
    n = Image.new("L", (sw, h))
    n.putdata([rnd.randint(0, 255) for _ in range(sw * h)])
    if stretch > 1:
        n = n.resize((w, h), Image.BILINEAR)
    light = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    light.putalpha(n.point(lambda v: int(max(0, v - 128) / 127 * amount)))
    dark = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    dark.putalpha(n.point(lambda v: int(max(0, 127 - v) / 127 * amount)))
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out.alpha_composite(light)
    out.alpha_composite(dark)
    return out


def rmask(w, h, r):
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), r, fill=255)
    return m


def shadow(canvas, x, y, w, h, r, blur=6, off=3, alpha=150):
    sh = Image.new("RGBA", (w + 4 * blur, h + 4 * blur), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((2 * blur, 2 * blur, 2 * blur + w, 2 * blur + h), r, fill=(0, 0, 0, alpha))
    sh = sh.filter(ImageFilter.GaussianBlur(blur))
    canvas.alpha_composite(sh, (max(0, x - 2 * blur), max(0, y - 2 * blur + off)))


def spaced(d, xy, text, fnt, fill, spacing=0.0, anchor="lm"):
    """text with letter spacing (px), anchored left/centre/right on its middle line"""
    if not spacing:
        d.text(xy, text, font=fnt, fill=fill, anchor=anchor)
        return
    widths = [fnt.getlength(c) for c in text]
    total = sum(widths) + spacing * (len(text) - 1)
    x, y = xy
    if anchor[0] == "m":
        x -= total / 2
    elif anchor[0] == "r":
        x -= total
    for c, cw in zip(text, widths):
        d.text((x, y), c, font=fnt, fill=fill, anchor="l" + anchor[1])
        x += cw + spacing


# ------------------------------------------------------------------------------------------------- Odin's textures
_TEX = {}


def odin_texture(w, h):
    """Odin's blue module texture (from its delay panel), mirrored to any size"""
    if "t" not in _TEX:
        _TEX["t"] = img("FX_Delay.png").crop((230, 24, 460, 168))
    t = _TEX["t"]
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


def odin_knot():
    """the Celtic knot that sits faintly behind Odin's panels: the darker lines of its delay panel's corner"""
    if "k" not in _TEX:
        k = img("FX_Delay.png").crop((6, 6, 176, 168))
        k.paste(odin_texture(26, 26), (4, 4), rmask(26, 26, 13))
        px = k.load()
        out = Image.new("RGBA", k.size, (0, 0, 0, 0))
        op = out.load()
        for j in range(k.size[1]):
            for i in range(k.size[0]):
                r, g, b, a = px[i, j]
                d = max(0, 62 - (r + g + b) / 3)
                op[i, j] = (12, 26, 36, int(min(255, d * 9)))
        _TEX["k"] = out
    return _TEX["k"]


# ------------------------------------------------------------------------------------------------- the looks
# Colours and a few switches per look; the drawing code is shared, so every look has the same structure.
LOOKS = [
    {   # Odin Blue
        "font": ALDRICH, "label_font": ALDRICH, "title_spacing": 2.0,
        "chassis": ((14, 24, 33), (7, 12, 17)), "grain": 5, "brushed": 1,
        "head": ((30, 44, 56), (16, 25, 33)), "head_line": (47, 86, 108), "head_label": (128, 152, 168),
        "logo": "odin", "logo_ink": (196, 222, 238),
        "panel": "odin", "panel_edge": (3, 7, 10), "panel_hi": (74, 122, 150), "radius": 12, "shadow": 150,
        "title": (168, 214, 242), "title_band": None, "title_rule": (36, 66, 84), "accent": (60, 155, 199),
        "plate": (5, 13, 19, 175), "plate_edge": (38, 72, 92, 200), "plate_r": 7,
        "box": (5, 20, 29), "box_edge": (46, 104, 134), "box_r": 6, "arrow": (110, 196, 238),
        "caption": (138, 160, 174), "caption_hi": TEXT_BLUE,
    },
    {   # 2000
        "font": TITILLIUM, "label_font": TITILLIUM_SB, "title_spacing": 2.5,
        "chassis": ((203, 201, 194), (188, 186, 179)), "grain": 7, "brushed": 1,
        "head": ((186, 184, 177), (168, 166, 159)), "head_line": (118, 116, 110), "head_label": (58, 58, 56),
        "logo": "text", "logo_ink": (40, 40, 39), "logo_dot": (196, 46, 40),
        "panel": ((176, 174, 167), (160, 158, 151)), "panel_edge": (104, 102, 97), "panel_hi": (226, 224, 218),
        "radius": 5, "shadow": 80,
        "title": (238, 237, 232), "title_band": (74, 74, 72), "title_rule": None, "accent": (196, 46, 40),
        "plate": (30, 37, 37, 255), "plate_edge": (88, 88, 84, 255), "plate_r": 3,
        "box": (30, 37, 37), "box_edge": (80, 80, 76), "box_r": 3, "arrow": (230, 229, 224),
        "caption": (52, 52, 50), "caption_hi": (176, 40, 34),
    },
    {   # 2500
        "font": TITILLIUM, "label_font": TITILLIUM_SB, "title_spacing": 2.5,
        "chassis": ((32, 33, 36), (22, 23, 25)), "grain": 9, "brushed": 18,
        "head": ((44, 46, 50), (26, 27, 30)), "head_line": (141, 146, 152), "head_label": (168, 173, 179),
        "logo": "text", "logo_ink": (218, 222, 227), "logo_dot": (78, 160, 232),
        "panel": ((54, 56, 61), (40, 42, 46)), "panel_edge": (10, 11, 13), "panel_hi": (160, 166, 172),
        "radius": 8, "shadow": 140,
        "title": (224, 228, 233), "title_band": None, "title_rule": (110, 115, 121), "accent": (78, 160, 232),
        "plate": (10, 32, 54, 255), "plate_edge": (44, 92, 140, 255), "plate_r": 4,
        "box": (10, 32, 54), "box_edge": (58, 112, 164), "box_r": 4, "arrow": (140, 196, 246),
        "caption": (170, 175, 181), "caption_hi": (96, 176, 242),
    },
    {   # 3000
        "font": TITILLIUM, "label_font": TITILLIUM_SB, "title_spacing": 2.5,
        "chassis": ((220, 211, 190), (206, 196, 174)), "grain": 7, "brushed": 1,
        "head": ((208, 199, 177), (192, 183, 160)), "head_line": (138, 128, 110), "head_label": (74, 66, 54),
        "logo": "text", "logo_ink": (58, 52, 43), "logo_dot": (168, 70, 44),
        "panel": ((203, 193, 169), (188, 178, 154)), "panel_edge": (128, 117, 99), "panel_hi": (240, 233, 216),
        "radius": 8, "shadow": 80,
        "title": (59, 52, 42), "title_band": None, "title_rule": (150, 138, 116), "accent": (168, 70, 44),
        "plate": (35, 42, 32, 255), "plate_edge": (96, 100, 80, 255), "plate_r": 4,
        "box": (35, 42, 32), "box_edge": (106, 109, 88), "box_r": 4, "arrow": (226, 222, 204),
        "caption": (74, 66, 54), "caption_hi": (168, 70, 44),
        "music": (110, 96, 74),
    },
]
assert len(LOOKS) == len(THEME_NAMES), "make_params.py THEMES and the looks here must match"

# drawn the same in every look (MPC's per-mode images are cut from the Odin page, so they must match any look)
INSET_FILL, INSET_EDGE, INSET_R = (12, 19, 24), (40, 54, 64), 8
JEWEL = (8, 12, 15)


# ------------------------------------------------------------------------------------------------- page model
class Page:
    """what a page's background is made of; each look draws it its own way"""

    def __init__(self, name):
        self.name = name
        self.panels = []     # (x, y, w, h, title)
        self.plates = []     # (x0, y0, x1, y1)
        self.insets = []     # (x0, y0, x1, y1)
        self.boxes = []      # (x0, y0, x1, y1, arrow)
        self.labels = []     # (x, y, text, role, anchor)  role: label | hi
        self.jewels = []     # (cx, cy, r)
        line("[tab %s]" % name)
        self.art_at = len(LINES)
        line("art file=art/gen/bg_%s.png x=0 y=%d w=%d h=%d" % (name.lower(), YOFF, W, H))


PAGE = None


def panel(x, w, title, y=TOP, h=PH):
    PAGE.panels.append((x, y, w, h, title))


def label(x, y, text, role="label", anchor="mm"):
    PAGE.labels.append((x, y, text, role, anchor))


def W_(when):
    """'osc1_type:analog' -> 'osc1_type:1' (the option's index: option names can hold spaces)"""
    key, opt = when.split(":")
    names = [o.replace(" ", "").lower() for o in PARAMS[key]["options"]]
    return "%s:%d" % (key, names.index(opt.replace(" ", "").lower()))


def wh(when):
    return " when=%s" % W_(when) if when else ""


_STRIPS = {}


def knob(cx, cy, key, size="big", when=None, plate=True):
    r, src, body, bw = KNOB[size]
    k = (size, key in BIPOLAR)
    if k not in _STRIPS:
        _STRIPS[k] = arc_knob_strip(src, body, r, "knob_%s%s.png" % (size, "_bip" if k[1] else ""), k[1])
    line('knob cx=%d cy=%d r=%d label="" key=%s strip=%s ns=%d vs=%d bw=%d%s' % (
        cx, Y(cy), r, key, _STRIPS[k], NS, VS, bw, wh(when)))
    if plate:
        _, _, p0, p1 = knob_geom(size)
        PAGE.plates.append((cx - bw // 2 + 3, cy + p0, cx + bw // 2 - 3, cy + p1))


def popup(cx, cy, w, h, key, when=None, cols=None):
    """a popup field: drawn here (field=none), unless it depends on a mode (when=: the builder draws it into that
    mode's own image, so it must sit on an inset)"""
    c = " cols=%d" % cols if cols else ""
    if when:
        line('popup cx=%d cy=%d w=%d h=%d label="" key=%s%s%s' % (cx, Y(cy), w, h, key, c, wh(when)))
    else:
        line('popup cx=%d cy=%d w=%d h=%d label="" key=%s field=none%s' % (cx, Y(cy), w, h, key, c))
        PAGE.boxes.append((cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2, True))


def readout(cx, cy, w, h, key, tsize, tcolor="86cbf6"):
    line('readout cx=%d cy=%d w=%d h=%d label="" key=%s box=0 tsize=%d tcolor=%s' % (cx, Y(cy), w, h, key, tsize, tcolor))
    PAGE.boxes.append((cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2, False))


def toggle_img(cx, cy, key, pair_, w, h, when=None):
    line('toggle cx=%d cy=%d label="" key=%s img=%s img_on=%s ns=0 w=%d h=%d%s' % (
        cx, Y(cy), key, pair_[0], pair_[1], w, h, wh(when)))


def qcol(sx):
    """the two knob columns of a Q-Link column"""
    return sx + 63, sx + 182


def four(sx, keys, rows, size="big"):
    a, b = qcol(sx)
    for k, (cx, cy) in zip(keys, [(a, rows[0]), (b, rows[0]), (a, rows[1]), (b, rows[1])]):
        if k != "-":
            knob(cx, cy, k, size)


def type_picker(sx, type_key):
    popup(sx + SW // 2, TOP + TITLE_H + 23, SW - 28, 36, type_key, cols=2)


def typed_column(sx, title, type_key):
    panel(sx, SW, title)
    type_picker(sx, type_key)
    PAGE.insets.append((sx + 8, INSET[0], sx + SW - 8, INSET[1]))


# ------------------------------------------------------------------------------------------------- control images
def arc_knob_strip(src, body, r, name, bipolar=False):
    """Odin's knob filmstrip, sharp at (or below) its native size, inside a value ring in Odin blue: a 270-degree
    track, lit from the minimum (or from the top, for bipolar controls) to the knob's position. 129 frames of the
    widget's 2r+8 box, as Odin's own strips."""
    s = 2 * r + 8
    strip = img(src)
    fw = strip.size[0]
    fh = strip.size[1] // 129
    scale = body / float(fw)
    k = 4
    ring = r - 1.5
    width = 4 if r >= 34 else 3
    out = Image.new("RGBA", (s, s * 129), (0, 0, 0, 0))
    for i in range(129):
        t = i / 128.0
        big = Image.new("RGBA", (s * k, s * k), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        box = [(s / 2 - ring) * k, (s / 2 - ring) * k, (s / 2 + ring) * k, (s / 2 + ring) * k]
        d.arc(box, 135, 405, fill=(6, 9, 11, 255), width=(width + 2) * k)
        d.arc(box, 135, 405, fill=(36, 46, 54, 255), width=width * k)
        a0, a1 = (270, 135 + 270 * t) if bipolar else (135, 135 + 270 * t)
        if abs(a1 - a0) > 0.5:
            d.arc(box, min(a0, a1), max(a0, a1), fill=(92, 196, 244, 255), width=width * k)
        tile = big.resize((s, s), Image.LANCZOS)
        f = strip.crop((0, i * fh, fw, (i + 1) * fh))
        if scale != 1.0:
            f = f.resize((max(1, round(fw * scale)), max(1, round(fh * scale))), Image.LANCZOS)
        ox = (s - f.size[0]) // 2
        oy = (s - round(fw * scale)) // 2
        layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        layer.paste(f, (ox, oy), f)
        tile.alpha_composite(layer)
        out.paste(tile, (0, i * s))
    return save(out, name)


def text_button(text, on, w, h, size, color=None):
    """a dark raised button with crisp text, the same in every look"""
    k = 3
    b = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0))
    face_top, face_bot = ((30, 56, 72), (14, 30, 40)) if on else ((48, 51, 55), (24, 26, 29))
    b.paste(vgradient(w * k, h * k, face_top, face_bot), (0, 0), rmask(w * k, h * k, 7 * k))
    d = ImageDraw.Draw(b)
    d.rounded_rectangle((0, 0, w * k - 1, h * k - 1), 7 * k, outline=(5, 7, 9, 255), width=2 * k)
    d.rounded_rectangle((2 * k, 2 * k, w * k - 1 - 2 * k, h * k - 1 - 2 * k), 5 * k,
                        outline=((74, 156, 204, 210) if on else (96, 100, 106, 120)), width=k)
    if text:
        c = color or (TEXT_BLUE if on else (196, 202, 206))
        f = font(ALDRICH, size * k)
        if on and not color:
            glow = Image.new("RGBA", b.size, (0, 0, 0, 0))
            ImageDraw.Draw(glow).text((w * k / 2, h * k / 2), text, font=f, fill=ODIN_BLUE + (255,), anchor="mm")
            b.alpha_composite(glow.filter(ImageFilter.GaussianBlur(4 * k)))
        d.text((w * k / 2, h * k / 2 + k), text, font=f, fill=rgba(c), anchor="mm")
    return b.resize((w, h), Image.LANCZOS)


def pair(name, text, w, h, size=18, on_color=None):
    return (save(text_button(text, False, w, h, size), "%s.png" % name),
            save(text_button(text, True, w, h, size, on_color), "%s_on.png" % name))


def arrow_button(direction, pressed, s=40, h=46):
    k = 3
    big = text_button("", pressed, s, h, 10).resize((s * k, h * k), Image.LANCZOS)
    d = ImageDraw.Draw(big)
    c, cy, a = s * k / 2, h * k / 2, 8 * k
    pts = [(c - a * 0.6, cy - a), (c - a * 0.6, cy + a), (c + a * 0.85, cy)] if direction > 0 else \
          [(c + a * 0.6, cy - a), (c + a * 0.6, cy + a), (c - a * 0.85, cy)]
    d.polygon(pts, fill=rgba(TEXT_BLUE if pressed else (204, 210, 214)))
    return big.resize((s, h), Image.LANCZOS)


def cpu_meter_strip(w, h):
    """21 frames, 0..100 % in 5 % steps: a segmented bar, green to amber to red, with the number"""
    frames = 21
    k = 2
    out = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for i in range(frames):
        pct = i * 5
        f = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0))
        d = ImageDraw.Draw(f)
        d.rounded_rectangle((0, 0, w * k - 1, h * k - 1), 6 * k, fill=(5, 18, 26, 255), outline=(46, 104, 134, 255), width=k)
        segs = 10
        bx, by, bw, bh = 48 * k, 10 * k, (w - 56) * k, (h - 20) * k
        sw = bw / segs
        lit = round(pct / 10.0)
        for s in range(segs):
            x0 = bx + s * sw + k
            col = (64, 196, 124) if s < 6 else (236, 180, 58) if s < 8 else (230, 72, 54)
            if s >= lit:
                col = tuple(int(c * 0.2) for c in col)
            d.rounded_rectangle((x0, by, x0 + sw - 3 * k, by + bh), k, fill=col + (255,))
        tcol = (230, 72, 54) if pct >= 80 else TEXT_BLUE
        d.text((25 * k, h * k / 2), "%d" % pct, font=font(ALDRICH, 19 * k), fill=tcol + (255,), anchor="mm")
        out.paste(f.resize((w, h), Image.LANCZOS), (0, i * h))
    return save(out, "cpu_meter.png")


def card(on, w, h):
    """a BROWSE list tile, the same in every look: dark glass, lit in Odin blue when selected"""
    k = 3
    c = vgradient(w * k, h * k, *(((22, 62, 82), (12, 40, 56)) if on else ((16, 23, 29), (10, 15, 20))))
    d = ImageDraw.Draw(c)
    d.rectangle((0, 0, w * k - 1, h * k - 1), outline=(4, 7, 9, 255), width=k)
    if on:
        d.rectangle((k, k, w * k - 1 - k, h * k - 1 - k), outline=(86, 190, 240, 255), width=2 * k)
        d.rectangle((4 * k, 7 * k, 7 * k, h * k - 8 * k), fill=(86, 190, 240, 255))
    else:
        d.rectangle((k, k, w * k - 1 - k, h * k - 1 - k), outline=(40, 58, 70, 255), width=k)
    return c.resize((w, h), Image.LANCZOS)


# ------------------------------------------------------------------------------------------------- drawing a look
def music_motif(w, h, ink, alpha):
    """a violin and a few notes in thin lines, drawn here (the 3000 look)"""
    k = 2
    m = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(m)
    col, faint = rgba(ink, alpha), rgba(ink, alpha // 2)
    lw = 2 * k
    cx, cy = w * k * 0.62, h * k * 0.60
    s = min(w * 1.1, h * 0.5) * k / 210.0
    d.ellipse((cx - 46 * s, cy + 6 * s, cx + 46 * s, cy + 92 * s), outline=col, width=lw)     # lower bout
    d.ellipse((cx - 36 * s, cy - 70 * s, cx + 36 * s, cy - 6 * s), outline=col, width=lw)     # upper bout
    gap = Image.new("L", m.size, 0)
    ImageDraw.Draw(gap).rectangle((cx - 30 * s, cy - 16 * s, cx + 30 * s, cy + 20 * s), fill=255)
    m.putalpha(ImageChops.subtract(m.getchannel("A"), gap))                                   # open the waist
    d.arc((cx - 50 * s, cy - 34 * s, cx - 14 * s, cy + 34 * s), 300, 60, fill=col, width=lw)   # C-bouts
    d.arc((cx + 14 * s, cy - 34 * s, cx + 50 * s, cy + 34 * s), 120, 240, fill=col, width=lw)
    for x in (cx - 8 * s, cx + 8 * s):                                                        # neck
        d.line((x, cy - 70 * s, x, cy - 150 * s), fill=col, width=lw)
    d.ellipse((cx - 13 * s, cy - 174 * s, cx + 13 * s, cy - 148 * s), outline=col, width=lw)  # scroll
    d.ellipse((cx - 5 * s, cy - 166 * s, cx + 5 * s, cy - 156 * s), outline=col, width=lw)
    for side in (-1, 1):                                                                      # f-holes
        x = cx + side * 20 * s
        d.arc((x - 6 * s, cy - 22 * s, x + 6 * s, cy), 90 if side < 0 else 270, 270 if side < 0 else 90, fill=col, width=lw)
        d.arc((x - 6 * s, cy, x + 6 * s, cy + 22 * s), 270 if side < 0 else 90, 90 if side < 0 else 270, fill=col, width=lw)
    for x in (cx - 3 * s, cx + 3 * s):                                                        # strings
        d.line((x, cy - 150 * s, x, cy + 88 * s), fill=faint, width=k)
    rx, ry = 9 * s, 6.5 * s
    for (nx, ny, kind) in ((0.14, 0.24, 1), (0.24, 0.16, 2), (0.18, 0.46, 1), (0.30, 0.56, 0)):   # notes
        x, y = nx * w * k, ny * h * k
        d.ellipse((x - rx, y - ry, x + rx, y + ry), fill=col)
        d.line((x + rx - k, y, x + rx - k, y - 40 * s), fill=col, width=lw)
        if kind == 1:
            d.line((x + rx - k, y - 40 * s, x + rx + 12 * s, y - 28 * s), fill=col, width=lw)
        elif kind == 2:
            x2 = x + 28 * s
            d.ellipse((x2 - rx, y + 6 * s - ry, x2 + rx, y + 6 * s + ry), fill=col)
            d.line((x2 + rx - k, y + 6 * s, x2 + rx - k, y - 34 * s), fill=col, width=lw)
            d.line((x + rx - k, y - 40 * s, x2 + rx - k, y - 34 * s), fill=col, width=3 * k)
    return m.resize((w, h), Image.LANCZOS)


def draw_panel(look, x, y, w, h, title, decorate):
    """one module panel onto the page (at SS scale)"""
    k = SS
    pw, ph = w * k, h * k
    r = look["radius"] * k
    shadow(CANVAS, x * k, y * k, pw, ph, r, blur=5 * k, off=2 * k, alpha=look["shadow"])
    if look["panel"] == "odin":
        body = odin_texture(w, h).resize((pw, ph), Image.LANCZOS)
        dark = Image.new("RGBA", (pw, ph), (6, 16, 24, 0))
        dark.putalpha(vgradient(pw, ph, (90, 90, 90), (150, 150, 150)).getchannel("R"))
        body.alpha_composite(dark)
        kn = odin_knot()
        kn = kn.resize((int(kn.size[0] * 1.15 * k), int(kn.size[1] * 1.15 * k)), Image.LANCZOS)
        kn.putalpha(kn.getchannel("A").point(lambda v: int(v * 0.5)))
        body.alpha_composite(kn, (4 * k, max(4 * k, ph // 2 - 60 * k)))
    else:
        body = vgradient(pw, ph, look["panel"][0], look["panel"][1])
        body.alpha_composite(noise(pw, ph, look["grain"], seed=x * 7 + y, stretch=look["brushed"]))
    if look.get("music") and decorate:   # in the panel's empty lower part only (make_skin: FREE_BOTTOM)
        mh = (h - FREE_TOP) * k
        body.alpha_composite(music_motif(pw - 24 * k, mh - 16 * k, look["music"], 120), (12 * k, FREE_TOP * k + 6 * k))
    d = ImageDraw.Draw(body)
    if look["title_band"] is not None:
        d.rounded_rectangle((3 * k, 3 * k, pw - 3 * k, TITLE_H * k), max(1, r - 2 * k), fill=rgba(look["title_band"]))
        d.rectangle((3 * k, (TITLE_H - 8) * k, pw - 3 * k, TITLE_H * k), fill=rgba(look["title_band"]))
        d.rectangle((3 * k, TITLE_H * k, 40 * k, TITLE_H * k + 2 * k), fill=rgba(look["accent"]))
    if look["title_rule"] is not None:
        d.line((14 * k, TITLE_H * k, pw - 14 * k, TITLE_H * k), fill=rgba(look["title_rule"], 200), width=k)
        d.line((14 * k, TITLE_H * k, 52 * k, TITLE_H * k), fill=rgba(look["accent"]), width=2 * k)
    if title:
        spaced(d, (18 * k, (TITLE_H // 2 + 1) * k), title.upper(), font(look["font"], 21 * k), rgba(look["title"]),
               look["title_spacing"] * k, "lm")
    d.rounded_rectangle((2 * k, 2 * k, pw - 1 - 2 * k, ph - 1 - 2 * k), max(1, r - 2 * k),
                        outline=rgba(look["panel_hi"], 110), width=k)
    CANVAS.paste(body, (x * k, y * k), rmask(pw, ph, r))
    ImageDraw.Draw(CANVAS).rounded_rectangle((x * k, y * k, x * k + pw - 1, y * k + ph - 1), r,
                                              outline=rgba(look["panel_edge"]), width=2 * k)


def draw_inset(x0, y0, x1, y1):
    k = SS
    d = ImageDraw.Draw(CANVAS)
    d.rounded_rectangle((x0 * k, y0 * k, x1 * k, y1 * k), INSET_R * k, fill=rgba(INSET_FILL), outline=rgba(INSET_EDGE), width=k)
    d.line(((x0 + INSET_R) * k, (y0 + 2) * k, (x1 - INSET_R) * k, (y0 + 2) * k), fill=(0, 0, 0, 160), width=2 * k)


def draw_plate(look, x0, y0, x1, y1):
    k = SS
    layer = Image.new("RGBA", ((x1 - x0 + 2) * k, (y1 - y0 + 2) * k), (0, 0, 0, 0))
    ImageDraw.Draw(layer).rounded_rectangle((0, 0, (x1 - x0) * k, (y1 - y0) * k), look["plate_r"] * k,
                                            fill=look["plate"], outline=look["plate_edge"], width=k)
    CANVAS.alpha_composite(layer, (x0 * k, y0 * k))


def draw_box(look, x0, y0, x1, y1, arrow):
    k = SS
    d = ImageDraw.Draw(CANVAS)
    r = look["box_r"] * k
    d.rounded_rectangle((x0 * k, y0 * k, x1 * k, y1 * k), r, fill=rgba(look["box"]), outline=rgba(look["box_edge"]),
                        width=max(1, int(1.5 * k)))
    d.line((x0 * k + r, y0 * k + 2 * k, x1 * k - r, y0 * k + 2 * k), fill=(0, 0, 0, 120), width=2 * k)
    if arrow:
        ax, ay, a = (x1 - 18) * k, (y0 + y1) / 2 * k, 6 * k
        d.line(((x1 - 36) * k, (y0 + 7) * k, (x1 - 36) * k, (y1 - 7) * k), fill=rgba(look["box_edge"]), width=k)
        d.polygon([(ax - a, ay - a * 0.55), (ax + a, ay - a * 0.55), (ax, ay + a * 0.75)], fill=rgba(look["arrow"]))


def draw_jewel(cx, cy, r):
    """the ECO light's bezel: a rounded square that covers the light's whole per-mode image (26 x 26)"""
    k = SS
    ImageDraw.Draw(CANVAS).rounded_rectangle(((cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k), 5 * k,
                                             fill=rgba(JEWEL), outline=(60, 66, 70, 255), width=k)


def draw_header(look):
    k = SS
    CANVAS.paste(vgradient(W * k, HEAD * k, look["head"][0], look["head"][1]), (0, 0))
    CANVAS.alpha_composite(noise(W * k, HEAD * k, look["grain"], seed=5, stretch=look["brushed"]), (0, 0))
    d = ImageDraw.Draw(CANVAS)
    d.line((0, (HEAD - 2) * k, W * k, (HEAD - 2) * k), fill=(0, 0, 0, 200), width=2 * k)
    d.line((0, HEAD * k - k, W * k, HEAD * k - k), fill=rgba(look["head_line"]), width=k)
    # the name, set straight onto the bar in the look's own lettering (no badge or box), clear of the ECO field
    spaced(d, (LOGO_X * k, 34 * k), "ODIN 2", font(look["font"], 28 * k), rgba(look["logo_ink"]), 4 * k, "lm")
    spaced(d, (LOGO_X * k + k, 58 * k), "SYNTHESIZER", font(look["label_font"], 11 * k), rgba(look["head_label"]),
           3.6 * k, "lm")
    for x, t in HEAD_LABELS:
        spaced(d, (x * k, 13 * k), t, font(look["label_font"], 15 * k), rgba(look["head_label"]), 1.5 * k, "mm")


def draw_labels(look, labels):
    k = SS
    d = ImageDraw.Draw(CANVAS)
    for x, y, t, role, anchor in labels:
        col = look["caption_hi"] if role == "hi" else look["caption"]
        spaced(d, (x * k, y * k), t, font(look["label_font"], 16 * k), rgba(col), 1.2 * k, anchor)


CANVAS = None


def render(page, look):
    """the page's background in one look (W x H)"""
    global CANVAS
    k = SS
    CANVAS = vgradient(W * k, H * k, look["chassis"][0], look["chassis"][1])
    CANVAS.alpha_composite(noise(W * k, H * k, look["grain"], seed=3, stretch=look["brushed"]))
    draw_header(look)
    for x, y, w, h, title in page.panels:
        draw_panel(look, x, y, w, h, title, decorate=(title in DECORATED))
    for p in page.plates:
        draw_plate(look, *p)
    for p in page.insets:
        draw_inset(*p)
    for b in page.boxes:
        draw_box(look, *b)
    for j in page.jewels:
        draw_jewel(*j)
    draw_labels(look, page.labels)
    return CANVAS.resize((W, H), Image.LANCZOS)


def finish(page):
    """the page's background in every look, and a when=theme art line for each look but the first"""
    name = page.name.lower()
    save(render(page, LOOKS[0]), "bg_%s.png" % name)
    extra = []
    for t in range(1, len(LOOKS)):
        f = save(render(page, LOOKS[t]), "bg_%s_t%d.png" % (name, t))
        extra.append("art file=%s x=0 y=%d w=%d h=%d when=theme:%d" % (f, YOFF, W, H, t))
    # right after the page's own background: under everything the builder adds per mode
    LINES[page.art_at + 1:page.art_at + 1] = extra


# ------------------------------------------------------------------------------------------------- the pages
LOGO_X = 18
HEAD_LABELS = ((226, "ECO"), (448, "BANK"), (761, "PATCH"), (976, "VOICES"), (1078, "CPU"))


def header():
    """the top bar on every page: ECO, the bank and the patch (each with its own arrows), voices, CPU, STOP. Every
    part keeps at least 4 px from its neighbours; the name on the left keeps 20 px from ECO."""
    popup(226, 46, 96, 42, "eco")
    PAGE.jewels.append((292, 46, 15))
    line('picture x=%d y=%d w=22 h=22 key=eco_active files="%s,%s"' % (281, Y(35), LED_OFF, LED_ON))
    line('button cx=338 cy=%d label="" key=bank_prev img=%s img_on=%s w=40 h=46' % (Y(46), ARROW_L, ARROW_L_ON))
    readout(448, 46, 170, 46, "bank_name", 24)
    line('button cx=558 cy=%d label="" key=bank_next img=%s img_on=%s w=40 h=46' % (Y(46), ARROW_R, ARROW_R_ON))
    line('button cx=610 cy=%d label="" key=preset_prev img=%s img_on=%s w=40 h=46' % (Y(46), ARROW_L, ARROW_L_ON))
    readout(761, 46, 254, 46, "preset_name", 24)
    line('button cx=912 cy=%d label="" key=preset_next img=%s img_on=%s w=40 h=46' % (Y(46), ARROW_R, ARROW_R_ON))
    readout(976, 46, 72, 46, "voices", 28)
    line('meter cx=1078 cy=%d w=120 h=42 key=cpu strip=%s frames=21' % (Y(46), CPU_STRIP))
    line('button cx=1205 cy=%d label="" key=panic img=%s img_on=%s w=118 h=50' % (Y(46), STOP_OFF, STOP_ON))


def new_page(name):
    global PAGE
    PAGE = Page(name)
    header()


def tab_main():
    new_page("MAIN")
    rows = SPREAD_ROWS
    panel(SX[0], SW, "VOICE")
    four(SX[0], ["master", "glide", "uni_detune", "uni_width"], rows)
    panel(SX[1], SW, "FILTER 1")
    type_picker(SX[1], "fil1_type")
    four(SX[1], ["fil1_freq", "fil1_res", "fil1_env", "fil1_kbd"], rows)
    panel(SX[2], SW, "AMP ENVELOPE")
    four(SX[2], ["env1_a", "env1_d", "env1_s", "env1_r"], rows)
    panel(SX[3], SW, "SPACE")
    four(SX[3], ["delay_wet", "delay_fb", "reverb_mix", "reverb_size"], rows)
    a, b = qcol(SX[3])
    toggle_img(a, BOT - 38, "delay_on", DELAY_TG, 112, 44)
    toggle_img(b, BOT - 38, "reverb_on", REVERB_TG, 112, 44)
    panel(RX, RW, "PLAY")
    label(RX + RW // 2, TOP + 62, "MODE")
    line('enum_v cx=%d cy=%d label="" key=playmode sw=204 sh=44' % (RX + RW // 2, Y(TOP + 148)))
    label(RX + RW // 2, TOP + 240, "UNISON")
    popup(RX + RW // 2, TOP + 276, 204, 42, "unison")
    knob(RX + 66, TOP + 362, "amp_vel", "mid")
    knob(RX + 190, TOP + 362, "bend", "mid")
    toggle_img(RX + RW // 2, BOT - 40, "arp_on", ARP_TG, 210, 48)
    line('qlinks "MAIN" = master,glide,uni_detune,uni_width,fil1_freq,fil1_res,fil1_env,fil1_kbd,'
         'env1_a,env1_d,env1_s,env1_r,delay_wet,delay_fb,reverb_mix,reverb_size')
    finish(PAGE)


def tab_osc():
    new_page("OSC")
    mid = (INSET[0] + INSET[1]) // 2
    for n, sx in zip((1, 2, 3), SX):
        o = "osc%d" % n
        typed_column(sx, "OSC %d" % n, o + "_type")
        four(sx, [o + "_vol", o + "_shape", o + "_oct", o + "_semi"], TYPED_ROWS)
        a, b = qcol(sx)
        line('enum_h cx=%d cy=%d sw=106 rows=2 label="" key=%s_wave%s' % (sx + SW // 2, Y(mid), o, wh(o + "_type:analog")))
        for mode in ("wavetable", "multi"):
            popup(sx + SW // 2, mid, SW - 44, 44, o + "_table", when="%s_type:%s" % (o, mode), cols=3)
        knob(a, TYPED_R3, o + "_vecy", "small", when=o + "_type:vector", plate=False)
        popup(a + 26, mid, 150, 44, o + "_chip", when=o + "_type:chiptune")
        toggle_img(b + 28, mid, o + "_chipnoise", NOISE_TG, 72, 44, when=o + "_type:chiptune")
        for mode in ("fm", "pm"):
            knob(a, TYPED_R3, o + "_cratio", "small", when="%s_type:%s" % (o, mode), plate=False)
            knob(b, TYPED_R3, o + "_mratio", "small", when="%s_type:%s" % (o, mode), plate=False)
        knob(a, TYPED_R3, o + "_hp", "small", when=o + "_type:noise", plate=False)
        for mode in ("wavedraw", "chipdraw", "specdraw"):
            line('text cx=%d cy=%d label="DRAWN IN THE PATCH" size=1.5 color=86cbf6 font="Aldrich" '
                 'fontfile=art/fonts/Aldrich-Regular.ttf%s' % (sx + SW // 2, Y(mid), wh("%s_type:%s" % (o, mode))))
    panel(SX[3], SW, "FINE TUNE")
    four(SX[3], ["osc1_fine", "osc2_fine", "osc3_fine", "tune"], TYPED_ROWS)
    panel(RX, RW, "UNISON")
    label(RX + RW // 2, TOP + 62, "VOICES PER NOTE")
    popup(RX + RW // 2, TOP + 98, 204, 42, "unison")
    knob(RX + 66, TOP + 202, "uni_detune", "mid")
    knob(RX + 190, TOP + 202, "uni_width", "mid")
    label(RX + RW // 2, TOP + 372, "SHAPE turns what")
    label(RX + RW // 2, TOP + 396, "matters for the type:")
    label(RX + RW // 2, TOP + 426, "PW  ·  POSITION  ·  FM", "hi")
    line('qlinks "OSC" = osc1_vol,osc1_shape,osc1_oct,osc1_semi,osc2_vol,osc2_shape,osc2_oct,osc2_semi,'
         'osc3_vol,osc3_shape,osc3_oct,osc3_semi,osc1_fine,osc2_fine,osc3_fine,tune')
    finish(PAGE)


def tab_filter():
    new_page("FILTER")
    for n, sx in zip((1, 2, 3), SX):
        f = "fil%d" % n
        typed_column(sx, "FILTER %d" % n, f + "_type")
        four(sx, [f + "_freq", f + "_res", f + "_env", f + "_kbd"], TYPED_ROWS)
        a, b = qcol(sx)
        knob(a, TYPED_R3, f + "_vel", "small", plate=False)
        knob(b, TYPED_R3, f + "_sem", "small", when=f + "_type:sem 12", plate=False)
        knob(b, TYPED_R3, f + "_formant", "small", when=f + "_type:formant", plate=False)
        knob(b, TYPED_R3, f + "_ring", "small", when=f + "_type:ringmod", plate=False)
    panel(SX[3], SW, "DRIVE")
    four(SX[3], ["fil1_sat", "fil2_sat", "dist_boost", "dist_mix"], TYPED_ROWS)
    panel(RX, RW, "ROUTING")
    y0 = TOP + 68
    for row, (lab, srcs) in enumerate([("INTO FILTER 1", ["osc1", "osc2", "osc3"]),
                                       ("INTO FILTER 2", ["osc1", "osc2", "osc3", "fil1"])]):
        y = y0 + row * 104
        label(RX + 18, y, lab, anchor="lm")
        for i, sname in enumerate(srcs):
            lab2 = {"osc1": "1", "osc2": "2", "osc3": "3", "fil1": "F1"}[sname]
            toggle_img(RX + 42 + i * 58, y + 46, "fil%d_%s" % (row + 1, sname), ROUTE[lab2], 52, 52)
    y = y0 + 208
    label(RX + 18, y, "TO AMP", anchor="lm")
    toggle_img(RX + 42, y + 46, "fil1_amp", ROUTE["F1"], 52, 52)
    toggle_img(RX + 100, y + 46, "fil2_amp", ROUTE["F2"], 52, 52)
    y = y0 + 318
    label(RX + 18, y, "DISTORTION", anchor="lm")
    toggle_img(RX + 70, y + 42, "dist_on", DIST_TG, 104, 44)
    line('enum_h cx=%d cy=%d label="" key=dist_algo sw=78 sh=42' % (RX + RW // 2, Y(y + 98)))
    line('qlinks "FILTER" = fil1_freq,fil1_res,fil1_env,fil1_kbd,fil2_freq,fil2_res,fil2_env,fil2_kbd,'
         'fil3_freq,fil3_res,fil3_env,fil3_kbd,fil1_sat,fil2_sat,dist_boost,dist_mix')
    finish(PAGE)


def tab_env():
    new_page("ENV")
    for n, sx, title in zip((1, 2, 3), SX, ("AMP ENV", "FILTER ENV", "MOD ENV")):
        panel(sx, SW, title)
        e = "env%d" % n
        four(sx, [e + "_a", e + "_d", e + "_s", e + "_r"], SPREAD_ROWS)
    panel(SX[3], SW, "LFO RATE")
    four(SX[3], ["lfo1_rate", "lfo2_rate", "lfo3_rate", "lfo4_rate"], SPREAD_ROWS)
    panel(RX, RW, "LFO WAVE")
    for i, (k, lab) in enumerate([("lfo1_wave", "LFO 1"), ("lfo2_wave", "LFO 2"), ("lfo3_wave", "LFO 3"),
                                  ("lfo4_wave", "GLOBAL")]):
        y = TOP + 68 + i * 112
        label(RX + 18, y, lab, anchor="lm")
        popup(RX + RW // 2, y + 38, 220, 42, k, cols=3)
    line('qlinks "ENV" = env1_a,env1_d,env1_s,env1_r,env2_a,env2_d,env2_s,env2_r,'
         'env3_a,env3_d,env3_s,env3_r,lfo1_rate,lfo2_rate,lfo3_rate,lfo4_rate')
    finish(PAGE)


def fx_column(sx, title, on_key, keys):
    panel(sx, SW, title)
    toggle_img(sx + SW - 56, TOP + TITLE_H // 2 + 1, on_key, ON_TG, 84, 32)
    four(sx, keys, SPREAD_ROWS)


def tab_fx():
    new_page("FX")
    fx_column(SX[0], "DELAY", "delay_on", ["delay_time", "delay_fb", "delay_wet", "-"])
    toggle_img(qcol(SX[0])[1], SPREAD_ROWS[1], "delay_pp", PP_TG, 112, 46)
    fx_column(SX[1], "REVERB", "reverb_on", ["reverb_size", "reverb_damp", "reverb_mix", "-"])
    fx_column(SX[2], "CHORUS", "chorus_on", ["chorus_rate", "chorus_amt", "chorus_mix", "-"])
    fx_column(SX[3], "PHASER", "phaser_on", ["phaser_rate", "phaser_amt", "phaser_mix", "-"])
    panel(RX, RW, "FLANGER")
    toggle_img(RX + RW - 56, TOP + TITLE_H // 2 + 1, "flanger_on", ON_TG, 84, 32)
    knob(RX + 66, SPREAD_ROWS[0], "flanger_rate", "mid")
    knob(RX + 190, SPREAD_ROWS[0], "flanger_amt", "mid")
    knob(RX + 66, SPREAD_ROWS[1], "flanger_mix", "mid")
    line('qlinks "FX" = delay_time,delay_fb,delay_wet,-,reverb_size,reverb_damp,reverb_mix,-,'
         'chorus_rate,chorus_amt,chorus_mix,-,phaser_rate,phaser_amt,phaser_mix,-')
    finish(PAGE)


def tab_browse():
    """the patch browser: the banks (patch categories) on the left, the bank's patches on the right; the look"""
    new_page("BROWSE")
    bx, bw = 8, 372
    panel(bx, bw, "BANKS")
    cols, rows, th, gap = 2, 8, 46, 6
    lw = bw - 28
    tw = (lw - gap) // cols
    line('list x=%d y=%d w=%d h=%d key=bank_slot cols=%d rows=%d th=%d gap=%d order=cols img=%s img_on=%s '
         'tsize=26 tcolor=e6edf2' % (bx + 14, Y(TOP + 56), lw, rows * th + (rows - 1) * gap, cols, rows, th, gap,
                                     save(card(False, tw, th), "card_bank.png"), save(card(True, tw, th), "card_bank_on.png")))
    label(bx + 18, BOT - 40, "THEME", anchor="lm")
    popup(bx + bw - 126, BOT - 40, 220, 42, "theme")
    px, pw = 388, W - 8 - 388
    panel(px, pw, "PATCHES")
    cols, rows, th, gap = 3, 10, 43, 4
    lw = pw - 28
    tw = (lw - 2 * gap) // cols
    line('list x=%d y=%d w=%d h=%d key=patch_slot cols=%d rows=%d th=%d gap=%d order=cols img=%s img_on=%s '
         'tsize=26 tcolor=e6edf2' % (px + 14, Y(TOP + 56), lw, rows * th + (rows - 1) * gap, cols, rows, th, gap,
                                     save(card(False, tw, th), "card_patch.png"), save(card(True, tw, th), "card_patch_on.png")))
    line('qlinks "BROWSE" = browse_bank,browse_patch,-,-,-,-,-,-,-,-,-,-,-,-,-,-')   # banks, patches: each on its own
    finish(PAGE)


def main():
    global ARROW_L, ARROW_L_ON, ARROW_R, ARROW_R_ON, STOP_OFF, STOP_ON, CPU_STRIP, LED_OFF, LED_ON
    global NOISE_TG, ARP_TG, DIST_TG, ON_TG, PP_TG, DELAY_TG, REVERB_TG, ROUTE
    random.seed(1)
    if os.path.isdir(GEN):
        for f in os.listdir(GEN):
            os.remove(os.path.join(GEN, f))
    ARROW_L = save(arrow_button(-1, False), "arrow_l.png")
    ARROW_L_ON = save(arrow_button(-1, True), "arrow_l_on.png")
    ARROW_R = save(arrow_button(1, False), "arrow_r.png")
    ARROW_R_ON = save(arrow_button(1, True), "arrow_r_on.png")
    STOP_OFF = save(text_button("STOP", False, 118, 50, 24, (240, 108, 92)), "stop.png")
    stop_on = text_button("STOP", True, 118, 50, 24, (255, 255, 255))
    red = Image.new("RGBA", stop_on.size, (210, 44, 32, 130))
    red.putalpha(ImageChops.multiply(red.getchannel("A"), rmask(118, 50, 7)))
    stop_on.alpha_composite(red)
    STOP_ON = save(stop_on, "stop_on.png")
    CPU_STRIP = cpu_meter_strip(120, 42)
    LED_OFF = save(img("LED_off.png").resize((22, 22), Image.LANCZOS), "led_off.png")
    LED_ON = save(img("LED_on.png").resize((22, 22), Image.LANCZOS), "led_on.png")
    NOISE_TG = pair("noise", "NOISE", 72, 44, 16)
    ARP_TG = pair("arp", "ARPEGGIATOR", 210, 48, 20)
    DIST_TG = pair("dist", "ON", 104, 44, 20)
    ON_TG = pair("fx_on", "ON", 84, 32, 17)
    PP_TG = pair("pingpong", "PING PONG", 112, 46, 16)
    DELAY_TG = pair("delay_tg", "DELAY", 112, 46, 19)
    REVERB_TG = pair("reverb_tg", "REVERB", 112, 46, 19)
    ROUTE = {}
    for lab in ("1", "2", "3", "F1", "F2"):
        ROUTE[lab] = pair("route_%s" % lab.lower(), lab, 52, 52, 20)
    LINES.extend([
        "# Odin 2 for MPC: generated by vst/make_skin.py -- edit that script, not this file.",
        "art_css=art/skin.css",
        "qlink_bounds=column",
        "theme_bg=0b1218", "theme_ink=e2e7ea", "theme_ink_dim=86cbf6", "theme_accent=86cbf6",
        "theme_accent_hi=86cbf6", "theme_line=04080b", "theme_lcd=05141d", "theme_box=0e171e",
        "theme_seg_active=3c9bc7", "theme_seg_inactive=141b21", "theme_seg_active_tx=04121a",
        "theme_btn_bg=1e2a33", "theme_btn_text=cfe3ee", "theme_tile_on=12384a",
        "qlinks_track = master,glide,uni_detune,uni_width,fil1_freq,fil1_res,fil1_env,fil1_kbd,"
        "env1_a,env1_d,env1_s,env1_r,delay_wet,delay_fb,reverb_mix,reverb_size",
    ])
    tab_main()
    tab_osc()
    tab_filter()
    tab_env()
    tab_fx()
    tab_browse()
    with open(os.path.join(HERE, "layout.conf"), "w") as f:
        f.write("\n".join(LINES) + "\n")
    print("layout.conf: %d lines, %d images" % (len(LINES), len(ASSETS)))


if __name__ == "__main__":
    main()
