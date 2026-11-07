#!/usr/bin/env python3
"""Odin 2's MPC pages (1.4.7): writes vst/layout.conf and every image it uses (vst/art/gen/).

Six pages (MAIN, OSC, FILTER, ENV, FX, BROWSE) in four looks the player switches live on the BROWSE page's LOOK bar:
  Odin Blue  Odin 2's own look, refined: its textured blue module panels on a deep chassis, ice-blue rings and values
  2000       a light-grey chassis with charcoal title bands, red keys and rings, a green-lit display, after the MPC2000
  Arctic     white and brushed silver, signal-orange keys and rings, dark ink
  Sunburst   an orange chassis with charcoal modules, white names and orange values
The looks are homages drawn here from scratch: no maker's logo, badge or artwork.

How a live look switch works with MPC's skin format: each page's background is the Odin Blue look; every other look
is one `art ... when=theme:N` picture over the whole page. The live controls (knobs, names, values, buttons, fields,
the CPU meter) are each written once per look with `when=theme:N`, in that look's colours, so no look borrows
another's. Three kinds of part are one set for every look, in neutral graphite and silver: the controls of a mode
(at the bottom of the OSC and FILTER columns: a widget takes one when= condition, and theirs is the mode), and the
option buttons and BROWSE list tiles (the skin builder names their pictures after the parameter, so per-look copies
would overwrite each other). The fields and the plates under each knob's name and value are smoked glass: the
look's panel shows through.

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
    "small": (26, "knob_6x6_a_strip.png", 40, 112),   # the 6x6 knob drawn smaller: sharp, and fills its ring
}
BIPOLAR = {"fil1_env", "fil2_env", "fil3_env", "tune"}
TYPED_ROWS = (TOP + 127, TOP + 285)   # knob rows of a column with a type picker under its title
TYPED_R3 = TOP + 430                  # the small knobs in its mode inset
INSET = (TOP + 400, BOT - 6)          # the mode inset (top, bottom)
SPREAD_ROWS = (TOP + 150, TOP + 350)  # two rows spread over a plain column


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
def ui(ring, value, ink, lit_top, lit_bot, lit_edge, lit_text, btn_top, btn_bot, btn_text, glass, glass_edge,
       track=(40, 44, 48), bar=None, alpha=130):
    """a look's live controls: knob ring (lit, track), value and name text, buttons (off / lit), the dark glass of its
    displays (plates, fields, list tiles, CPU meter)"""
    return {"ring": ring, "track": track, "value": value, "ink": ink, "lit": (lit_top, lit_bot), "lit_edge": lit_edge,
            "lit_text": lit_text, "btn": (btn_top, btn_bot), "btn_text": btn_text, "glass": glass,
            "glass_edge": glass_edge, "bar": bar or ring, "alpha": alpha}


LOOKS = [
    {   # Odin Blue: navy and ice blue, Odin 2's own
        "font": ALDRICH, "label_font": ALDRICH, "title_spacing": 2.0,
        "chassis": ((14, 24, 33), (7, 12, 17)), "grain": 5, "brushed": 1,
        "head": ((30, 44, 56), (16, 25, 33)), "head_line": (47, 86, 108), "head_label": (128, 152, 168),
        "logo": "odin", "logo_ink": (196, 222, 238),
        "panel": "odin", "panel_edge": (3, 7, 10), "panel_hi": (74, 122, 150), "radius": 12, "shadow": 150,
        "title": (168, 214, 242), "title_band": None, "title_rule": (36, 66, 84), "accent": (60, 155, 199),
        "plate": (4, 14, 22, 120), "plate_edge": (70, 130, 166, 120), "plate_r": 7,
        "box": (5, 20, 29), "box_edge": (46, 104, 134), "box_r": 6, "arrow": (110, 196, 238),
        "caption": (138, 160, 174), "caption_hi": TEXT_BLUE,
        "ui": ui(ring=(92, 196, 244), value=(134, 203, 246), ink=(226, 231, 234),
                 lit_top=(30, 56, 72), lit_bot=(14, 30, 40), lit_edge=(74, 156, 204), lit_text=(134, 203, 246),
                 btn_top=(48, 51, 55), btn_bot=(24, 26, 29), btn_text=(196, 202, 206),
                 glass=(5, 18, 26), glass_edge=(46, 104, 134), track=(36, 46, 54)),
    },
    {   # 2000: warm grey and charcoal, red keys, a green-lit LCD
        "font": TITILLIUM, "label_font": TITILLIUM_SB, "title_spacing": 2.5,
        "chassis": ((206, 206, 202), (188, 188, 184)), "grain": 5, "brushed": 1,
        "head": ((214, 214, 210), (194, 194, 190)), "head_line": (110, 110, 108), "head_label": (40, 40, 40),
        "logo": "text", "logo_ink": (36, 36, 35), "logo_dot": (200, 54, 40),
        "panel": ((198, 198, 194), (180, 180, 176)), "panel_edge": (96, 96, 94), "panel_hi": (240, 240, 236),
        "radius": 5, "shadow": 80,
        "title": (244, 244, 240), "title_band": (40, 41, 43), "title_rule": None, "accent": (226, 44, 34),
        "plate": (22, 28, 24, 205), "plate_edge": (30, 34, 32, 230), "plate_r": 3,
        "box": (30, 38, 32), "box_edge": (78, 88, 76), "box_r": 3, "arrow": (184, 222, 146),
        "caption": (36, 36, 36), "caption_hi": (190, 30, 22),
        "ui": ui(ring=(236, 52, 38), value=(168, 238, 118), ink=(240, 240, 236),
                 lit_top=(206, 46, 34), lit_bot=(150, 28, 20), lit_edge=(255, 112, 92), lit_text=(255, 255, 255),
                 btn_top=(74, 74, 72), btn_bot=(50, 50, 48), btn_text=(230, 228, 222),
                 glass=(24, 32, 26), glass_edge=(78, 88, 76), track=(58, 58, 56), alpha=195),
    },
]
LOOKS += [
    {   # Arctic: white and brushed silver, signal-orange keys and rings, dark ink
        "font": TITILLIUM, "label_font": TITILLIUM_SB, "title_spacing": 2.5,
        "chassis": ((236, 238, 241), (218, 221, 226)), "grain": 4, "brushed": 10,
        "head": ((248, 249, 250), (228, 231, 235)), "head_line": (176, 182, 190), "head_label": (86, 92, 100),
        "logo": "text", "logo_ink": (30, 34, 40), "logo_dot": (255, 102, 0),
        "panel": ((250, 251, 252), (234, 236, 240)), "panel_edge": (168, 174, 184), "panel_hi": (255, 255, 255),
        "radius": 10, "shadow": 60,
        "title": (30, 34, 40), "title_band": None, "title_rule": (206, 210, 216), "accent": (255, 102, 0),
        "plate": (196, 203, 213, 120), "plate_edge": (160, 168, 180, 200), "plate_r": 6,
        "box": (220, 225, 232), "box_edge": (160, 168, 180), "box_r": 6, "arrow": (240, 96, 0),
        "caption": (70, 76, 86), "caption_hi": (226, 88, 0),
        "ui": ui(ring=(255, 106, 0), value=(196, 66, 0), ink=(28, 32, 38),
                 lit_top=(255, 130, 34), lit_bot=(236, 96, 6), lit_edge=(255, 176, 100), lit_text=(255, 255, 255),
                 btn_top=(255, 255, 255), btn_bot=(222, 226, 232), btn_text=(40, 44, 52),
                 glass=(214, 220, 228), glass_edge=(168, 176, 188), track=(208, 213, 220), alpha=170),
    },
    {   # Sunburst: an orange chassis, charcoal modules, white names and orange values
        "font": TITILLIUM, "label_font": TITILLIUM_SB, "title_spacing": 2.5,
        "chassis": ((246, 124, 30), (226, 98, 14)), "grain": 4, "brushed": 1,
        "head": ((252, 134, 38), (234, 108, 20)), "head_line": (150, 58, 6), "head_label": (54, 22, 4),
        "logo": "text", "logo_ink": (26, 14, 6), "logo_dot": (255, 255, 255),
        "panel": ((40, 40, 43), (26, 26, 28)), "panel_edge": (120, 46, 4), "panel_hi": (96, 96, 100),
        "radius": 10, "shadow": 120,
        "title": (255, 255, 255), "title_band": None, "title_rule": (70, 70, 74), "accent": (255, 128, 30),
        "plate": (0, 0, 0, 110), "plate_edge": (255, 140, 50, 90), "plate_r": 6,
        "box": (20, 20, 22), "box_edge": (110, 70, 40), "box_r": 6, "arrow": (255, 140, 40),
        "caption": (176, 176, 180), "caption_hi": (255, 150, 60),
        "ui": ui(ring=(255, 132, 28), value=(255, 166, 74), ink=(244, 244, 246),
                 lit_top=(255, 128, 30), lit_bot=(220, 94, 10), lit_edge=(255, 180, 110), lit_text=(255, 255, 255),
                 btn_top=(62, 62, 66), btn_bot=(36, 36, 39), btn_text=(236, 236, 240),
                 glass=(16, 16, 18), glass_edge=(96, 66, 44), track=(66, 66, 70), alpha=150),
    },
]
for _lk in LOOKS:   # what a look's glass lies on: its panel colour
    _p = _lk["panel"] if _lk["panel"] != "odin" else ((20, 40, 54), (12, 24, 32))
    _lk["ui"]["under"] = mix(_p[0], _p[1], 0.5)
# the controls of mode-dependent parts (the dark insets at the bottom of the OSC and FILTER columns): one set for
# every look, in neutral graphite and silver (a widget takes one when= condition, the mode)
NEUTRAL = ui(ring=(206, 212, 218), value=(214, 220, 224), ink=(226, 230, 232),
             lit_top=(70, 76, 82), lit_bot=(44, 48, 52), lit_edge=(206, 212, 218), lit_text=(255, 255, 255),
             btn_top=(52, 55, 59), btn_bot=(30, 32, 35), btn_text=(200, 205, 210),
             glass=(20, 22, 25), glass_edge=(62, 66, 72), track=(50, 54, 58), alpha=255)
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
        self.chips = []      # (x0, y0, x1, y1, look index): a look's colour chip (the BROWSE look picker)
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


def hexc(c):
    return "%02x%02x%02x" % tuple(c[:3])


def themed(make, when=None):
    """one layout line per look, each shown only in its look (when=theme:N) and drawn in its colours; a control
    that depends on a mode (when=) is one neutral line instead"""
    if when:
        line(make(NEUTRAL, "n") + wh(when))
        return
    for t, look in enumerate(LOOKS):
        line(make(look["ui"], t) + " when=theme:%d" % t)


_IMGS = {}


def themed_img(name, draw, u, t):
    """an image drawn for one look's palette (cached per name and look)"""
    key = (name, t)
    if key not in _IMGS:
        _IMGS[key] = save(draw(u), "%s_t%s.png" % (name, t))
    return _IMGS[key]


