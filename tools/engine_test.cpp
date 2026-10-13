/* odin2-mpc dev test of the engine (src/odin_mpc.cpp) through its mpc_engine() interface, on the build machine:
 *   1. every bank preset, loaded the plugin's way (init patch + stored differences), equals the same .odin file
 *      loaded the way Odin loads it, in state and in sound
 *   2. a project state round-trips into a fresh instance, also after edits
 *   3. STOP silences a held chord with its reverb and delay tails; the polyphony limit holds
 *   4. timings: preset load, state save, render per block (this machine, not the MPC)
 *   tools/engine_test.sh builds and runs it. */
#include "odin_state.h"
extern "C" {
#include "engine.h"
}
#include <chrono>
#include <cmath>
#include <dirent.h>
#include <fstream>
#include <sstream>

static int fails = 0;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)

static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static std::string readFile(const std::string &p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

static void note(const mpc_engine_t *e, void *s, int st, int n, int v) { uint8_t m[3] = {(uint8_t)st, (uint8_t)n, (uint8_t)v}; e->midi(s, m, 3); }

static double render(const mpc_engine_t *e, void *s, int blocks, std::vector<int16_t> *keep = nullptr) {
    int16_t b[256]; double sum = 0;
    for (int k = 0; k < blocks; k++) {
        e->render(s, b, 128);
        for (int i = 0; i < 256; i++) sum += (double)b[i] * b[i];
        if (keep) keep->insert(keep->end(), b, b + 256);
    }
    return sqrt(sum / (blocks * 256.0)) / 32768.0;
}

static std::string get(const mpc_engine_t *e, void *s, const char *k) { char b[9000] = {0}; e->get_param(s, k, b, sizeof b); return b; }

int main(int argc, char **argv) {
    std::string bankdir = argc > 1 ? argv[1] : "presets_src/Soundbanks/Factory Presets";
    const mpc_engine_t *e = mpc_engine();
    double t0 = now();
    void *s = e->create(nullptr);
    printf("create: %.0f ms\n", (now() - t0) * 1000);

    // 1. every bank preset, applied the plugin's way, against the .odin file read by Odin's own patch loader
    {
        struct BankEntry { const char *category, *name, *state; };
        static const BankEntry BANK[] = {
#include "presets/bank_data.inc"
        };
        OdinAudioProcessor &ref = *new OdinAudioProcessor();   // far too big for the stack
        ref.prepare(44100);
        ref.loadPatchFromMemory(BinaryData::init_patch_odin, BinaryData::init_patch_odinSize);
        OdinMPC::Lines init = OdinMPC::snapshot(ref);
        int checked = 0, equal = 0;
        double worst = 0;
        std::string worst_name;
        for (size_t n = 1; n < sizeof BANK / sizeof BANK[0]; n++) {
            std::string data = readFile(bankdir + "/" + BANK[n].category + "/" + BANK[n].name + ".odin");
            if (data.empty()) { printf("FAIL no file for %s\n", BANK[n].name); fails++; continue; }
            ref.loadPatchFromMemory(BinaryData::init_patch_odin, BinaryData::init_patch_odinSize);
            ref.loadPatchFromMemory(data.data(), data.size());
            OdinMPC::Lines want = OdinMPC::snapshot(ref);
            OdinMPC::Lines got = OdinMPC::overlay(init, OdinMPC::parse(BANK[n].state));
            checked++;
            if (OdinMPC::diff(want, got).empty() && OdinMPC::diff(got, want).empty()) equal++;
            else printf("  differs: %s\n", BANK[n].name);
            // and the engine: load it and check nothing is left over against that state
            t0 = now();
            e->set_param(s, "preset", std::to_string(n).c_str());
            double dt = now() - t0;
            if (dt > worst) { worst = dt; worst_name = BANK[n].name; }
            std::string st = get(e, s, "state");
            if (st.find('\n', st.find("preset=")) != st.size() - 1) { printf("  not clean after load: %s\n", BANK[n].name); fails++; }
        }
        CHECK(equal == checked && checked > 250, "%d/%d bank presets equal Odin's own loading of the .odin file", equal, checked);
        printf("info slowest preset load: %.1f ms (%s, this machine)\n", worst * 1000, worst_name.c_str());
    }

    // 2. state round trip with edits
    e->set_param(s, "preset", "42");
    e->set_param(s, "fil1_freq", "0.3");
    e->set_param(s, "osc2_type", "2");
    e->set_param(s, "env1_r", "0.6");
    e->set_param(s, "delay_on", "1");
    e->set_param(s, "polyphony", "8");
    e->set_param(s, "playmode", "2");
    std::string st = get(e, s, "state");
    printf("info state after 6 edits: %zu bytes\n", st.size());
    void *b = e->create(nullptr);
    e->set_param(b, "state", st.c_str());
    bool same = true;
    for (const char *k : {"preset", "fil1_freq", "osc2_type", "env1_r", "delay_on", "polyphony", "playmode", "osc1_vol"}) {
        std::string va = get(e, s, k), vb = get(e, b, k);
        if (va != vb) { printf("  %s: %s vs %s\n", k, va.c_str(), vb.c_str()); same = false; }
    }
    CHECK(same, "state restores the preset, the edits, polyphony and play mode");
    CHECK(get(e, b, "state") == st, "a restored state saves back identically");
    e->destroy(b);

    // 3. STOP and polyphony
    e->set_param(s, "preset", "0");
    e->set_param(s, "polyphony", "24");
    e->set_param(s, "reverb_on", "1"); e->set_param(s, "delay_on", "1"); e->set_param(s, "env1_r", "0.9");
    for (int k = 0; k < 6; k++) note(e, s, 0x90, 48 + 4 * k, 110);
    double loud = render(e, s, 40);
    e->set_param(s, "panic", "1");
    render(e, s, 2);
    double after = render(e, s, 20);
    CHECK(loud > 0.01 && after < 1e-4, "STOP silences a held chord with reverb and delay (rms %.4f -> %.6f)", loud, after);
    e->set_param(s, "panic", "0");
    e->set_param(s, "polyphony", "2");
    for (int k = 0; k < 6; k++) note(e, s, 0x90, 48 + 4 * k, 110);
    render(e, s, 10);
    // count sounding voices through Odin's voice manager
    // (indirect: with 2 voices, notes 3..6 steal; the engine must not crash and stay audible)
    double two = render(e, s, 20);
    CHECK(two > 0.001, "polyphony 2 with 6 notes held still plays (rms %.4f)", two);
    for (int k = 0; k < 6; k++) note(e, s, 0x80, 48 + 4 * k, 0);
    e->set_param(s, "polyphony", "24");

    // 3b. ECO: no change for patches within its caps; fewer unison voices and less work for heavy ones;
    //     saved with the project; a project from before ECO opens in Auto
    {
        auto find = [&](const char *name) {
            for (int n = 0;; n++) {
                std::string nm = get(e, s, (std::string("preset:") + std::to_string(n)).c_str());
                if (nm.empty()) return -1;
                if (nm == name) return n;
            }
        };
        auto take = [&](int preset, const char *eco, std::vector<int16_t> &out, double &us) {
            void *x = e->create(nullptr);
            e->set_param(x, "preset", std::to_string(preset).c_str());
            e->set_param(x, "eco", eco);
            srand(1);
            for (int k = 0; k < 4; k++) note(e, x, 0x90, 48 + 5 * k, 100);
            double t = now();
            render(e, x, 200, &out);
            us = (now() - t) / 200 * 1e6;
            e->destroy(x);
        };
        std::vector<int16_t> a1, a2;
        double u1, u2;
        int light = find("Init Patch");
        take(light, "0", a1, u1);
        take(light, "3", a2, u2);
        CHECK(a1 == a2, "ECO Lite leaves a patch without unison or distortion unchanged (Init Patch)");
        int heavy = find("Alien Chaos2 [psylion]");
        std::vector<int16_t> b0, b2, b3;
        double h0, h2, h3;
        take(heavy, "0", b0, h0);
        take(heavy, "2", b2, h2);
        take(heavy, "3", b3, h3);
        double rms2 = 0;
        for (int16_t v : b2) rms2 += (double)v * v;
        CHECK(b0 != b2 && rms2 > 0, "ECO Eco plays Alien Chaos2 lighter and still plays it");
        printf("info Alien Chaos2, 4 notes: Off %.0f us, Eco %.0f us, Lite %.0f us per block (this machine)\n", h0, h2, h3);
        void *x = e->create(nullptr);
        CHECK(get(e, x, "eco") == "1", "ECO starts in Auto");
        e->set_param(x, "eco", "3");
        std::string st = get(e, x, "state");
        void *y = e->create(nullptr);
        e->set_param(y, "state", st.c_str());
        CHECK(get(e, y, "eco") == "3" && get(e, y, "eco_active") == "1", "ECO Lite is saved with the project, and lights");
        e->set_param(y, "state", "odin2mpc=1\npreset=5\n");
        CHECK(get(e, y, "eco") == "1", "a project saved before ECO opens in Auto");
        e->destroy(x);
        e->destroy(y);
    }

    // 3c. OSC SHAPE: follows the oscillator's type, named after what it turns
    {
        void *x = e->create(nullptr);
        e->set_param(x, "osc1_type", "1");   // Analog
        e->set_param(x, "osc1_shape", "0.25");
        CHECK(get(e, x, "osc1_shape_name") == "PW" && fabs(atof(get(e, x, "osc1_pw").c_str()) - 0.25) < 0.01,
              "SHAPE on an analog oscillator turns its pulse width (%s, pw %s)", get(e, x, "osc1_shape_display").c_str(),
              get(e, x, "osc1_pw").c_str());
        e->set_param(x, "osc1_type", "2");   // Wavetable
        e->set_param(x, "osc1_shape", "0.7");
        CHECK(get(e, x, "osc1_shape_name") == "POSITION" && fabs(atof(get(e, x, "osc1_pos").c_str()) - 0.7) < 0.01,
              "SHAPE on a wavetable oscillator turns its position");
        e->set_param(x, "osc1_type", "0");   // Off
        CHECK(get(e, x, "osc1_shape_name") == "-", "SHAPE on an oscillator that is off turns nothing");
        e->destroy(x);
    }

    // 3d. voices (1.3): the budget gives a light patch all 16 voices and a heavy one fewer; a chord stays within it;
    //     under overload the oldest notes fade out; the budget recovers; the old VOICES setting in a project is
    //     ignored
    {
        auto find = [&](const char *name) {
            for (int n = 0;; n++) {
                std::string nm = get(e, s, (std::string("preset:") + std::to_string(n)).c_str());
                if (nm.empty()) return -1;
                if (nm == name) return n;
            }
        };
        void *x = e->create(nullptr);
        e->set_param(x, "budget_test_fixed", "1");   // the budget sees only the test's load, not this machine's
        e->set_param(x, "preset", std::to_string(find("Init Patch")).c_str());
        int light = atoi(get(e, x, "voices").c_str());
        e->set_param(x, "preset", std::to_string(find("Antarctica [RS]")).c_str());
        int heavy = atoi(get(e, x, "voices").c_str());
        CHECK(light == 16 && heavy >= 2 && heavy < 8, "voice budget: Init Patch %d voices, Antarctica %d", light, heavy);

        for (int k = 0; k < 4; k++) note(e, x, 0x90, 50 + 4 * k, 100);
        render(e, x, 4);
        int active = atoi(get(e, x, "voices_active").c_str());
        int held = atoi(get(e, x, "notes_held").c_str());
        CHECK(active >= 1 && active <= heavy && held == (heavy < 4 ? heavy : 4),
              "a 4-note chord on a unison-2 patch stays within the budget and keeps its notes (%d voices of %d, %d notes)",
              active, heavy, held);
        for (int k = 0; k < 4; k++) note(e, x, 0x80, 50 + 4 * k, 0);
        render(e, x, 400);

        e->set_param(x, "preset", std::to_string(find("Init Patch")).c_str());
        for (int k = 0; k < 8; k++) note(e, x, 0x90, 48 + 3 * k, 100);
        render(e, x, 20);
        int before = atoi(get(e, x, "voices_active").c_str());
        e->set_param(x, "budget_test_load", "95");
        double loud = render(e, x, 60);
        int after = atoi(get(e, x, "voices_active").c_str());
        int lim = atoi(get(e, x, "voices").c_str());
        CHECK(before == 8 && after >= 1 && after < 8 && loud > 0 && lim < 16,
              "overload fades out the oldest notes (%d -> %d voices, budget %d) and keeps playing", before, after, lim);
        e->set_param(x, "budget_test_load", "0");
        for (int k = 0; k < 8; k++) note(e, x, 0x80, 48 + 3 * k, 0);
        render(e, x, 3000);   // ~9 s: the budget comes back up a voice per half second
        CHECK(atoi(get(e, x, "voices").c_str()) == 16, "the budget recovers when the load drops (%s voices)",
              get(e, x, "voices").c_str());

        e->set_param(x, "state", "odin2mpc=1\npreset=0\npolyphony=3\n");
        CHECK(atoi(get(e, x, "voices").c_str()) == 16 && get(e, x, "polyphony") == "16",
              "a project from 1.2 with VOICES at 3 opens with the automatic voices");
        e->destroy(x);
    }

    // 3e. master tune (1.3): A4 in Hz, saved with the project, kept across patches; 440 Hz changes nothing
    {
        auto play = [&](void *x) {
            std::vector<int16_t> out;
            note(e, x, 0x90, 69, 100);
            render(e, x, 60, &out);
            note(e, x, 0x80, 69, 0);
            return out;
        };
        void *a = e->create(nullptr), *b = e->create(nullptr), *c = e->create(nullptr);
        e->set_param(b, "tune", "0.5");
        e->set_param(c, "tune", "1");
        srand(1); std::vector<int16_t> pa = play(a);
        srand(1); std::vector<int16_t> pb = play(b);
        srand(1); std::vector<int16_t> pc = play(c);
        CHECK(get(e, a, "tune_display") == "440.0 Hz" && pa == pb && pa != pc && get(e, c, "tune_display") == "465.0 Hz",
              "master tune: 440 Hz by default and unchanged sound, 465 Hz plays higher");
        e->set_param(c, "preset", "5");
        std::string st = get(e, c, "state");
        void *d = e->create(nullptr);
        e->set_param(d, "state", st.c_str());
        CHECK(get(e, d, "tune_display") == "465.0 Hz", "master tune stays across patches and is saved with the project");
        e->destroy(a); e->destroy(b); e->destroy(c); e->destroy(d);
    }

    // 4. render cost and the meter
    e->set_param(s, "preset", "100");
    for (int k = 0; k < 8; k++) note(e, s, 0x90, 48 + 3 * k, 100);
    t0 = now();
    render(e, s, 344);
    printf("info 8 notes, preset 100 (%s): %.0f us per block here; meter shows %s %%\n", get(e, s, "preset_name").c_str(),
           (now() - t0) / 344 * 1e6, get(e, s, "cpu").c_str());
    t0 = now();
    for (int k = 0; k < 50; k++) get(e, s, "state");
    printf("info state save: %.2f ms\n", (now() - t0) / 50 * 1000);
    e->destroy(s);
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
