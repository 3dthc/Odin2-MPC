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
 *   Ultra (1.4.11) as Lite, with the whole engine at half the sample rate (22.05 kHz) and a 2x upsampler after it:
 *         about half the work again; nothing above about 10 kHz (wavetables are chosen for the lower Nyquist)
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
#include <vector>
#include <cstring>
#include <algorithm>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#ifndef MPC_DESKTOP
#include <dlfcn.h>
#endif

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

/* BROWSE (1.4): the banks are the patch categories, in the order they first appear in the bank */
const int BANK_SLOTS = 16, PATCH_SLOTS = 30, NTHEMES = 4;
struct Banks {
    int n = 0;
    const char *name[BANK_SLOTS];
    std::vector<int> patches[BANK_SLOTS];
    Banks(int nuser = 0) {
        for (int i = 0; i < NBANK; i++) {
            int b = 0;
            while (b < n && strcmp(name[b], BANK[i].category)) b++;
            if (b == n) {
                if (n == BANK_SLOTS) continue;   // more categories than tiles: not browsable (the bank has 15)
                name[n++] = BANK[i].category;
            }
            if ((int)patches[b].size() < PATCH_SLOTS) patches[b].push_back(i);
        }
        if (nuser > 0 && n < BANK_SLOTS) {   // 1.4.10: the USER bank, .odin files from the User Presets folder
            name[n] = "USER";
            for (int u = 0; u < nuser && u < PATCH_SLOTS; u++) patches[n].push_back(NBANK + u);
            n++;
        }
    }
    int of(int preset) const {
        for (int b = 0; b < n; b++)
            for (int p : patches[b])
                if (p == preset) return b;
        return 0;
    }
};

/* ECO Ultra's 2x upsampler: a 63-tap Kaiser-windowed (beta 7) halfband filter. Every other tap but the centre is
 * zero, so the odd output samples are the input delayed and the even ones take 16 multiplies (the taps are
 * symmetric). Flat to about 9.5 kHz, more than 70 dB down above 12.5 kHz, 0.35 ms of delay. */
struct Upsampler2x {
    static const int TAPS = 63, HALF = 32, PAIRS = 16;
    float hist[2 * HALF] = {0};   // the last 32 inputs, written twice so a window is always contiguous
    int pos = 0;
    static const float *coefs() {   // h[2k] * 2 for k = 0..15 (the other half mirrors it)
        static float c[PAIRS];
        static bool done = false;
        if (!done) {
            auto i0 = [](double x) {   // the zeroth-order modified Bessel function, for the Kaiser window
                double sum = 1, t = 1;
                for (int k = 1; k < 30; k++) t *= (x / (2 * k)) * (x / (2 * k)), sum += t;
                return sum;
            };
            const double beta = 7.0, mid = (TAPS - 1) / 2.0;
            for (int k = 0; k < PAIRS; k++) {
                double x = 2 * k - mid, r = x / mid;
                double h = 0.5 * sin(M_PI * 0.5 * x) / (M_PI * 0.5 * x) * i0(beta * sqrt(1 - r * r)) / i0(beta);
                c[k] = (float)(2.0 * h);   // x2: the gain zero-stuffing loses
            }
            done = true;
        }
        return c;
    }
    void reset() { for (float &h : hist) h = 0; pos = 0; }
    /* one input sample in, two output samples out */
    void push(float x, float &a, float &b) {
        const float *c = coefs();
        hist[pos] = hist[pos + HALF] = x;
        const float *w = hist + pos + 1;   // w[0] = the oldest of the last 32 inputs, w[31] = x
        float s = 0;
        for (int k = 0; k < PAIRS; k++) s += c[k] * (w[31 - k] + w[k]);
        a = s;
        b = w[31 - 15];   // the centre tap: x delayed by 15
        pos = pos + 1 < HALF ? pos + 1 : 0;
    }
};

/* 1.4.10: user patches. Their program numbers follow the bank's: NBANK .. NBANK + NUSER - 1 */
const int NUSER = PATCH_SLOTS;
struct UserPatch {
    std::string name;   // the file name without .odin
    std::string data;   // the file; empty for a patch a project kept whose file is gone
    std::string lines;  // that patch: its differences from the init patch, as the project saved them
};

/* The folder: "User Presets" beside the plugin on the MPC; Documents/Odin2 MPC/User Presets on a desktop.
 * ODIN2MPC_USER_DIR overrides both (tools/engine_test). */
