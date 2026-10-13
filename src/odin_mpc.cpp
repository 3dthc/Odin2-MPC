/* odin2-mpc: Odin 2 as an MPC OS plugin engine (mpc-vst-plugins wrapper/engine.h).
 *
 * The wrapper (vst2_wrap.c) turns this into a VST2 instrument for MPC's built-in plugin host. This file connects
 * it to Odin's own processor: the curated controls in odin_mpc_params.inc each drive one Odin parameter or patch
 * property, the factory bank is compiled in (src/presets/bank_data.inc), and the project state is the preset it
 * started from plus the lines that changed since (odin_state.h).
 *
 * Extras the MPC page has that Odin's GUI does not: a STOP button that silences voices and effect tails at once, a
 * CPU meter (the share of the 128-frame block budget this instance spends rendering, averaged over about 0.1 s and
 * shown a few times a second), a voice budget, and ECO.
 *
 * Voice budget (1.3, no voices knob): a patch may sound as many voices as fit in VOICE_BUDGET % of the block, at most
 * VOICE_CEILING. It starts from the patch's measured cost (bank_cost.inc) and follows what voices really cost while
 * playing. Under it a chord thins unison stacks before it drops notes; if the load still nears the limit the oldest
 * note fades out in 15 ms.
 *
 * ECO makes voices cheaper:
 *   Off   each voice as Odin plays it
 *   Auto  modulation, oscillator pitch and filter cutoff at control rate (every 8 samples); when Odin's load stays
 *         high, new notes start at most 3 unison voices (2 under heavy load), back to full when the load has been
 *         low for 1.5 s; notes already sounding are never changed (default)
 *   Eco   control rate every 16 samples, new notes start at most 3 unison voices
 *   Lite  as Eco with at most 2, and the per-voice distortion without its 3x oversampling
 *
 * GPL-3.0-or-later, as Odin 2. */
#include "odin_state.h"

extern "C" {
#include "engine.h"
}

#include <cmath>
#include <ctime>
#include <new>
#include <unordered_map>