_STRIPS = {}


def knob(cx, cy, key, size="big", when=None, plate=True):
    r, src, body, bw = KNOB[size]
    bip = key in BIPOLAR

    def make(u, t):
        k = (size, bip, t)
        if k not in _STRIPS:
            _STRIPS[k] = arc_knob_strip(src, body, r, "knob_%s%s_t%s.png" % (size, "_bip" if bip else "", t), bip, u)
        return 'knob cx=%d cy=%d r=%d label="" key=%s strip=%s ns=%d vs=%d bw=%d ink=%s ink_dim=%s' % (
            cx, Y(cy), r, key, _STRIPS[k], NS, VS, bw, hexc(u["ink"]), hexc(u["value"]))
    themed(make, when)
    if plate:
        _, _, p0, p1 = knob_geom(size)
        PAGE.plates.append((cx - bw // 2 + 3, cy + p0, cx + bw // 2 - 3, cy + p1))


def popup(cx, cy, w, h, key, when=None, cols=None):
    """a popup field: drawn here (field=none), unless it depends on a mode (when=: the builder draws it into that
    mode's own image, so it must sit on an inset)"""
    c = " cols=%d" % cols if cols else ""
    if when:
        line('popup cx=%d cy=%d w=%d h=%d label="" key=%s%s accent=%s%s' % (cx, Y(cy), w, h, key, c,
                                                                            hexc(NEUTRAL["value"]), wh(when)))
    else:
        themed(lambda u, t: 'popup cx=%d cy=%d w=%d h=%d label="" key=%s field=none accent=%s%s' % (
            cx, Y(cy), w, h, key, hexc(u["value"]), c))
        PAGE.boxes.append((cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2, True))


def readout(cx, cy, w, h, key, tsize):
    themed(lambda u, t: 'readout cx=%d cy=%d w=%d h=%d label="" key=%s box=0 tsize=%d tcolor=%s' % (
        cx, Y(cy), w, h, key, tsize, hexc(u["value"])))
    PAGE.boxes.append((cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2, False))


def toggle_img(cx, cy, key, pair_, w, h, when=None):
    """pair_: (name, text, size) of a text button, drawn in each look's colours"""
    name, text, size = pair_

    def make(u, t):
        off = themed_img(name, lambda u_: text_button(text, False, w, h, size, ui_=u_), u, t)
        on = themed_img(name + "_on", lambda u_: text_button(text, True, w, h, size, ui_=u_), u, t)
        return 'toggle cx=%d cy=%d label="" key=%s img=%s img_on=%s ns=0 w=%d h=%d' % (cx, Y(cy), key, off, on, w, h)
    themed(make, when)


def button_img(cx, cy, key, name, draw_off, draw_on, w, h):
    """a trigger button with its own pictures (off / pressed), per look"""
    def make(u, t):
        return 'button cx=%d cy=%d label="" key=%s img=%s img_on=%s w=%d h=%d' % (
            cx, Y(cy), key, themed_img(name, draw_off, u, t), themed_img(name + "_on", draw_on, u, t), w, h)
    themed(make)


def seg(kind, cx, cy, key, extra, w, h, when=None):
    """option buttons (enum_h / enum_v) on graphite button pictures; the names are drawn on top (skin.css)"""
    def make(u, t):
        off = themed_img("seg_%dx%d" % (w, h), lambda u_: text_button("", False, w, h, 10, ui_=u_), u, t)
        on = themed_img("seg_%dx%d_on" % (w, h), lambda u_: text_button("", True, w, h, 10, ui_=u_, fill=True), u, t)
        return '%s cx=%d cy=%d label="" key=%s %s img=%s img_on=%s' % (kind, cx, Y(cy), key, extra, off, on)
    # one neutral set in every look: the kit names an option group's pictures after its parameter, so per-look copies
    # of one group would overwrite each other
    line(make(NEUTRAL, "n") + wh(when))


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


_INSET_IMG = []


def inset_image():
    """the mode inset's glass, as a picture of its own: shown only for the types that have controls in it"""
    if not _INSET_IMG:
        k, w, h = 3, SW - 16, INSET[1] - INSET[0]
        m = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0))
        d = ImageDraw.Draw(m)
        d.rounded_rectangle((0, 0, w * k - 1, h * k - 1), INSET_R * k, fill=(6, 9, 12, 170), outline=(255, 255, 255, 40), width=k)
        d.line((INSET_R * k, k, (w - INSET_R) * k, k), fill=(255, 255, 255, 20), width=k)
        _INSET_IMG.append(save(m.resize((w, h), Image.LANCZOS), "inset.png"))
    return _INSET_IMG[0]