std::string userDir() {
    if (const char *e = getenv("ODIN2MPC_USER_DIR")) return e;
#ifdef MPC_DESKTOP
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home || !*home) return "";
    return std::string(home) + "/Documents/Odin2 MPC/User Presets";
#else
    Dl_info di;
    if (!dladdr((void *)&userDir, &di) || !di.dli_fname) return "";
    std::string so = di.dli_fname;
    size_t sl = so.rfind('/');
    return sl == std::string::npos ? "" : so.substr(0, sl) + "/User Presets";
#endif
}

void makeDir(const std::string &d) {
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0755);
#endif
}

/* the folder's .odin files (at most NUSER, by name); makes the folder if it isn't there so it's easy to find */
std::vector<UserPatch> scanUser() {
    std::vector<UserPatch> out;
    std::string d = userDir();
    if (d.empty()) return out;
    DIR *dir = opendir(d.c_str());
    if (!dir) {
#ifdef MPC_DESKTOP
        size_t sl = d.rfind('/');
        if (sl != std::string::npos) makeDir(d.substr(0, sl));
#endif
        makeDir(d);
        return out;
    }
    std::vector<std::string> names;
    while (struct dirent *e = readdir(dir)) {
        std::string n = e->d_name;
        if (n.size() > 5 && n[0] != '.' && strcasecmp(n.c_str() + n.size() - 5, ".odin") == 0) names.push_back(n);
    }
    closedir(dir);
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    });
    for (auto &n : names) {
        if ((int)out.size() >= NUSER) break;
        FILE *f = fopen((d + "/" + n).c_str(), "rb");
        if (!f) continue;
        std::string data;
        char buf[16384];
        size_t r;
        while ((r = fread(buf, 1, sizeof buf, f)) > 0 && data.size() < (4u << 20)) data.append(buf, r);
        fclose(f);
        if (data.empty()) continue;
        out.push_back({n.substr(0, n.size() - 5), data, ""});
    }
    return out;
}

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

/* 1.4.11: the name on the main page and BROWSE, without the "[author]" tag many factory names end with, so it fits its
 * box at a readable size (MPC's PRESET menu and the Sounds-browser files keep the full name) */
std::string screenName(const char *n) {
    std::string t = n;
    size_t a = t.rfind(" [");
    if (a != std::string::npos && a > 0 && t.back() == ']') t.erase(a);
    return t;
}

/* a user patch hasn't been measured: start from a heavy factory patch's cost (the 80th percentile), and let the
 * budget follow what it measures */