namespace {

enum Kind { K_NORM, K_INT, K_TOG, K_TOPT, K_TINT, K_SPECIAL };

struct Control {
    const char *key;
    Kind kind;
    const char *target;   // Odin parameter id, or "tree.property"
    int nopts;
    int vals[40];         // K_TOPT: the stored value of each option
    int lo, hi;
    const char *fmt;      // K_NORM: how to print the value
};

const Control CONTROLS[] = {
#include "odin_mpc_params.inc"
};
const int NCONTROLS = sizeof CONTROLS / sizeof CONTROLS[0];

/* OSC SHAPE: the Odin parameter (suffix after "oscN") an oscillator type's one main knob drives, and its name */
struct Shape {
    int type;
    const char *suffix, *name;
};
const Shape SHAPES[] = {
    {OSC_TYPE_ANALOG, "_pulsewidth", "PW"},   {OSC_TYPE_WAVETABLE, "_position", "POSITION"},
    {OSC_TYPE_MULTI, "_multi_position", "POSITION"}, {OSC_TYPE_VECTOR, "_vec_x", "VECTOR X"},
    {OSC_TYPE_FM, "_fm", "FM AMOUNT"},     {OSC_TYPE_PM, "_fm", "PM AMOUNT"},
    {OSC_TYPE_NOISE, "_lp", "LOWPASS"},
};

struct BankEntry {
    const char *category, *name, *state;
};
const BankEntry BANK[] = {
#include "presets/bank_data.inc"
};
const int NBANK = sizeof BANK / sizeof BANK[0];

/* Each factory patch's measured load on the MPC Live II (tools/patch_cost.sh, calibrated on the device): tenths of a
 * percent of the block budget per sounding voice, at audio rate (ECO Off) and at control rate (the other modes), and
 * with no voice sounding (effects). The voice budget starts from these and then follows what it measures. */
struct BankCost {
    unsigned short voiceOff, voiceCtrl, base;
};
const BankCost BANK_COST[] = {
#include "presets/bank_cost.inc"
};
static_assert(sizeof BANK_COST / sizeof BANK_COST[0] == NBANK, "bank_cost.inc doesn't match the bank");

/* Voices: Odin has 24; this plugin lets at most VOICE_CEILING sound (the "polyphony" control, kept for automation
 * and old projects, no longer on the page), and fewer when the patch is heavy: as many as fit in VOICE_BUDGET % of
 * the block. If the load still gets close to the limit, the oldest note is faded out in 15 ms. */
#ifndef ODIN_MPC_NO_BUDGET
const int VOICE_CEILING = 16;
#else   // tools: measure patches, or compare with Odin, without the budget (all 24 voices, nothing shed)
const int VOICE_CEILING = 24;
#endif
const float VOICE_BUDGET = 75.f;
const float SHED_LOAD = 92.f;
/* control rate: modulation, oscillator pitch and filter cutoff worked out every 8 samples (5.5 kHz) in Auto, every
 * 16 (2.8 kHz) in Eco and Lite; about a quarter and a third less work, very close to the sound at audio rate */
const int CTRL_RATE_AUTO = 8;
const int CTRL_RATE_ECO = 16;

const int MAX_MIDI = 256;
const float BLOCK_SECONDS_PER_FRAME = 1.f / 44100.f;

double nowSeconds() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int16_t toS16(float f) {
    f *= 32768.f;
    if (f >= 32767.f) return 32767;
    if (f <= -32768.f) return -32768;
    return (int16_t)lrintf(f);
}

struct Instance {
    OdinAudioProcessor odin;
    std::unordered_map<std::string, int> index;   // control key -> CONTROLS index
    int preset = 0;
    OdinMPC::Lines presetState;                    // snapshot right after the current preset was loaded

    uint8_t midi[MAX_MIDI][3];
    int midiFrame[MAX_MIDI];
    int nmidi = 0;

    float left[128], right[128];

    // CPU meter and ECO
    float cpuAvg = 0;      // percent of the block budget, averaged over ~0.1 s
    float cpuHeld = 0;     // cpuAvg, peak-held, falling slowly
    int cpuShown = 0;      // the value the page shows (5 % steps)
    long framesSinceShow = 0;
    unsigned displayRev = 0;
    enum { ECO_OFF, ECO_AUTO, ECO_ECO, ECO_LITE };
    int eco = ECO_AUTO;
    int autoCap = 12;      // ECO Auto: the unison cap the load asks for now (12 = none)
    long lowFrames = 0;    // ECO Auto: how long the load has been low

    // voice budget
    int ceiling = VOICE_CEILING;   // the most voices ever ("polyphony")
    int voiceLimit = VOICE_CEILING;   // the most voices now
    float voiceCost = 5.f;   // estimated % of the block per sounding voice
    float baseCost = 3.f;    // estimated % of the block with no voice sounding
    float hintCost = 5.f;    // the patch's measured cost per voice (bank_cost.inc)
    float fastLoad = 0;      // the load over the last few blocks
    long raiseFrames = 0;    // how long the budget has allowed more voices than voiceLimit
    long shedHold = 0;       // frames until the next voice may be shed
    int lastActive = 0;
    float testLoad = 0;      // tools/engine_test: pretend the load is this much higher
    bool testFixed = false;  // tools/engine_test: the budget sees testLoad alone (any machine's speed)

    // master tune: A4 in Hz (TUNE_LO..TUNE_HI, 0.1 Hz steps), for new notes; the plugin's, not the patch's
    static constexpr float TUNE_LO = 415.f, TUNE_HI = 465.f;
    float tuneHz = 440.f;
    void setTune(float hz) {
        hz = hz < TUNE_LO ? TUNE_LO : hz > TUNE_HI ? TUNE_HI : hz;
        tuneHz = std::round(hz * 10.f) / 10.f;
        odin.m_master_tune = tuneHz == 440.f ? 1.f : tuneHz / 440.f;
    }