def typed_column(sx, title, type_key, with_inset):
    """a column with a type picker under its title; its inset at the bottom only for the types in with_inset"""
    panel(sx, SW, title)
    type_picker(sx, type_key)
    opts = PARAMS[type_key]["options"]
    # no dark inset under the type picker (removed 2026-11-06 at the user's request): the controls sit on the panel


# ------------------------------------------------------------------------------------------------- control images
FRAMES = 129   # knob filmstrip frames, as Odin draws them (the kit makes every strip 128)


def arc_knob_strip(src, body, r, name, bipolar, u):
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
    # the knob body's centre in its frame (Odin's frames also hold a drop shadow below it): put that on the ring's centre
    bx0, by0, bx1, by1 = strip.crop((0, 0, fw, fh)).convert("RGBA").getchannel("A").point(
        lambda a: 255 if a >= 200 else 0).getbbox()
    bcx, bcy = (bx0 + bx1) / 2.0 * scale, (by0 + by1) / 2.0 * scale
    out = Image.new("RGBA", (s, s * FRAMES), (0, 0, 0, 0))
    for n in range(FRAMES):
        t = n / (FRAMES - 1.0)
        i = round(t * 128)
        big = Image.new("RGBA", (s * k, s * k), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        box = [(s / 2 - ring) * k, (s / 2 - ring) * k, (s / 2 + ring) * k, (s / 2 + ring) * k]
        d.arc(box, 135, 405, fill=(6, 9, 11, 255), width=(width + 2) * k)
        d.arc(box, 135, 405, fill=rgba(u["track"]), width=width * k)
        a0, a1 = (270, 135 + 270 * t) if bipolar else (135, 135 + 270 * t)
        if abs(a1 - a0) > 0.5:
            d.arc(box, min(a0, a1), max(a0, a1), fill=rgba(u["ring"]), width=width * k)
        tile = big.resize((s, s), Image.LANCZOS)
        f = strip.crop((0, i * fh, fw, (i + 1) * fh))
        if scale != 1.0:
            f = f.resize((max(1, round(fw * scale)), max(1, round(fh * scale))), Image.LANCZOS)
        ox = int(round(s / 2.0 - bcx))
        oy = int(round(s / 2.0 - bcy))
        layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        layer.paste(f, (ox, oy), f)
        tile.alpha_composite(layer)
        out.paste(tile, (0, n * s))
    return save(out, name)


def text_button(text, on, w, h, size, color=None, ui_=None, fill=False):
    """a raised button with crisp text in a look's colours: graphite when off; lit in the look's own colour when on
    (fill: a solid lit face, for option buttons whose names are drawn on top)"""
    u = ui_ or LOOKS[0]["ui"]
    k = 3
    b = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0))
    face_top, face_bot = u["lit"] if on else u["btn"]
    b.paste(vgradient(w * k, h * k, face_top, face_bot), (0, 0), rmask(w * k, h * k, 7 * k))
    d = ImageDraw.Draw(b)
    d.rounded_rectangle((0, 0, w * k - 1, h * k - 1), 7 * k, outline=(5, 7, 9, 255), width=2 * k)
    d.rounded_rectangle((2 * k, 2 * k, w * k - 1 - 2 * k, h * k - 1 - 2 * k), 5 * k,
                        outline=(rgba(u["lit_edge"], 230) if on else (255, 255, 255, 34)), width=(2 if on else 1) * k)
    if text:
        c = color or (u["lit_text"] if on else u["btn_text"])
        f = font(ALDRICH, size * k)
        if on and not color:
            glow = Image.new("RGBA", b.size, (0, 0, 0, 0))
            ImageDraw.Draw(glow).text((w * k / 2, h * k / 2), text, font=f, fill=rgba(u["lit_edge"]), anchor="mm")
            b.alpha_composite(glow.filter(ImageFilter.GaussianBlur(4 * k)))
        d.text((w * k / 2, h * k / 2 + k), text, font=f, fill=rgba(c), anchor="mm")
    return b.resize((w, h), Image.LANCZOS)