const BankCost &userCost() {
    static const BankCost c = [] {
        std::vector<unsigned short> a, b, e;
        for (int i = 0; i < NBANK; i++) a.push_back(BANK_COST[i].voiceOff), b.push_back(BANK_COST[i].voiceCtrl), e.push_back(BANK_COST[i].base);
        std::sort(a.begin(), a.end()), std::sort(b.begin(), b.end()), std::sort(e.begin(), e.end());
        int k = NBANK * 8 / 10;
        return BankCost{a[k], b[k], e[k]};
    }();
    return c;
}

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
    std::vector<UserPatch> user;   // the USER bank (1.4.10)
    Banks bk;
    const Banks &banks() const { return bk; }
    const char *nameOf(int p) const {
        if (p >= 0 && p < NBANK) return BANK[p].name;
        int u = p - NBANK;
        if (u >= 0 && u < (int)user.size()) return user[u].name.c_str();
        if (u < 0 || u >= NUSER) return "";
        snprintf(emptyName, sizeof emptyName, "USER %d (empty)", u + 1);
        return emptyName;
    }
    mutable char emptyName[24];
    bool emptySlot(int p) const { return p >= NBANK + (int)user.size(); }
    OdinMPC::Lines presetState;                    // snapshot right after the current preset was loaded

    uint8_t midi[MAX_MIDI][3];
    int midiFrame[MAX_MIDI];
    int nmidi = 0;

    float left[128], right[128];

    /* ECO Ultra: Odin renders at 22.05 kHz into these; Upsampler2x makes the 44.1 kHz block. A block with an odd frame
     * count leaves one output frame over for the next. */
    float halfL[65], halfR[65];
    Upsampler2x upL, upR;
    float carryL = 0, carryR = 0;
    bool hasCarry = false;
    bool halfRate = false;
    long nonFinite = 0;   // samples that weren't numbers (engine_test reads "test_nonfinite")

    // CPU meter and ECO
    float cpuAvg = 0;      // percent of the block budget, averaged over ~0.1 s
    float cpuHeld = 0;     // cpuAvg, peak-held, falling slowly
    int cpuShown = 0;      // the value the page shows (5 % steps)
    long framesSinceShow = 0;
    unsigned displayRev = 0;
    enum { ECO_OFF, ECO_AUTO, ECO_ECO, ECO_LITE, ECO_ULTRA };
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
    int theme = 0;        // the page's look (1.4), the plugin's own: kept across patches, saved with the project
    int browseBank = 0;   // BROWSE: the bank whose patches the list shows
    // One touch of a button reaches us as two sets, and a redraw makes the host report the button's value back on
    // top of those, so a step taken on every set above 0.5 was taken two or three times. Each button remembers when
    // it last acted; anything within TAP_MS of that is the same touch.
    static constexpr double TAP_MS = 250;
    std::unordered_map<std::string, double> tapAt;
    bool tapOnce(const std::string &key) {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        double now = t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
        auto it = tapAt.find(key);
        if (it != tapAt.end() && now - it->second < TAP_MS) return false;
        tapAt[key] = now;
        return true;
    }
    void setTune(float hz) {
        hz = hz < TUNE_LO ? TUNE_LO : hz > TUNE_HI ? TUNE_HI : hz;
        tuneHz = std::round(hz * 10.f) / 10.f;
        odin.m_master_tune = tuneHz == 440.f ? 1.f : tuneHz / 440.f;
    }

    Instance() : user(scanUser()), bk((int)user.size()) {
        for (int i = 0; i < NCONTROLS; i++) index[CONTROLS[i].key] = i;
        odin.prepare(44100.f);
        loadPreset(0);
        applyEco();
    }

    // ---- presets
    OdinMPC::Lines initState;   // Odin's init patch, as a snapshot (the bank stores differences from it)

    /* Load bank preset n: work out its whole state (init patch plus its stored differences) and apply only what
     * differs from the current state, so a preset that keeps the drawn wavetables doesn't rebuild them. */
    void loadPreset(int n, bool asEmpty = false) {
        if (n < 0 || n >= NBANK + NUSER) return;   // an empty USER slot loads the init patch
        odin.panic();
        if (initState.empty()) {
            odin.loadPatchFromMemory(BinaryData::init_patch_odin, BinaryData::init_patch_odinSize);
            initState = OdinMPC::snapshot(odin);
        }
        OdinMPC::Lines target;
        bool empty = n >= NBANK && (asEmpty || emptySlot(n));
        const UserPatch *u = n >= NBANK && !empty ? &user[n - NBANK] : nullptr;
        if (empty) {
            target = initState;
            OdinMPC::apply(odin, OdinMPC::diff(OdinMPC::snapshot(odin), target));
        } else if (u && !u->data.empty()) {   // a .odin file: Odin's own reader (it migrates older patches)
            OdinMPC::Lines before = OdinMPC::snapshot(odin);
            if (!odin.loadPatchFromMemory(u->data.data(), u->data.size())) {
                OdinMPC::apply(odin, OdinMPC::diff(OdinMPC::snapshot(odin), before));   // not a patch: nothing changes
                return;
            }
            target = OdinMPC::snapshot(odin);
        } else {
            target = OdinMPC::overlay(initState, OdinMPC::parse(u ? u->lines.c_str() : BANK[n].state));
            OdinMPC::apply(odin, OdinMPC::diff(OdinMPC::snapshot(odin), target));
        }
        odin.m_value_tree.state.getChildWithName("misc").setProperty("patch_name", String(nameOf(n)), nullptr);
        odin.resetAudioEngine();   // as Odin does after a patch loads (voices, filters, LFOs, envelopes)
        preset = n;
        presetState = target;
        startBudget();
        browseBank = banks().of(n);
        ++displayRev;   // every control may have moved
    }

    /* a project saved on a user patch: its lines are the whole sound (differences from the init patch). It loads
     * from them, not from the file, so it sounds as saved even if the file changed; the USER entry of that name is
     * marked loaded, or, when there's no such file, the patch is kept as a USER entry of its own. */
    void loadUserState(const std::string &name, const OdinMPC::Lines &lines) {
        int u = 0;
        while (u < (int)user.size() && user[u].name != name) u++;
        std::string text;
        for (auto &kv : lines) text += kv.first + "=" + kv.second + "\n";
        if (u == (int)user.size()) {
            if (u >= NUSER) u = NUSER - 1, user.resize(NUSER - 1);
            user.push_back({name, "", text});
            bk = Banks((int)user.size());
        }
        UserPatch keep = user[u];
        user[u].data.clear(), user[u].lines = text;
        loadPreset(NBANK + u);
        if (!keep.data.empty()) user[u] = keep;   // picking it again from the bank loads the file
    }

    // ---- project state: "preset=<n>" then what changed since that preset was loaded
    std::string saveState() {
        OdinMPC::Lines changed = OdinMPC::diff(presetState, OdinMPC::snapshot(odin));
        std::string s = "odin2mpc=1\npreset=" + std::to_string(preset) + "\n";
        if (preset >= NBANK && !emptySlot(preset)) {   // a user patch: its name, and the whole sound, so the project doesn't need the file
            s += "userfile=" + user[preset - NBANK].name + "\n";
            changed = OdinMPC::diff(initState, OdinMPC::snapshot(odin));
        }
        if (ceiling != VOICE_CEILING) s += "voices=" + std::to_string(ceiling) + "\n";
        if (eco != ECO_AUTO) s += "eco=" + std::to_string(eco) + "\n";
        if (theme) s += "look=" + std::to_string(theme) + "\n";   // 1.4.7: "look" (1.4-1.4.6 saved "theme", another list)
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
        int th = 0;
        std::string userfile;
        bool isUser = false;
        OdinMPC::Lines rest;
        for (auto &kv : lines) {
            if (kv.first == "odin2mpc") continue;
            else if (kv.first == "preset") n = atoi(kv.second.c_str());
            else if (kv.first == "userfile") userfile = kv.second, isUser = true;
            else if (kv.first == "polyphony") continue;   // 1.0-1.2's VOICES knob: the budget does this now
            else if (kv.first == "voices") poly = atoi(kv.second.c_str());
            else if (kv.first == "eco") e = atoi(kv.second.c_str());
            else if (kv.first == "tune") tune = (float)atof(kv.second.c_str());
            else if (kv.first == "look") th = atoi(kv.second.c_str());
            else if (kv.first == "theme") {   // 1.4-1.4.6: Odin Blue, 2000, 2500, 3000; the last two are gone
                int old = atoi(kv.second.c_str());
                th = old == 1 ? 1 : 0;
            }
            else rest.push_back(kv);
        }
        if (isUser) loadUserState(userfile, rest);
        else {
            if (n >= NBANK + NUSER) n = 0;
            loadPreset(n, n >= NBANK);   // saved on an empty USER slot: the init patch, even if a file is there now
            if (!rest.empty()) OdinMPC::apply(odin, rest);
        }
        setPolyphony(poly);
        setEco(e);
        setTune(tune);
        theme = th >= 0 && th < NTHEMES ? th : 0;
        ++displayRev;
    }

    // ---- ECO
    void setEco(int e) {
        if (e < ECO_OFF || e > ECO_ULTRA) return;
        bool rate = (eco == ECO_OFF) != (e == ECO_OFF) || (eco == ECO_ULTRA) != (e == ECO_ULTRA);
        eco = e;
        setHalfRate(e == ECO_ULTRA);
        applyEco();
        if (rate) startBudget();   // audio rate <-> control rate: start from the patch's cost for the new one
    }

    int ecoCap() const {
        switch (eco) {
        case ECO_ECO: return 3;
        case ECO_LITE:
        case ECO_ULTRA: return 2;
        case ECO_AUTO: return autoCap;   // 12, 3, 2 or 1 by load
        default: return 12;
        }
    }

    bool ecoActive() const { return ecoCap() < 12 || eco == ECO_LITE || eco == ECO_ULTRA; }

    /* ECO Ultra in or out: Odin at the other sample rate, from silence (its delay lines and voices start over) */
    void setHalfRate(bool on) {
        if (on == halfRate) return;
        halfRate = on;
        odin.panic();
        odin.prepare(on ? 22050.f : 44100.f);
        odin.resetAudioEngine();
        upL.reset(), upR.reset();
        hasCarry = false;
    }

    void applyEco() {
        bool was = ecoLight;
        applyUnisonCap();
        odin.m_ctrl_rate = eco == ECO_OFF ? 1 : eco == ECO_AUTO ? CTRL_RATE_AUTO : eco == ECO_ULTRA ? CTRL_RATE_ECO / 2 : CTRL_RATE_ECO;
        odin.m_unison_fit = eco != ECO_OFF;
        odin.m_voice_manager.m_steal_from_stacks = eco != ECO_OFF;
        if ((eco == ECO_LITE || eco == ECO_ULTRA) != distLite) {
            distLite = eco == ECO_LITE || eco == ECO_ULTRA;
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

    /* where the loaded patch is in the browsed bank's list (-1: in another bank) */
    int browseIndex() const {
        const std::vector<int> &ps = banks().patches[browseBank];
        for (int i = 0; i < (int)ps.size(); i++)
            if (ps[i] == preset) return i;
        return -1;
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
        const BankCost &c = preset < NBANK ? BANK_COST[preset] : userCost();
        float k = eco == ECO_ULTRA ? 0.55f : 1.f;   // half the samples, plus the upsampler
        hintCost = voiceCost = k * (eco == ECO_OFF ? c.voiceOff : c.voiceCtrl) / 10.f;
        baseCost = k * c.base / 10.f;
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
            else if ((k == "bank_prev" || k == "bank_next") && v > 0.5f) {   // 1.4.11: the next bank, and its first patch
                int n = tapOnce(k) ? banks().n : 0;
                if (!n) break;
                int b = (banks().of(preset) + (k == "bank_next" ? 1 : n - 1)) % n;
                if (!banks().patches[b].empty()) loadPreset(banks().patches[b][0]);
                browseBank = b;
                ++displayRev;
            } else if ((k == "preset_prev" || k == "preset_next") && v > 0.5f) {   // a patch of that bank
                if (!tapOnce(k)) break;
                const std::vector<int> &ps = banks().patches[banks().of(preset)];   // the loaded patch's bank
                int at = -1, m = (int)ps.size();
                for (int i = 0; i < m; i++)
                    if (ps[i] == preset) at = i;
                bool next = k == "preset_next";
                int to = at < 0 ? (next ? 0 : m - 1) : (at + (next ? 1 : m - 1)) % m;
                if (m) loadPreset(ps[to]);
            }
            else if (k == "polyphony") setPolyphony((int)std::lround(v));
            else if (k == "eco") setEco((int)std::lround(v));
            else if (k == "tune") setTune(TUNE_LO + v * (TUNE_HI - TUNE_LO));
            else if (k == "browse_bank") {   // BROWSE Q-Link 1: the bank only, nothing loads
                int b = (int)std::lround(v);
                b = b < 0 ? 0 : b >= banks().n ? banks().n - 1 : b;
                if (b != browseBank) browseBank = b, ++displayRev;
            } else if (k == "browse_patch") {   // BROWSE Q-Link 2: a patch of the browsed bank, never another bank
                const std::vector<int> &ps = banks().patches[browseBank];
                int m = (int)ps.size(), i = (int)std::lround(v), at = browseIndex();
                i = i < 0 ? 0 : i >= m ? m - 1 : i;
                if (at < 0) i = 0;   // the loaded patch is in another bank: the first turn picks this bank's first
                if (m && ps[i] != preset) loadPreset(ps[i]);
            } else if (k == "theme") {
                int t = (int)std::lround(v);
                if (t >= 0 && t < NTHEMES && t != theme) theme = t, ++displayRev;
            } else if (k.compare(0, 10, "bank_slot_") == 0) {   // a tap on a bank: browse it
                int b = tapOnce(k) ? atoi(k.c_str() + 10) - 1 : -1;
                if (b >= 0 && b < banks().n && b != browseBank) browseBank = b, ++displayRev;
            } else if (k.compare(0, 11, "patch_slot_") == 0) {   // a tap on a patch: load it (the loaded one: nothing)
                int i = tapOnce(k) ? atoi(k.c_str() + 11) - 1 : -1;
                const std::vector<int> &ps = banks().patches[browseBank];
                if (i >= 0 && i < (int)ps.size() && ps[i] != preset) loadPreset(ps[i]);
            }
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
            if (k == "theme") return (float)theme;
            if (k == "browse_bank") return (float)browseBank;
            if (k == "browse_patch") return (float)(browseIndex() < 0 ? 0 : browseIndex());
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
        if (halfRate) renderHalf(frames);
        else odin.processStereo(left, right, frames, midi, midiFrame, nmidi);
        nmidi = 0;
        double spent = nowSeconds() - t0;
        for (int i = 0; i < frames; i++) {
            if (!std::isfinite(left[i]) || !std::isfinite(right[i])) ++nonFinite;   // engine_test: none, in any mode
            out[2 * i] = toS16(left[i]);
            out[2 * i + 1] = toS16(right[i]);
        }
        float pct = (float)(spent / (frames * BLOCK_SECONDS_PER_FRAME) * 100.0) + testLoad;
        meter(pct, frames);
        budget(testFixed ? testLoad : pct, frames);
    }

    /* ECO Ultra: render ceil(frames / 2) frames at 22.05 kHz, then two output frames from each (one left over when
     * the count is odd, played first next block); MIDI lands on the half-rate frame of its offset */
    void renderHalf(int frames) {
        int o = 0;
        if (hasCarry && frames > 0) left[0] = carryL, right[0] = carryR, o = 1, hasCarry = false;
        int need = frames - o, n = (need + 1) / 2;
        for (int i = 0; i < nmidi; i++) {
            int f = (midiFrame[i] - o) / 2;
            midiFrame[i] = f < 0 ? 0 : f >= n ? (n > 0 ? n - 1 : 0) : f;
        }
        if (n > 0) odin.processStereo(halfL, halfR, n, midi, midiFrame, nmidi);
        for (int i = 0; i < n; i++) {
            float a, b, c, d;
            upL.push(halfL[i], a, b);
            upR.push(halfR[i], c, d);
            left[o] = a, right[o] = c, o++;
            if (o < frames) left[o] = b, right[o] = d, o++;
            else carryL = b, carryR = d, hasCarry = true;
        }
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
    if (!strncmp(key, "preset_category:", 16)) {   // its bank, for the Sounds browser's folders (tools/xpl_presets.sh)
        int n = atoi(key + 16);
        if (n >= 0 && n < NBANK) return snprintf(buf, len, "%s", BANK[n].category);
        return n >= NBANK && n < NBANK + NUSER ? snprintf(buf, len, "USER") : 0;
    }
    if (!strcmp(key, "test_nonfinite")) return snprintf(buf, len, "%ld", s->nonFinite);
    if (!strcmp(key, "browse_bank_display")) return snprintf(buf, len, "%s", s->banks().name[s->browseBank]);
    if (!strcmp(key, "browse_patch_display")) {
        int at = s->browseIndex();
        return snprintf(buf, len, "%s", at < 0 ? "-" : screenName(s->nameOf(s->preset)).c_str());
    }
    if (!strcmp(key, "bank_name")) return snprintf(buf, len, "%s", s->banks().name[s->banks().of(s->preset)]);   // the loaded patch's
    if (!strcmp(key, "preset_name")) return snprintf(buf, len, "%s", screenName(s->nameOf(s->preset)).c_str());   // its bank: BROWSE
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
        if (n < 0 || n >= NBANK + NUSER) return 0;
        if (n >= NBANK + (int)s->user.size()) return snprintf(buf, len, "USER %d (empty)", n - NBANK + 1);
        return snprintf(buf, len, "%s", s->nameOf(n));
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
    if (!strncmp(key, "bank_slot_", 10) || !strncmp(key, "patch_slot_", 11)) {   // BROWSE list tiles
        bool bank = key[0] == 'b';
        const char *rest = key + (bank ? 10 : 11);
        int i = atoi(rest) - 1;
        bool on = strstr(rest, "_on") != nullptr;
        const Banks &bk = s->banks();
        if (bank) {
            if (i < 0 || i >= bk.n) return on ? snprintf(buf, len, "0") : snprintf(buf, len, " ");
            return on ? snprintf(buf, len, "%d", i == s->browseBank) : snprintf(buf, len, "%s", bk.name[i]);
        }
        const std::vector<int> &ps = bk.patches[s->browseBank];
        if (i < 0 || i >= (int)ps.size()) return on ? snprintf(buf, len, "0") : snprintf(buf, len, " ");
        return on ? snprintf(buf, len, "%d", ps[i] == s->preset) : snprintf(buf, len, "%s", screenName(s->nameOf(ps[i])).c_str());
    }
    if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
        auto it = s->index.find(std::string(key, kl - 8));
        if (it == s->index.end() || CONTROLS[it->second].kind != K_NORM) return 0;
        return snprintf(buf, len, "%s", s->display(CONTROLS[it->second]).c_str());
    }
    auto it = s->index.find(key);
    if (it == s->index.end()) return 0;
    const Control &c = CONTROLS[it->second];
    // continuous controls read back unrounded (an OSC SHAPE knob or TUNE read as 0/1 jumped under a Q-Link in 1.2-1.3)
    if (c.kind == K_NORM || s->shapeOsc(c.key) || !strcmp(c.key, "tune")) return snprintf(buf, len, "%.6f", s->get(c));
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