    Instance() {
        for (int i = 0; i < NCONTROLS; i++) index[CONTROLS[i].key] = i;
        odin.prepare(44100.f);
        loadPreset(0);
        applyEco();
    }

    // ---- presets
    OdinMPC::Lines initState;   // Odin's init patch, as a snapshot (the bank stores differences from it)

    /* Load bank preset n: work out its whole state (init patch plus its stored differences) and apply only what
     * differs from the current state, so a preset that keeps the drawn wavetables doesn't rebuild them. */
    void loadPreset(int n) {
        if (n < 0 || n >= NBANK) return;
        odin.panic();
        if (initState.empty()) {
            odin.loadPatchFromMemory(BinaryData::init_patch_odin, BinaryData::init_patch_odinSize);
            initState = OdinMPC::snapshot(odin);
        }
        OdinMPC::Lines target = OdinMPC::overlay(initState, OdinMPC::parse(BANK[n].state));
        OdinMPC::apply(odin, OdinMPC::diff(OdinMPC::snapshot(odin), target));
        odin.m_value_tree.state.getChildWithName("misc").setProperty("patch_name", String(BANK[n].name), nullptr);
        odin.resetAudioEngine();   // as Odin does after a patch loads (voices, filters, LFOs, envelopes)
        preset = n;
        presetState = target;
        startBudget();
        ++displayRev;   // every control may have moved
    }

    // ---- project state: "preset=<n>" then what changed since that preset was loaded
    std::string saveState() {
        OdinMPC::Lines changed = OdinMPC::diff(presetState, OdinMPC::snapshot(odin));
        std::string s = "odin2mpc=1\npreset=" + std::to_string(preset) + "\n";
        if (ceiling != VOICE_CEILING) s += "voices=" + std::to_string(ceiling) + "\n";
        if (eco != ECO_AUTO) s += "eco=" + std::to_string(eco) + "\n";
        if (tuneHz != 440.f) {
            char t[32];
            snprintf(t, sizeof t, "tune=%.1f\n", tuneHz);
            s += t;
        }
        // the wrapper's chunk holds 8 KB: everything else first, then drawn wavetables if they still fit
        for (int pass = 0; pass < 2; pass++)
            for (auto &kv : changed) {
                if ((kv.first.compare(0, 5, "draw.") == 0) != (pass == 1)) continue;
                std::string line = kv.first + "=" + kv.second + "\n";
                if (s.size() + line.size() > 8000) continue;
                s += line;
            }
        return s;
    }

    void loadState(const char *text) {
        OdinMPC::Lines lines = OdinMPC::parse(text);
        int n = 0, poly = VOICE_CEILING, e = ECO_AUTO;   // a state saved before ECO existed: Auto
        float tune = 440.f;
        OdinMPC::Lines rest;
        for (auto &kv : lines) {
            if (kv.first == "odin2mpc") continue;
            else if (kv.first == "preset") n = atoi(kv.second.c_str());
            else if (kv.first == "polyphony") continue;   // 1.0-1.2's VOICES knob: the budget does this now
            else if (kv.first == "voices") poly = atoi(kv.second.c_str());
            else if (kv.first == "eco") e = atoi(kv.second.c_str());
            else if (kv.first == "tune") tune = (float)atof(kv.second.c_str());
            else rest.push_back(kv);
        }
        loadPreset(n);
        if (!rest.empty()) OdinMPC::apply(odin, rest);
        setPolyphony(poly);
        setEco(e);
        setTune(tune);
        ++displayRev;
    }

    // ---- ECO
    void setEco(int e) {
        if (e < ECO_OFF || e > ECO_LITE) return;
        bool rate = (eco == ECO_OFF) != (e == ECO_OFF);
        eco = e;
        applyEco();
        if (rate) startBudget();   // audio rate <-> control rate: start from the patch's cost for the new one
    }

