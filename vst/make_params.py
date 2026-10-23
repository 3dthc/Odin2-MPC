#!/usr/bin/env python3
"""The curated Odin 2 controls the MPC sees, in one place.

Writes:
  vst/params.json           the plugin's parameter list (mpc-vst-plugins tools/params.py format), VST index = order
  src/odin_mpc_params.inc   the engine's table: which Odin parameter / tree property each control drives and how

Every control maps onto Odin's own state, so a factory preset sets them all and saving a project keeps Odin's
full patch. Kinds:
  norm   an Odin float parameter; the MPC control is 0..1 along Odin's own knob travel (its skewed range), the
         engine shows the real value ("dynamic_display")
  int    an Odin whole-number parameter, same range
  tog    an Odin on/off parameter, shown Off/On
  topt   an Odin tree property (patch data that is not automatable in Odin), as a list of named values
  tint   an Odin tree property, whole numbers
  plus a few of the plugin's own: preset, preset_name, polyphony, play mode, panic, cpu

NEVER reorder or remove entries once a version has been used in a project: projects store values by index.
Append new controls at the end. Run this script after any change (build.sh does).
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

OSC_TYPES = [("Off", 1), ("Analog", 2), ("Wavetable", 3), ("Multi", 4), ("Vector", 5), ("Chiptune", 6), ("FM", 7),
             ("PM", 8), ("Noise", 15), ("WaveDraw", 20), ("ChipDraw", 21), ("SpecDraw", 22)]
ANALOG_WAVES = [("Saw", 0), ("Pulse", 1), ("Triangle", 2), ("Sine", 3)]
WAVETABLES = ["Classic Analog", "Nasal", "Reed", "Glass", "FM", "BrokenSine", "Harsh", "Mean", "Rectangular",
              "Bitreduced", "Strings", "Piano", "Organ", "Oboe", "Trumpet", "MixedWaves A", "MixedWaves B",
              "MixedWaves C", "MixedWaves D", "Voice A", "Voice B", "Voice C", "Voice D", "Additive A", "Additive B",
              "Additive C", "Additive D", "Overtones A", "Overtones B", "Harmonics A", "Harmonics B", "MutantSaw A",
              "MutantSaw B", "MutantSquare A", "MutantSquare B"]
CHIPWAVES = [("Pulse 50", 1), ("Pulse 25", 2), ("Pulse 12.5", 3), ("Triangle", 4), ("Saw", 5), ("Sine", 6)]
FILTER_TYPES = [("Bypass", 1), ("LP 24", 2), ("LP 12", 3), ("BP 24", 4), ("BP 12", 5), ("HP 24", 6), ("HP 12", 7),
                ("SEM 12", 15), ("Diode", 19), ("Korg LP", 24), ("Korg HP", 25), ("Comb", 30), ("Formant", 35),
                ("RingMod", 40)]
LFO_WAVES = ["Sine", "Saw", "Triangle", "Square 50", "Square 25", "Square 12", "Peak", "S&H", "Pyramid 4",
             "Pyramid 6", "Pyramid 8", "Pyramid 12", "Stair 3", "Stair 4", "Stair 6", "Stair 8", "Stair 12",
             "WaveDraw 1", "WaveDraw 2", "WaveDraw 3", "ChipDraw 1", "ChipDraw 2", "ChipDraw 3"]
DIST_ALGOS = [("Clamp", 1), ("Fold", 2), ("Zero", 3)]
UNISON = [("1", 1), ("2", 2), ("3", 3), ("4", 4), ("6", 6), ("12", 12)]
PLAY_MODES = ["Legato", "Poly", "Retrig"]   # tree "legato" = 0, 1, 2 (Odin's PLAYMODETOVALUETREE)

# fmt (how the engine prints a norm value): hz, sec, db, pct, bip (-100..100 %), ct (cents), x (plain number)
P = []   # (key, name, kind, target, extra)


def norm(key, name, odin_id, fmt):
    P.append((key, name, "norm", odin_id, {"fmt": fmt}))


def integer(key, name, odin_id, lo, hi):
    P.append((key, name, "int", odin_id, {"min": lo, "max": hi}))


def tog(key, name, odin_id):
    P.append((key, name, "tog", odin_id, {}))


def topt(key, name, tree, prop, opts):
    """opts: [(label, stored value)]"""
    P.append((key, name, "topt", tree + "." + prop, {"opts": opts}))


def tint(key, name, tree, prop, lo, hi):
    P.append((key, name, "tint", tree + "." + prop, {"min": lo, "max": hi}))


def special(key, name, **kw):
    P.append((key, name, "special", "", kw))


# ---- header (on every page)
special("polyphony", "Polyphony", display="int")
special("preset", "Preset", display="int")
special("preset_name", "Patch", display="string")
special("preset_prev", "Prev Patch", momentary=True, step_of="preset", step_delta=-1)
special("preset_next", "Next Patch", momentary=True, step_of="preset", step_delta=1)
special("panic", "Stop", momentary=True)
special("cpu", "CPU")

# ---- oscillators
for n in (1, 2, 3):
    o = "osc%d" % n
    topt(o + "_type", "Osc%d Type" % n, "osc", o + "_type", OSC_TYPES)
    integer(o + "_oct", "Osc%d Octave" % n, o + "_oct", -4, 4)
    integer(o + "_semi", "Osc%d Semi" % n, o + "_semi", -12, 12)
    norm(o + "_fine", "Osc%d Fine" % n, o + "_fine", "ct")
    norm(o + "_vol", "Osc%d Volume" % n, o + "_vol", "db")
    topt(o + "_wave", "Osc%d Wave" % n, "osc", o + "_analog_wave", ANALOG_WAVES)
    norm(o + "_pw", "Osc%d PW" % n, o + "_pulsewidth", "pct")
    norm(o + "_drift", "Osc%d Drift" % n, o + "_drift", "pct")
    topt(o + "_table", "Osc%d Table" % n, "osc", o + "_wavetable", [(w, i + 1) for i, w in enumerate(WAVETABLES)])
    norm(o + "_pos", "Osc%d Position" % n, o + "_position", "pct")
    norm(o + "_posmod", "Osc%d Pos Mod" % n, o + "_pos_mod", "bip")
    norm(o + "_mpos", "Osc%d Multi Pos" % n, o + "_multi_position", "pct")
    norm(o + "_spread", "Osc%d Spread" % n, o + "_spread", "pct")
    norm(o + "_detune", "Osc%d Detune" % n, o + "_detune", "pct")
    norm(o + "_vecx", "Osc%d Vector X" % n, o + "_vec_x", "pct")
    norm(o + "_vecy", "Osc%d Vector Y" % n, o + "_vec_y", "pct")
    topt(o + "_chip", "Osc%d Chip Wave" % n, "osc", o + "_chipwave", CHIPWAVES)
    tog(o + "_chipnoise", "Osc%d Chip Noise" % n, o + "_chipnoise")
    norm(o + "_fm", "Osc%d FM Amount" % n, o + "_fm", "pct")
    tint(o + "_cratio", "Osc%d Carrier" % n, "osc", o + "_carrier_ratio", 1, 16)
    tint(o + "_mratio", "Osc%d Modulator" % n, "osc", o + "_modulator_ratio", 1, 16)
    norm(o + "_lp", "Osc%d Noise LP" % n, o + "_lp", "hz")
    norm(o + "_hp", "Osc%d Noise HP" % n, o + "_hp", "hz")

# ---- filters (1 and 2 per voice, 3 on the stereo sum)
for n in (1, 2, 3):
    f = "fil%d" % n
    topt(f + "_type", "Filter%d Type" % n, "misc", f + "_type", FILTER_TYPES)
    norm(f + "_freq", "Filter%d Freq" % n, f + "_freq", "hz")
    norm(f + "_res", "Filter%d Reso" % n, f + "_res", "pct")
    norm(f + "_env", "Filter%d Env" % n, f + "_env", "bip")
    norm(f + "_kbd", "Filter%d Kbd" % n, f + "_kbd", "pct")
    norm(f + "_vel", "Filter%d Vel" % n, f + "_vel", "pct")
    norm(f + "_gain", "Filter%d Gain" % n, f + "_gain", "db")
    norm(f + "_sat", "Filter%d Saturation" % n, f + "_saturation", "pct")
    norm(f + "_sem", "Filter%d SEM Shape" % n, f + "_sem_transition", "bip")
    norm(f + "_formant", "Filter%d Vowel" % n, f + "_formant_transition", "pct")
    norm(f + "_ring", "Filter%d Ring Amt" % n, f + "_ring_mod_amount", "pct")

# ---- routing
for n in (1, 2):
    for s in (1, 2, 3):
        tog("fil%d_osc%d" % (n, s), "Osc%d > Fil%d" % (s, n), "fil%d_osc%d" % (n, s))
tog("fil2_fil1", "Fil1 > Fil2", "fil2_fil1")
tog("fil1_amp", "Fil1 > Amp", "fil1_to_amp")
tog("fil2_amp", "Fil2 > Amp", "fil2_to_amp")

# ---- amp and distortion
norm("amp_gain", "Amp Gain", "amp_gain", "db")
norm("amp_pan", "Amp Pan", "amp_pan", "bip")
norm("amp_vel", "Amp Velocity", "amp_velocity", "pct")
topt("dist_on", "Distortion", "misc", "dist_on", [("Off", 0), ("On", 1)])
topt("dist_algo", "Dist Type", "misc", "dist_algo", DIST_ALGOS)
norm("dist_boost", "Dist Boost", "dist_boost", "pct")
norm("dist_mix", "Dist Mix", "dist_drywet", "pct")

# ---- envelopes and LFOs
for n, label in ((1, "Amp"), (2, "Filter"), (3, "Mod")):
    e = "env%d" % n
    norm(e + "_a", "%s Env A" % label, e + "_attack", "sec")
    norm(e + "_d", "%s Env D" % label, e + "_decay", "sec")
    norm(e + "_s", "%s Env S" % label, e + "_sustain", "pct")
    norm(e + "_r", "%s Env R" % label, e + "_release", "sec")
for n in (1, 2, 3, 4):
    l = "lfo%d" % n
    label = "Global LFO" if n == 4 else "LFO%d" % n
    norm(l + "_rate", label + " Rate", l + "_freq", "hz")
    topt(l + "_wave", label + " Wave", "lfo", l + "_wave", [(w, i) for i, w in enumerate(LFO_WAVES)])

# ---- effects
tog("delay_on", "Delay", "delay_on")
norm("delay_time", "Delay Time", "delay_time", "sec")
norm("delay_fb", "Delay Feedback", "delay_feedback", "pct")
norm("delay_wet", "Delay Wet", "delay_wet", "pct")
tog("delay_pp", "Ping Pong", "delay_pingpong")
tog("chorus_on", "Chorus", "chorus_on")
norm("chorus_rate", "Chorus Rate", "chorus_rate", "hz")
norm("chorus_amt", "Chorus Amount", "chorus_amount", "pct")
norm("chorus_mix", "Chorus Mix", "chorus_drywet", "pct")
tog("phaser_on", "Phaser", "phaser_on")
norm("phaser_rate", "Phaser Rate", "phaser_rate", "hz")
norm("phaser_amt", "Phaser Amount", "phaser_mod", "pct")
norm("phaser_mix", "Phaser Mix", "phaser_drywet", "pct")
tog("flanger_on", "Flanger", "flanger_on")
norm("flanger_rate", "Flanger Rate", "flanger_rate", "hz")
norm("flanger_amt", "Flanger Amount", "flanger_amount", "pct")
norm("flanger_mix", "Flanger Mix", "flanger_drywet", "pct")
tog("reverb_on", "Reverb", "reverb_on")
norm("reverb_size", "Reverb Hall", "rev_mid_hall", "x")
norm("reverb_damp", "Reverb Damp", "rev_hf_damp", "hz")
norm("reverb_mix", "Reverb Mix", "rev_drywet", "pct")

# ---- voice
norm("master", "Master", "master", "db")
norm("glide", "Glide", "glide", "pct")
special("playmode", "Play Mode", options=PLAY_MODES)
topt("unison", "Unison", "misc", "unison_voices", UNISON)
norm("uni_detune", "Uni Detune", "unison_detune", "x")
norm("uni_width", "Uni Width", "unison_width", "pct")
tint("bend", "Bend Range", "misc", "pitchbend_amount", 0, 24)
tog("arp_on", "Arpeggiator", "arp_on")

# ---- added in 1.1 (appended: projects store values by position)
special("eco", "ECO", options=["Off", "Auto", "Eco", "Lite"])
special("eco_active", "ECO Active", options=["Off", "On"])

# ---- added in 1.2: one knob per oscillator for its type's main control (PW, position, FM amount, ...), so a Q-Link
# always has something to turn whatever the type; the engine names it after what it drives
for n in (1, 2, 3):
    special("osc%d_shape" % n, "Osc%d Shape" % n)
special("voices", "Voices")   # read-only: how many voices the budget lets sound now (1.3)
special("tune", "Master Tune")   # A4 = 415..465 Hz, 440 in the middle; the plugin's, kept across patches (1.3)


def build():
    params, rows = [], []
    for key, name, kind, target, ex in P:
        p = {"key": key, "name": name}
        if kind == "norm":
            p.update({"min": 0.0, "max": 1.0, "dynamic_display": True})
        elif kind == "int":
            p.update({"min": ex["min"], "max": ex["max"], "display": "int"})
        elif kind == "tog":
            p.update({"options": ["Off", "On"]})
        elif kind == "topt":
            p.update({"options": [o[0] for o in ex["opts"]]})
        elif kind == "tint":
            p.update({"min": ex["min"], "max": ex["max"], "display": "int"})
        else:
            if key == "preset":
                p.update({"min": 0, "max": 9999, "display": "int", "nudge_pct": 10})   # max rewritten from the bank
            elif key == "preset_name":
                p.update({"display": "string", "poll": False})
            elif key in ("preset_prev", "preset_next"):
                p.update({"momentary": True, "step_of": "preset", "step_delta": ex["step_delta"]})
            elif key == "panic":
                p.update({"momentary": True, "hold_ms": 250})
            elif key == "cpu":
                p.update({"min": 0, "max": 100, "display": "int"})
            elif key == "polyphony":   # 1.3: the voice ceiling, no longer on the page (the budget sets voices)
                p.update({"min": 1, "max": 24, "display": "int", "default": 16})
            elif key == "voices":
                p.update({"display": "string"})
            elif key == "tune":
                p.update({"min": 0.0, "max": 1.0, "default": 0.5, "dynamic_display": True})
            elif key == "playmode":
                p.update({"options": ex["options"], "default": 1})
            elif key == "eco":
                p.update({"options": ex["options"], "default": 1})
            elif key == "eco_active":   # read-only: lights while ECO is changing what plays
                p.update({"options": ex["options"]})
            elif key.endswith("_shape"):
                p.update({"min": 0.0, "max": 1.0, "dynamic_display": True, "dynamic_name": True})
        params.append(p)
        rows.append((key, kind, target, ex))
    return params, rows


def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    params, rows = build()
    bank = json.load(open(os.path.join(ROOT, "src", "presets", "bank.json")))
    for p in params:
        if p["key"] == "preset":
            p["max"] = len(bank["presets"]) - 1
    # text that follows another param asks MPC to re-read the screen when that param is written (mpc-vst-plugins
    # 2026-10-21: plain host writes no longer do): the shape knob's name and value follow its oscillator's type, and the
    # unpolled preset name follows the preset number
    for p in params:
        if p["key"] in ("osc1_type", "osc2_type", "osc3_type", "preset"):
            p["refresh_display"] = True
    out = {"name": "Odin 2", "params": params}
    with open(os.path.join(HERE, "params.json"), "w") as f:
        json.dump(out, f, indent=1)
        f.write("\n")

    lines = ["/* Generated by vst/make_params.py: the controls the MPC sees and the Odin state each one drives. */",
             "/* key, kind, Odin parameter id or tree.property, option values (topt), min, max, print format */"]
    for key, kind, target, ex in rows:
        opts = ex.get("opts")
        vals = "{" + ", ".join(str(v) for _, v in opts) + "}" if opts else "{}"
        lo, hi = ex.get("min", 0), ex.get("max", 0)
        fmt = ex.get("fmt", "")
        lines.append("    {%s, K_%s, %s, %d, %s, %d, %d, %s}," % (c_str(key), kind.upper(), c_str(target),
                                                                  len(opts) if opts else 0, vals, lo, hi, c_str(fmt)))
    with open(os.path.join(ROOT, "src", "odin_mpc_params.inc"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("%d parameters" % len(params))


if __name__ == "__main__":
    main()