def pair(name, text, w, h, size=18):
    """a text toggle's name, caption and text size (toggle_img draws it per look)"""
    return (name, text, size)


def arrow_button(direction, pressed, s=40, h=46, ui_=None):
    k = 3
    u = ui_ or LOOKS[0]["ui"]
    big = text_button("", pressed, s, h, 10, ui_=u).resize((s * k, h * k), Image.LANCZOS)
    d = ImageDraw.Draw(big)
    c, cy, a = s * k / 2, h * k / 2, 8 * k
    pts = [(c - a * 0.6, cy - a), (c - a * 0.6, cy + a), (c + a * 0.85, cy)] if direction > 0 else \
          [(c + a * 0.6, cy - a), (c + a * 0.6, cy + a), (c - a * 0.85, cy)]
    d.polygon(pts, fill=rgba(u["lit_text"] if pressed else u["btn_text"]))
    return big.resize((s, h), Image.LANCZOS)


def cpu_meter_strip(w, h, u, t):
    """21 frames, 0..100 % in 5 % steps: a segmented bar, green to amber to red, with the number"""
    frames = 21
    k = 2
    out = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for i in range(frames):
        pct = i * 5
        f = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0))
        d = ImageDraw.Draw(f)
        d.rounded_rectangle((0, 0, w * k - 1, h * k - 1), 6 * k, fill=rgba(u["glass"]), outline=rgba(u["glass_edge"]), width=k)
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
        tcol = (236, 84, 62) if pct >= 80 else u["value"]
        d.text((25 * k, h * k / 2), "%d" % pct, font=font(ALDRICH, 19 * k), fill=tcol + (255,), anchor="mm")
        out.paste(f.resize((w, h), Image.LANCZOS), (0, i * h))
    return save(out, "cpu_meter_t%s.png" % t)