    int ecoCap() const {
        switch (eco) {
        case ECO_ECO: return 3;
        case ECO_LITE: return 2;
        case ECO_AUTO: return autoCap;   // 12, 3, 2 or 1 by load
        default: return 12;
        }
    }

    bool ecoActive() const { return ecoCap() < 12 || eco == ECO_LITE; }

    void applyEco() {
        bool was = ecoLight;
        applyUnisonCap();
        odin.m_ctrl_rate = eco == ECO_OFF ? 1 : eco == ECO_AUTO ? CTRL_RATE_AUTO : CTRL_RATE_ECO;
        odin.m_unison_fit = eco != ECO_OFF;
        odin.m_voice_manager.m_steal_from_stacks = eco != ECO_OFF;
        if ((eco == ECO_LITE) != distLite) {
            distLite = eco == ECO_LITE;
            odin.setDistortionLite(distLite);
        }
        ecoLight = ecoActive();
        if (was != ecoLight) ++displayRev;   // the ECO light
    }
    bool distLite = false, ecoLight = false;

    /* a new note's unison: ECO's cap, and never more voices than the budget allows */
    void applyUnisonCap() {
        int cap = ecoCap();
        odin.m_unison_cap = cap < voiceLimit ? cap : voiceLimit;
    }

    /* Auto: from the averaged load, with hysteresis; only the cap for notes that start from now on changes */
    void autoEco(int frames) {
        int want = autoCap;
        // (above the voice budget's VOICE_BUDGET %: this is for when the budget alone hasn't kept the load down)
        if (cpuAvg > 100.f) want = 1;
        else if (cpuAvg > 95.f && autoCap > 2) want = 2;
        else if (cpuAvg > 85.f && autoCap > 3) want = 3;
        if (cpuAvg < 50.f) {
            lowFrames += frames;
            if (lowFrames > 66150) {   // 1.5 s of low load: back to the full patch
                want = 12;
                lowFrames = 0;
            }
        } else {
            lowFrames = 0;
        }
        if (want != autoCap) {
            autoCap = want;
            if (eco == ECO_AUTO) applyEco();
        }
    }

    // ---- voices
    void setPolyphony(int v) {   // the ceiling
        ceiling = v < 1 ? 1 : v > VOICES ? VOICES : v;
        if (voiceLimit > ceiling) setLimit(ceiling);
    }

    void setLimit(int v) {
        v = v < 1 ? 1 : v > ceiling ? ceiling : v;
        if (v == voiceLimit) return;
        voiceLimit = v;
        odin.m_voice_manager.m_voice_limit = v;   // new notes; voices above it finish as they would
        applyUnisonCap();
        ++displayRev;
    }

    int budgetVoices() const {
        float room = VOICE_BUDGET - baseCost;
        int v = (int)(room / (voiceCost > 0.1f ? voiceCost : 0.1f));
        return v < 1 ? 1 : v;
    }

    /* after a patch loads: start from its measured costs */
    void startBudget() {
        const BankCost &c = BANK_COST[preset];
        hintCost = voiceCost = (eco == ECO_OFF ? c.voiceOff : c.voiceCtrl) / 10.f;
        baseCost = c.base / 10.f;
        raiseFrames = 0;
        int v = budgetVoices();
#ifdef ODIN_MPC_NO_BUDGET
        v = ceiling;
#endif
        voiceLimit = -1;   // apply even if unchanged
        setLimit(v);
    }

    int activeVoices() const {
        int n = 0;
        for (int i = 0; i < VOICES; i++) n += odin.m_voice[i] ? 1 : 0;
        return n;
    }