def card(on, w, h, u):
    """a BROWSE list tile in a look's display glass; the selected one lifted, edged and marked in its colour"""
    k = 3
    g = u["glass"]
    top, bot = (mix(g, u["lit_edge"], 0.30), mix(g, u["lit_edge"], 0.16)) if on else (mix(g, (255, 255, 255), 0.05), g)
    # glass over the look's panel, blended here: the kit lays list tiles on black, so they must be opaque
    a = min(255, u["alpha"] + (60 if on else 0)) / 255.0
    under = u.get("under", (0, 0, 0))
    c = vgradient(w * k, h * k, mix(under, top, a), mix(under, bot, a))
    d = ImageDraw.Draw(c)
    d.rectangle((0, 0, w * k - 1, h * k - 1), outline=(0, 0, 0, 120), width=k)
    if on:
        d.rectangle((k, k, w * k - 1 - k, h * k - 1 - k), outline=rgba(u["bar"]), width=2 * k)
        d.rectangle((4 * k, 7 * k, 7 * k, h * k - 8 * k), fill=rgba(u["bar"]))
    else:
        d.rectangle((k, k, w * k - 1 - k, h * k - 1 - k), outline=rgba(u["glass_edge"], 150), width=k)
    return c.resize((w, h), Image.LANCZOS)


# ------------------------------------------------------------------------------------------------- drawing a look
def draw_panel(look, x, y, w, h, title):
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
    d = ImageDraw.Draw(body)
    if look["title_band"] is not None and title:
        d.rounded_rectangle((3 * k, 3 * k, pw - 3 * k, TITLE_H * k), max(1, r - 2 * k), fill=rgba(look["title_band"]))
        d.rectangle((3 * k, (TITLE_H - 8) * k, pw - 3 * k, TITLE_H * k), fill=rgba(look["title_band"]))
        d.rectangle((3 * k, TITLE_H * k, 40 * k, TITLE_H * k + 2 * k), fill=rgba(look["accent"]))
    if look["title_rule"] is not None and title:
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


def draw_glass(x0, y0, x1, y1, r, tint, edge, sheen=22):
    """smoked glass: the look's tint over the panel (the panel shows through), a soft light across its top half, a
    hairline highlight under the top edge and a fine edge"""
    k = SS
    w, h = (x1 - x0) * k, (y1 - y0) * k
    layer = Image.new("RGBA", (w + 1, h + 1), (0, 0, 0, 0))
    ImageDraw.Draw(layer).rounded_rectangle((0, 0, w, h), r * k, fill=tuple(tint))
    shine = Image.new("RGBA", (w + 1, h + 1), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shine)
    for yy in range(0, h // 2, k):
        sd.line((0, yy, w, yy), fill=(255, 255, 255, int(sheen * (1 - yy / (h / 2.0)))), width=k)
    sd.line((r * k, k, w - r * k, k), fill=(255, 255, 255, sheen * 2), width=k)
    m = Image.new("L", (w + 1, h + 1), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w, h), r * k, fill=255)
    shine.putalpha(ImageChops.multiply(shine.getchannel("A"), m))
    layer.alpha_composite(shine)
    ImageDraw.Draw(layer).rounded_rectangle((0, 0, w, h), r * k, outline=tuple(edge), width=k)
    CANVAS.alpha_composite(layer, (x0 * k, y0 * k))