    /* Follow the measured load: per-voice and base cost estimates, the limit from them (down at once, up one voice
     * per half second), and if the load still gets close to the limit, fade out the oldest note. */
    void budget(float pct, int frames) {
        int active = activeVoices();
        lastActive = active;
#ifdef ODIN_MPC_NO_BUDGET
        return;
#endif
        float a = frames / 4410.f;   // ~0.1 s
        if (a > 1.f) a = 1.f;
        if (active == 0) {
            baseCost += (pct - baseCost) * a;
            voiceCost += (hintCost - voiceCost) * a * 0.1f;   // back toward the patch's measured cost (~1 s)
        } else {
            float per = (pct - baseCost) / active;
            if (per < 0.1f) per = 0.1f;
            voiceCost += (per - voiceCost) * a * 0.5f;
        }
        int want = budgetVoices();
        if (want < voiceLimit) {
            setLimit(want);
            raiseFrames = 0;
        } else if (want > voiceLimit && voiceLimit < ceiling) {
            raiseFrames += frames;
            if (raiseFrames > 11025) {   // a quarter second with room: half the way up
                setLimit(voiceLimit + (want - voiceLimit + 1) / 2);
                raiseFrames = 0;
            }
        } else {
            raiseFrames = 0;
        }

        fastLoad += (pct - fastLoad) * 0.35f;
        if (shedHold > 0) {
            shedHold -= frames;
        } else if (fastLoad > SHED_LOAD && active > 1) {
            shedOldest();
            shedHold = 1323;   // 30 ms: let it fade and the load settle
        }
    }

    /* fade out the oldest note (all its unison voices): one already released first, else the oldest held */
    void shedOldest() {
        auto &vm = odin.m_voice_manager;
        for (int pass = 0; pass < 2; pass++)
            for (int age = VOICES - 1; age >= 0; age--) {
                int v = vm.voiceByAge(age);
                Voice &voice = odin.m_voice[v];
                if (!voice || voice.env[0].isFastReleasing()) continue;
                if (pass == 0 && !voice.isInRelease()) continue;
                int key = voice.m_MIDI_key;
                bool released = voice.isInRelease();
                for (int i = 0; i < VOICES; i++) {
                    Voice &o = odin.m_voice[i];
                    if (o && o.m_MIDI_key == key && o.isInRelease() == released && !o.env[0].isFastReleasing())
                        odin.fastReleaseVoice(i);
                }
                return;
            }
    }

    // ---- one control
    RangedAudioParameter *param(const Control &c) { return odin.m_value_tree.getParameter(c.target); }

    /* OSC SHAPE: the parameter oscillator n's main knob drives for its current type (none: nullptr) */
    const Shape *shapeOf(int n) {
        int type = (int)odin.m_value_tree.state.getChildWithName("osc")[Identifier("osc" + std::to_string(n) + "_type")];
        for (const Shape &sh : SHAPES)
            if (sh.type == type) return &sh;
        return nullptr;
    }
    RangedAudioParameter *shapeParam(int n) {
        const Shape *sh = shapeOf(n);
        return sh ? odin.m_value_tree.getParameter("osc" + std::to_string(n) + sh->suffix) : nullptr;
    }
    static int shapeOsc(const std::string &k) {   // "osc2_shape" -> 2, else 0
        return k.size() == 10 && k.compare(0, 3, "osc") == 0 && k.compare(4, 6, "_shape") == 0 ? k[3] - '0' : 0;
    }

    ValueTree tree(const Control &c, std::string &prop) {
        std::string t = c.target;
        size_t dot = t.find('.');
        prop = t.substr(dot + 1);
        return odin.m_value_tree.state.getChildWithName(t.substr(0, dot).c_str());
    }

    void set(const Control &c, float v) {
        switch (c.kind) {
        case K_NORM: {
            RangedAudioParameter *p = param(c);
            if (p) odin.m_value_tree.setValue(p, p->convertFrom0to1(v));
            break;
        }
        case K_INT:
        case K_TOG: {
            RangedAudioParameter *p = param(c);
            if (p) odin.m_value_tree.setValue(p, std::round(v));
            break;
        }
        case K_TOPT: {
            int i = (int)std::lround(v);
            if (i < 0 || i >= c.nopts) return;
            std::string prop;
            ValueTree t = tree(c, prop);
            t.setProperty(Identifier(prop), var(c.vals[i]), nullptr);
            if (prop.size() == 8 && prop.compare(4, 4, "type") == 0) ++displayRev;   // oscN_type: its SHAPE knob moved
            break;
        }
        case K_TINT: {
            int i = (int)std::lround(v);
            i = i < c.lo ? c.lo : i > c.hi ? c.hi : i;
            std::string prop;
            ValueTree t = tree(c, prop);
            t.setProperty(Identifier(prop), var(i), nullptr);
            break;
        }
        case K_SPECIAL: {
            std::string k = c.key;
            if (k == "preset") loadPreset((int)std::lround(v));
            else if (k == "panic" && v > 0.5f) odin.panic();
            else if (k == "polyphony") setPolyphony((int)std::lround(v));
            else if (k == "eco") setEco((int)std::lround(v));
            else if (k == "tune") setTune(TUNE_LO + v * (TUNE_HI - TUNE_LO));
            else if (int n = shapeOsc(k)) {
                RangedAudioParameter *p = shapeParam(n);
                if (p) odin.m_value_tree.setValue(p, p->convertFrom0to1(v));
            }
            else if (k == "playmode") {
                int i = (int)std::lround(v);
                if (i < 0 || i > 2) return;
                odin.m_value_tree.state.getChildWithName("misc").setProperty("legato", i, nullptr);
                odin.setMonoPolyLegato(VALUETREETOPLAYMODE(i));
            }
            break;
        }
        }
    }

    /* the value the wrapper reads back, in the units it sends (0..1, a whole number, or an option index) */
    float get(const Control &c) {
        switch (c.kind) {
        case K_NORM: {
            RangedAudioParameter *p = param(c);
            return p ? p->convertTo0to1(p->value.load()) : 0.f;
        }
        case K_INT:
        case K_TOG: {
            RangedAudioParameter *p = param(c);
            return p ? p->value.load() : 0.f;
        }
        case K_TOPT: {
            std::string prop;
            ValueTree t = tree(c, prop);
            int stored = (int)std::lround((double)t[Identifier(prop)]);
            for (int i = 0; i < c.nopts; i++)
                if (c.vals[i] == stored) return (float)i;
            return 0.f;
        }
        case K_TINT: {
            std::string prop;
            ValueTree t = tree(c, prop);
            return (float)std::lround((double)t[Identifier(prop)]);
        }
        case K_SPECIAL: {
            std::string k = c.key;
            if (k == "preset") return (float)preset;
            if (k == "polyphony") return (float)ceiling;
            if (k == "voices") return (float)voiceLimit;
            if (k == "tune") return (tuneHz - TUNE_LO) / (TUNE_HI - TUNE_LO);
            if (k == "cpu") return (float)cpuShown;
            if (k == "eco") return (float)eco;
            if (k == "eco_active") return ecoActive() ? 1.f : 0.f;
            if (int n = shapeOsc(k)) {
                RangedAudioParameter *p = shapeParam(n);
                return p ? p->convertTo0to1(p->value.load()) : 0.f;
            }
            if (k == "playmode") return (float)(int)odin.m_value_tree.state.getChildWithName("misc")["legato"];
            return 0.f;
        }
        }
        return 0.f;
    }