def draw_inset(x0, y0, x1, y1, look=None):
    """a mode inset: deeper glass (its controls are the neutral set, so the tint is neutral too)"""
    draw_glass(x0, y0, x1, y1, INSET_R, (6, 9, 12, 170), (255, 255, 255, 40), sheen=10)


def draw_plate(look, x0, y0, x1, y1):
    draw_glass(x0, y0, x1, y1, look["plate_r"], look["plate"], look["plate_edge"])


def draw_box(look, x0, y0, x1, y1, arrow):
    k = SS
    draw_glass(x0, y0, x1, y1, look["box_r"], look["plate"], rgba(look["box_edge"], 200))
    d = ImageDraw.Draw(CANVAS)
    if arrow:
        ax, ay, a = (x1 - 18) * k, (y0 + y1) / 2 * k, 6 * k
        d.line(((x1 - 36) * k, (y0 + 7) * k, (x1 - 36) * k, (y1 - 7) * k), fill=rgba(look["box_edge"]), width=k)
        d.polygon([(ax - a, ay - a * 0.55), (ax + a, ay - a * 0.55), (ax, ay + a * 0.75)], fill=rgba(look["arrow"]))


def draw_chip(x0, y0, x1, y1, t):
    """look t in a chip: its chassis, a module in its panel colour, and a bar in its knob colour"""
    lk, k = LOOKS[t], SS
    w, h = (x1 - x0) * k, (y1 - y0) * k
    c = vgradient(w, h, lk["chassis"][0], lk["chassis"][1])
    pan = lk["panel"] if lk["panel"] != "odin" else ((22, 40, 54), (12, 22, 30))
    d = ImageDraw.Draw(c)
    d.rectangle((w * 0.30, h * 0.22, w - 3 * k, h - 3 * k), fill=rgba(mix(pan[0], pan[1], 0.5)))
    d.rectangle((w * 0.30, h * 0.22, w - 3 * k, h * 0.22 + 3 * k), fill=rgba(lk["ui"]["ring"]))
    d.ellipse((w * 0.48, h * 0.42, w * 0.48 + h * 0.44, h * 0.86), outline=rgba(lk["ui"]["ring"]), width=2 * k)
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), 5 * k, fill=255)
    CANVAS.paste(c, (x0 * k, y0 * k), m)
    ImageDraw.Draw(CANVAS).rounded_rectangle((x0 * k, y0 * k, x1 * k - 1, y1 * k - 1), 5 * k,
                                              outline=(0, 0, 0, 150), width=k)


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
        spaced(d, (x * k, y * k), t, font(look["label_font"], 20 * k), rgba(col), 1.2 * k, anchor)


CANVAS = None