    /* the value text of a K_NORM control, in Odin's units */
    std::string display(const Control &c) {
        RangedAudioParameter *p = param(c);
        if (!p) return "";
        float v = p->value.load();
        char b[32];
        std::string f = c.fmt;
        if (f == "hz") {
            if (v >= 1000.f) snprintf(b, sizeof b, "%.2f kHz", v / 1000.f);
            else if (v >= 100.f) snprintf(b, sizeof b, "%.0f Hz", v);
            else if (v >= 10.f) snprintf(b, sizeof b, "%.1f Hz", v);
            else snprintf(b, sizeof b, "%.2f Hz", v);
        } else if (f == "sec") {
            if (v < 1.f) snprintf(b, sizeof b, "%.0f ms", v * 1000.f);
            else snprintf(b, sizeof b, "%.2f s", v);
        } else if (f == "db") {
            if (v <= p->range.start + 0.05f) snprintf(b, sizeof b, "-inf dB");
            else snprintf(b, sizeof b, "%+.1f dB", v);
        } else if (f == "pct") {
            snprintf(b, sizeof b, "%.0f %%", v * 100.f);
        } else if (f == "bip") {
            snprintf(b, sizeof b, "%+.0f %%", v * 100.f);
        } else if (f == "ct") {
            snprintf(b, sizeof b, "%+.0f ct", v);
        } else {
            snprintf(b, sizeof b, "%.2f", v);
        }
        return b;
    }

    // ---- audio
    void render(int16_t *out, int frames) {
        double t0 = nowSeconds();
        odin.processStereo(left, right, frames, midi, midiFrame, nmidi);
        nmidi = 0;
        double spent = nowSeconds() - t0;
        for (int i = 0; i < frames; i++) {
            out[2 * i] = toS16(left[i]);
            out[2 * i + 1] = toS16(right[i]);
        }
        float pct = (float)(spent / (frames * BLOCK_SECONDS_PER_FRAME) * 100.0) + testLoad;
        meter(pct, frames);
        budget(testFixed ? testLoad : pct, frames);
    }