def render(page, look):
    """the page's background in one look (W x H)"""
    global CANVAS
    k = SS
    CANVAS = vgradient(W * k, H * k, look["chassis"][0], look["chassis"][1])
    CANVAS.alpha_composite(noise(W * k, H * k, look["grain"], seed=3, stretch=look["brushed"]))
    draw_header(look)
    for x, y, w, h, title in page.panels:
        draw_panel(look, x, y, w, h, title)
    for p in page.plates:
        draw_plate(look, *p)
    for p in page.insets:
        draw_inset(*p)
    for b in page.boxes:
        draw_box(look, *b)
    for j in page.jewels:
        draw_jewel(*j)
    for c in page.chips:
        draw_chip(*c)
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
    left = (lambda u: arrow_button(-1, False, ui_=u), lambda u: arrow_button(-1, True, ui_=u))
    right = (lambda u: arrow_button(1, False, ui_=u), lambda u: arrow_button(1, True, ui_=u))
    button_img(338, 46, "bank_prev", "arrow_l", left[0], left[1], 40, 46)
    readout(448, 46, 170, 46, "bank_name", 24)
    button_img(558, 46, "bank_next", "arrow_r", right[0], right[1], 40, 46)
    button_img(610, 46, "preset_prev", "arrow_l", left[0], left[1], 40, 46)
    readout(761, 46, 254, 46, "preset_name", 24)
    button_img(912, 46, "preset_next", "arrow_r", right[0], right[1], 40, 46)
    readout(976, 46, 72, 46, "voices", 28)
    themed(lambda u, t: 'meter cx=1078 cy=%d w=120 h=42 key=cpu strip=%s frames=21' % (
        Y(46), CPU[t]))
    button_img(1205, 46, "panic", "stop", stop_off, stop_on, 118, 50)


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
    seg("enum_v", RX + RW // 2, TOP + 148, "playmode", "sw=204 sh=44", 204, 44)
    label(RX + RW // 2, TOP + 240, "UNISON")
    popup(RX + RW // 2, TOP + 276, 204, 42, "unison")
    knob(RX + 66, SPREAD_ROWS[1], "amp_vel")
    knob(RX + 190, SPREAD_ROWS[1], "bend")
    toggle_img(RX + RW // 2, BOT - 40, "arp_on", ARP_TG, 210, 48)
    line('qlinks "MAIN" = master,glide,uni_detune,uni_width,fil1_freq,fil1_res,fil1_env,fil1_kbd,'
         'env1_a,env1_d,env1_s,env1_r,delay_wet,delay_fb,reverb_mix,reverb_size')
    finish(PAGE)


def tab_osc():
    new_page("OSC")
    mid = (INSET[0] + INSET[1]) // 2
    for n, sx in zip((1, 2, 3), SX):
        o = "osc%d" % n
        typed_column(sx, "OSC %d" % n, o + "_type",
                     ("analog", "wavetable", "multi", "vector", "chiptune", "fm", "pm", "noise"))
        four(sx, [o + "_vol", o + "_shape", o + "_oct", o + "_semi"], TYPED_ROWS)
        a, b = qcol(sx)
        seg("enum_h", sx + SW // 2, mid, o + "_wave", "sw=106 rows=2", 106, 40, when=o + "_type:analog")
        for mode in ("wavetable", "multi"):
            popup(sx + SW // 2, mid, SW - 44, 44, o + "_table", when="%s_type:%s" % (o, mode), cols=3)
        knob(a, TYPED_R3, o + "_vecy", "small", when=o + "_type:vector", plate=False)
        popup(a + 26, mid, 150, 44, o + "_chip", when=o + "_type:chiptune")
        toggle_img(b + 28, mid, o + "_chipnoise", NOISE_TG, 72, 44, when=o + "_type:chiptune")
        for mode in ("fm", "pm"):
            knob(a, TYPED_R3, o + "_cratio", "small", when="%s_type:%s" % (o, mode), plate=False)
            knob(b, TYPED_R3, o + "_mratio", "small", when="%s_type:%s" % (o, mode), plate=False)
        knob(a, TYPED_R3, o + "_hp", "small", when=o + "_type:noise", plate=False)
    panel(SX[3], SW, "FINE TUNE")
    four(SX[3], ["osc1_fine", "osc2_fine", "osc3_fine", "tune"], TYPED_ROWS)
    panel(RX, RW, "VOICE")
    # on the oscillators' grid: the voices where their type pickers are, the knobs on their first knob row
    label(RX + 18, TOP + TITLE_H + 23, "VOICES", anchor="lm")
    popup(RX + RW - 14 - 64, TOP + TITLE_H + 23, 128, 36, "unison")
    knob(RX + 66, TYPED_ROWS[0], "uni_detune")
    knob(RX + 190, TYPED_ROWS[0], "uni_width")
    knob(RX + 66, TYPED_ROWS[1], "glide")
    knob(RX + 190, TYPED_ROWS[1], "bend")
    line('qlinks "OSC" = osc1_vol,osc1_shape,osc1_oct,osc1_semi,osc2_vol,osc2_shape,osc2_oct,osc2_semi,'
         'osc3_vol,osc3_shape,osc3_oct,osc3_semi,osc1_fine,osc2_fine,osc3_fine,tune')
    finish(PAGE)


def tab_filter():
    new_page("FILTER")
    for n, sx in zip((1, 2, 3), SX):
        f = "fil%d" % n
        typed_column(sx, "FILTER %d" % n, f + "_type",
                     [o.lower() for o in PARAMS[f + "_type"]["options"] if o != "Bypass"])
        four(sx, [f + "_freq", f + "_res", f + "_env", f + "_kbd"], TYPED_ROWS)
        a, b = qcol(sx)
        for o in PARAMS[f + "_type"]["options"][1:]:   # velocity: with the inset, for every type but Bypass
            knob(a, TYPED_R3, f + "_vel", "small", when="%s_type:%s" % (f, o.lower()), plate=False)
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
    seg("enum_h", RX + RW // 2, y + 98, "dist_algo", "sw=78 sh=42", 78, 42)
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


def fx_column(sx, title, on_key, keys, w=SW, alone=True):
    """an effect: its on switch in the title, two knobs a row; a third alone on its row is centred (alone=False:
    something else takes the place beside it)"""
    panel(sx, w, title)
    toggle_img(sx + w - 56, TOP + TITLE_H // 2 + 1, on_key, ON_TG, 84, 32)
    a, b = (sx + 63, sx + w - 63)
    knob(a, SPREAD_ROWS[0], keys[0])
    knob(b, SPREAD_ROWS[0], keys[1])
    knob(sx + w // 2 if alone else a, SPREAD_ROWS[1], keys[2])


def tab_fx():
    new_page("FX")
    fx_column(SX[0], "DELAY", "delay_on", ["delay_time", "delay_fb", "delay_wet"], alone=False)
    toggle_img(qcol(SX[0])[1], SPREAD_ROWS[1], "delay_pp", PP_TG, 112, 46)
    fx_column(SX[1], "REVERB", "reverb_on", ["reverb_size", "reverb_damp", "reverb_mix"])
    fx_column(SX[2], "CHORUS", "chorus_on", ["chorus_rate", "chorus_amt", "chorus_mix"])
    fx_column(SX[3], "PHASER", "phaser_on", ["phaser_rate", "phaser_amt", "phaser_mix"])
    fx_column(RX, "FLANGER", "flanger_on", ["flanger_rate", "flanger_amt", "flanger_mix"], w=RW)
    line('qlinks "FX" = delay_time,delay_fb,delay_wet,-,reverb_size,reverb_damp,reverb_mix,-,'
         'chorus_rate,chorus_amt,chorus_mix,-,phaser_rate,phaser_amt,phaser_mix,-')
    finish(PAGE)


LOOK_BAR = 70   # BROWSE: the look picker's bar along the bottom


def tab_browse():
    """the patch browser: the banks (patch categories) on the left, the bank's patches on the right; along the bottom
    the look picker, one button per look with a chip of its colours"""
    new_page("BROWSE")
    ph = PH - LOOK_BAR - 6
    bx, bw = 8, 372
    panel(bx, bw, "BANKS", h=ph)
    # the bank and the patch: each a simple < name > selector (the same arrows as the top bar), not a list
    def selector(x, w, prev_key, next_key, name_key, cy):
        left = (lambda u: arrow_button(-1, False, ui_=u), lambda u: arrow_button(-1, True, ui_=u))
        right = (lambda u: arrow_button(1, False, ui_=u), lambda u: arrow_button(1, True, ui_=u))
        cx = x + w // 2
        button_img(x + 36, cy, prev_key, "arrow_l", left[0], left[1], 40, 46)
        readout(cx, cy, w - 120, 46, name_key, 28)
        button_img(x + w - 36, cy, next_key, "arrow_r", right[0], right[1], 40, 46)

    selector(bx, bw, "bank_prev", "bank_next", "bank_name", TOP + 80)
    px, pw = 388, W - 8 - 388
    panel(px, pw, "PATCHES", h=ph)
    selector(px, pw, "preset_prev", "preset_next", "preset_name", TOP + 80)
    # the look picker: a bar with one button per look; a window in each button shows that look's chip
    y0 = BOT - LOOK_BAR
    panel(8, W - 16, "", y=y0, h=LOOK_BAR)
    label(26, y0 + LOOK_BAR // 2, "LOOK", anchor="lm")
    n, sh, gap = len(LOOKS), 46, 2
    sw = min(280, (W - 16 - 110 - 14 - (n - 1) * gap) // n)   # the buttons fill the bar
    cx, cy = 8 + 110 + (W - 16 - 110 - 14) // 2, y0 + LOOK_BAR // 2
    x0 = cx - (n * sw + (n - 1) * gap) // 2
    win = (6, 10, 6 + 22, sh - 10)

    def face(on):
        b = text_button("", on, sw, sh, 10, ui_=NEUTRAL)
        m = Image.new("L", b.size, 255)
        ImageDraw.Draw(m).rounded_rectangle((win[0] - 1, win[1] - 1, win[2], win[3]), 5, fill=0)
        b.putalpha(ImageChops.multiply(b.getchannel("A"), m))
        return b
    line('enum_h cx=%d cy=%d label="" key=theme sw=%d sh=%d img=%s img_on=%s' % (
        cx, Y(cy), sw, sh, save(face(False), "look_seg.png"), save(face(True), "look_seg_on.png")))
    for t in range(n):
        sx = x0 + t * (sw + gap)
        PAGE.chips.append((sx + win[0], cy - sh // 2 + win[1], sx + win[2], cy - sh // 2 + win[3], t))
    line('qlinks "BROWSE" = browse_bank,browse_patch,-,-,-,-,-,-,-,-,-,-,-,-,-,-')   # banks, patches: each on its own
    finish(PAGE)


def main():
    global LED_OFF, LED_ON
    global NOISE_TG, ARP_TG, DIST_TG, ON_TG, PP_TG, DELAY_TG, REVERB_TG, ROUTE
    random.seed(1)
    if os.path.isdir(GEN):
        for f in os.listdir(GEN):
            os.remove(os.path.join(GEN, f))
    global CPU, stop_off, stop_on

    def stop_off(u):
        return text_button("STOP", False, 118, 50, 24, (240, 108, 92), ui_=u)

    def stop_on(u):
        b = text_button("STOP", True, 118, 50, 24, (255, 255, 255), ui_=u)
        red = Image.new("RGBA", b.size, (210, 44, 32, 150))
        red.putalpha(ImageChops.multiply(red.getchannel("A"), rmask(118, 50, 7)))
        b.alpha_composite(red)
        return b
    CPU = [cpu_meter_strip(120, 42, look["ui"], t) for t, look in enumerate(LOOKS)]
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
        # what no look draws itself (open popup lists, mode-dependent parts): neutral graphite and silver
        "theme_bg=0e1215", "theme_ink=e2e6e8", "theme_ink_dim=d6dce0", "theme_accent=d6dce0",
        "theme_accent_hi=ffffff", "theme_line=05070a", "theme_lcd=0c1318", "theme_box=11171b",
        "theme_seg_active=5a6168", "theme_seg_inactive=1a1e22", "theme_seg_active_tx=ffffff",
        "theme_btn_bg=2a2f34", "theme_btn_text=e2e6e8", "theme_tile_on=3a4148",
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