    /* The load averaged over ~0.1 s (so one slow block, e.g. 24 voices starting together, doesn't read as an
     * overload), peak-held and falling about 25 % a second, shown in 5 % steps at most 4 times a second: the page
     * redraws rarely (an animated picture costs MPC's screen thread a lot, mpc-vst-plugins docs/NOTES.md). */
    void meter(float pct, int frames) {
        float a = frames / 4410.f;
        cpuAvg += (pct - cpuAvg) * (a > 1.f ? 1.f : a);
        if (cpuAvg > cpuHeld) cpuHeld = cpuAvg;
        else cpuHeld -= 25.f * frames * BLOCK_SECONDS_PER_FRAME;
        if (cpuHeld < 0) cpuHeld = 0;
        autoEco(frames);
        framesSinceShow += frames;
        if (framesSinceShow < 11025) return;
        int shown = (int)(cpuHeld / 5.f + 0.5f) * 5;
        if (shown > 100) shown = 100;
        if (shown != cpuShown) {
            cpuShown = shown;
            ++displayRev;
            framesSinceShow = 0;
        }
    }
};

// ---- the engine interface
void *create(const char *) {
    return new (std::nothrow) Instance();
}

void destroy(void *inst) {
    delete (Instance *)inst;
}

void midi(void *inst, const uint8_t *msg, int len) {
    Instance *s = (Instance *)inst;
    if (len < 1 || s->nmidi >= MAX_MIDI) return;
    uint8_t st = msg[0];
    if (st < 0x80 || st >= 0xf0) return;   // channel messages only
    s->midi[s->nmidi][0] = st;
    s->midi[s->nmidi][1] = len > 1 ? msg[1] : 0;
    s->midi[s->nmidi][2] = len > 2 ? msg[2] : 0;
    s->midiFrame[s->nmidi] = 0;
    s->nmidi++;
}

void set_param(void *inst, const char *key, const char *val) {
    Instance *s = (Instance *)inst;
    if (!strcmp(key, "state")) {
        s->loadState(val);
        return;
    }
    if (!strcmp(key, "budget_test_load")) {   // tools/engine_test
        s->testLoad = (float)atof(val);
        return;
    }
    if (!strcmp(key, "budget_test_fixed")) {   // tools/engine_test
        s->testFixed = atoi(val) != 0;
        return;
    }
    if (!strcmp(key, "lfo_bpm")) {   // the host tempo (vst.json HAS_LFO_BPM)
        float bpm = (float)atof(val);
        if (bpm > 0) s->odin.m_BPM = bpm;
        return;
    }
    auto it = s->index.find(key);
    if (it == s->index.end()) return;
    s->set(CONTROLS[it->second], (float)atof(val));
}

int get_param(void *inst, const char *key, char *buf, int len) {
    Instance *s = (Instance *)inst;
    if (!strcmp(key, "state")) return snprintf(buf, len, "%s", s->saveState().c_str());
    if (!strcmp(key, "display_rev")) return snprintf(buf, len, "%u", s->displayRev);
    if (!strcmp(key, "preset_name")) {
        const BankEntry &b = BANK[s->preset];
        return snprintf(buf, len, "%s / %s", b.category, b.name);
    }
    if (!strcmp(key, "preset_count")) return snprintf(buf, len, "%d", NBANK);
    if (!strcmp(key, "voices_active")) return snprintf(buf, len, "%d", s->lastActive);   // tools
    if (!strcmp(key, "notes_held")) {   // tools: how many keys have a voice sounding, not released
        bool held[128] = {};
        int n = 0;
        for (int i = 0; i < VOICES; i++) {
            Voice &v = s->odin.m_voice[i];
            if (v && !v.isInRelease() && v.m_MIDI_key >= 0 && v.m_MIDI_key < 128 && !held[v.m_MIDI_key])
                held[v.m_MIDI_key] = true, n++;
        }
        return snprintf(buf, len, "%d", n);
    }
    if (!strncmp(key, "preset:", 7)) {   // program names for MPC's PRESET menu
        int n = atoi(key + 7);
        if (n < 0 || n >= NBANK) return 0;
        return snprintf(buf, len, "%s", BANK[n].name);
    }
    size_t kl = strlen(key);
    if (kl == 15 && !strcmp(key + 10, "_name") && !strncmp(key, "osc", 3)) {   // oscN_shape_name
        const Shape *sh = s->shapeOf(key[3] - '0');
        return snprintf(buf, len, "%s", sh ? sh->name : "-");
    }
    if (kl == 18 && !strcmp(key + 10, "_display") && !strncmp(key, "osc", 3)) {   // oscN_shape_display
        int n = key[3] - '0';
        const Shape *sh = s->shapeOf(n);
        if (!sh) return snprintf(buf, len, "-");
        std::string target = "osc" + std::to_string(n) + sh->suffix;
        for (int i = 0; i < NCONTROLS; i++)   // its value text as the control for that parameter shows it
            if (CONTROLS[i].kind == K_NORM && target == CONTROLS[i].target)
                return snprintf(buf, len, "%s", s->display(CONTROLS[i]).c_str());
        return 0;
    }
    if (!strcmp(key, "tune_display")) return snprintf(buf, len, "%.1f Hz", s->tuneHz);
    if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
        auto it = s->index.find(std::string(key, kl - 8));
        if (it == s->index.end() || CONTROLS[it->second].kind != K_NORM) return 0;
        return snprintf(buf, len, "%s", s->display(CONTROLS[it->second]).c_str());
    }
    auto it = s->index.find(key);
    if (it == s->index.end()) return 0;
    const Control &c = CONTROLS[it->second];
    if (c.kind == K_NORM) return snprintf(buf, len, "%.6f", s->get(c));
    return snprintf(buf, len, "%d", (int)std::lround(s->get(c)));
}

void render(void *inst, int16_t *out, int frames) {
    ((Instance *)inst)->render(out, frames);
}

const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, nullptr};

} // namespace

extern "C" const mpc_engine_t *mpc_engine(void) {
    return &ENGINE;
}
