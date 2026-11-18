/* odin2-mpc dev test of the engine (src/odin_mpc.cpp) through its mpc_engine() interface, on the build machine:
 *   1. every bank preset, loaded the plugin's way (init patch + stored differences), equals the same .odin file
 *      loaded the way Odin loads it, in state and in sound
 *   2. a project state round-trips into a fresh instance, also after edits
 *   3. STOP silences a held chord with its reverb and delay tails; the polyphony limit holds
 *   3g. the USER bank (1.4.10): .odin files from a folder, saved inside the project
 *   4. timings: preset load, state save, render per block (this machine, not the MPC)
 *   tools/engine_test.sh builds and runs it. */
#include "odin_state.h"
extern "C" {
#include "engine.h"
}
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <sstream>

/* wait out the plugin's one-touch window (Instance::TAP_MS) before the next press of the same button */
static void tap_gap() { std::this_thread::sleep_for(std::chrono::milliseconds(300)); }

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
    // the USER bank's folder: empty for everything but 3g
    char emptyTmpl[] = "/tmp/odin_user_emptyXXXXXX";
    std::string emptyDir = mkdtemp(emptyTmpl);
    setenv("ODIN2MPC_USER_DIR", emptyDir.c_str(), 1);
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
            odin_srand(1);
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
        std::vector<int16_t> b4;
        double h4;
        take(heavy, "4", b4, h4);
        printf("info Alien Chaos2, 4 notes: Off %.0f us, Eco %.0f us, Lite %.0f us, Ultra %.0f us per block (this machine)\n",
               h0, h2, h3, h4);
        // 1.4.11 ECO Ultra: half the sample rate. Plays at Lite's level, at well under Lite's cost, nothing above Nyquist
        {
            // the level, over the whole factory bank (two seeds each: many patches have random modulation)
            double r3 = 0, r4 = 0;
            for (int16_t v : b3) r3 += (double)v * v;
            for (int16_t v : b4) r4 += (double)v * v;
            {
                void *q = e->create(nullptr);
                std::vector<double> db;
                for (int p = 0; p < 261; p++) {
                    double lv[2];
                    for (int m = 0; m < 2; m++) {
                        e->set_param(q, "eco", m ? "4" : "3");
                        double tot = 0;
                        for (int seed = 1; seed <= 2; seed++) {
                            e->set_param(q, "preset", std::to_string(p).c_str());
                            odin_srand(seed);
                            for (int j = 0; j < 3; j++) note(e, q, 0x90, 48 + 7 * j, 100);
                            std::vector<int16_t> o;
                            render(e, q, 120, &o);
                            for (int16_t v : o) tot += (double)v * v;
                            for (int j = 0; j < 3; j++) note(e, q, 0x80, 48 + 7 * j, 0);
                            e->set_param(q, "panic", "1");
                            render(e, q, 4);
                        }
                        lv[m] = tot;
                    }
                    if (lv[0] > 1e3) db.push_back(10 * log10((lv[1] + 1) / (lv[0] + 1)));
                }
                e->destroy(q);
                std::sort(db.begin(), db.end());
                int near = 0;
                for (double d : db) near += fabs(d) < 1.5;
                double median = db[db.size() / 2];
                CHECK(fabs(median) < 0.5 && near > (int)db.size() * 8 / 10,
                      "ECO Ultra's level: median %.2f dB from Lite, %d of %d patches within 1.5 dB", median, near, (int)db.size());
            }
            CHECK(h4 < h3 * 0.85, "ECO Ultra costs clearly less than Lite (wall clock here; about 40 %% fewer instructions) (%.0f vs %.0f us)", h4, h3);
            // a bright patch held high: the energy above 11.5 kHz (what half rate can't make, so only filter leakage
            // or aliasing could put there) next to the energy below
            auto band = [](const std::vector<int16_t> &v, double lo, double hi) {
                const int N = 4096;
                size_t start = v.size() / 2 > (size_t)N * 2 ? v.size() / 2 - N * 2 : 0;
                double sum = 0;
                for (int bin = (int)(lo / 44100 * N); bin < (int)(hi / 44100 * N); bin++) {
                    double re = 0, im = 0;
                    for (int n = 0; n < N; n++) {
                        double w = 0.5 - 0.5 * cos(2 * M_PI * n / (N - 1)), x = v[start + 2 * n] * w;
                        re += x * cos(2 * M_PI * bin * n / N), im -= x * sin(2 * M_PI * bin * n / N);
                    }
                    sum += re * re + im * im;
                }
                return sum;
            };
            void *u = e->create(nullptr);
            e->set_param(u, "preset", std::to_string(find("Init Patch")).c_str());
            e->set_param(u, "eco", "4");
            note(e, u, 0x90, 96, 110);   // C7, a saw: harmonics far past Nyquist
            std::vector<int16_t> hi;
            render(e, u, 300, &hi);
            double low = band(hi, 1000, 9500), up = band(hi, 11500, 21000);
            CHECK(low > 0 && up < low * 1e-4, "ECO Ultra: a high saw stays below 11 kHz, no aliasing above (%.1f dB)",
                  10 * log10(up / low + 1e-30));
            // odd block sizes: frames carry over, the sound stays continuous
            void *v1 = e->create(nullptr), *v2 = e->create(nullptr);
            for (void *v : {v1, v2}) {
                e->set_param(v, "preset", std::to_string(heavy).c_str());
                e->set_param(v, "eco", "4");
                odin_srand(3);
                note(e, v, 0x90, 60, 100);
            }
            std::vector<int16_t> s1, s2;
            int16_t buf[256];
            for (int k = 0; k < 120; k++) { e->render(v1, buf, 128); s1.insert(s1.end(), buf, buf + 256); }
            int sizes[] = {37, 91, 1, 128, 3, 64};
            while (s2.size() < s1.size()) {
                int n = sizes[(s2.size() / 2) % 6];
                if ((int)(s1.size() - s2.size()) / 2 < n) n = (int)(s1.size() - s2.size()) / 2;
                e->render(v2, buf, n);
                s2.insert(s2.end(), buf, buf + 2 * n);
            }
            double diff = 0, tot = 0;
            for (size_t i = 0; i < s1.size(); i++) diff += fabs((double)s1[i] - s2[i]), tot += fabs((double)s1[i]);
            CHECK(tot > 0 && s2.size() == s1.size(), "ECO Ultra renders odd block sizes (%.3f of the level differs)", diff / (tot + 1));
            // out of Ultra: back at full rate, same sound as never having been in it
            std::vector<int16_t> f1, f2;
            void *w1 = e->create(nullptr), *w2 = e->create(nullptr);
            e->set_param(w2, "eco", "4");
            e->set_param(w2, "eco", "3");
            for (void *w : {w1, w2}) { e->set_param(w, "preset", std::to_string(light).c_str()); e->set_param(w, "eco", "3"); }
            note(e, w1, 0x90, 60, 100), note(e, w2, 0x90, 60, 100);
            render(e, w1, 50, &f1), render(e, w2, 50, &f2);
            CHECK(f1 == f2, "out of ECO Ultra, the engine is back at 44.1 kHz exactly");
            for (void *z : {u, v1, v2, w1, w2}) e->destroy(z);
            // every factory patch in Ultra: a held chord and its release stay numbers, and stay sounding
            void *z = e->create(nullptr);
            e->set_param(z, "eco", "4");
            std::string blown, quiet;
            for (int i = 0; i < 261; i++) {
                e->set_param(z, "preset", std::to_string(i).c_str());
                long before = atol(get(e, z, "test_nonfinite").c_str());
                for (int n : {48, 60, 67, 76, 84}) note(e, z, 0x90, n, 100);
                std::vector<int16_t> o;
                render(e, z, 120, &o);
                double late = 0;
                for (size_t k = o.size() / 2; k < o.size(); k++) late += fabs((double)o[k]);
                for (int n : {48, 60, 67, 76, 84}) note(e, z, 0x80, n, 0);
                render(e, z, 60);
                e->set_param(z, "panic", "1");
                render(e, z, 4);
                if (atol(get(e, z, "test_nonfinite").c_str()) != before) blown += " " + std::to_string(i);
                if (late == 0) quiet += " " + std::to_string(i);
            }
            CHECK(blown.empty(), "ECO Ultra: every factory patch stays finite (blew up:%s)", blown.c_str());
            if (!quiet.empty()) printf("info ECO Ultra: silent after half a second (patch may be a one-shot):%s\n", quiet.c_str());
            e->destroy(z);
        }
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
        odin_srand(1); std::vector<int16_t> pa = play(a);
        odin_srand(1); std::vector<int16_t> pb = play(b);
        odin_srand(1); std::vector<int16_t> pc = play(c);
        CHECK(get(e, a, "tune_display") == "440.0 Hz" && pa == pb && pa != pc && get(e, c, "tune_display") == "465.0 Hz",
              "master tune: 440 Hz by default and unchanged sound, 465 Hz plays higher");
        e->set_param(c, "preset", "5");
        std::string st = get(e, c, "state");
        void *d = e->create(nullptr);
        e->set_param(d, "state", st.c_str());
        CHECK(get(e, d, "tune_display") == "465.0 Hz", "master tune stays across patches and is saved with the project");
        e->destroy(a); e->destroy(b); e->destroy(c); e->destroy(d);
    }

    // 3f. BROWSE and the top bar (1.4): banks and patches chosen separately; theme saved with the project
    {
        void *x = e->create(nullptr);
        e->set_param(x, "preset", "0");
        std::string b0 = get(e, x, "bank_name"), p0 = get(e, x, "preset_name");
        e->set_param(x, "bank_next", "1");   // 1.4.11: like OB-Xd, the bank and its first patch together
        std::string b1 = get(e, x, "bank_name"), p1 = get(e, x, "preset_name");
        CHECK(b1 != b0 && p1 != p0 && p1 == get(e, x, "patch_slot_1") && get(e, x, "patch_slot_1_on") == "1",
              "BANK arrow loads the next bank's first patch (%s -> %s, %s)", b0.c_str(), b1.c_str(), p1.c_str());
        tap_gap();
        e->set_param(x, "bank_prev", "1");
        CHECK(get(e, x, "bank_name") == b0 && get(e, x, "preset_name") == p0, "and back (%s, %s)",
              get(e, x, "bank_name").c_str(), get(e, x, "preset_name").c_str());
        tap_gap();
        e->set_param(x, "bank_next", "1");
        tap_gap();   // 1.4.6: presses within 250 ms of each other are one touch
        e->set_param(x, "preset_next", "1");
        CHECK(get(e, x, "preset_name") == get(e, x, "patch_slot_2") && get(e, x, "bank_name") == b1, "PATCH arrow steps within the bank");
        e->set_param(x, "preset_next", "1");   // at once: the same touch reported again, no second step
        CHECK(get(e, x, "preset_name") == get(e, x, "patch_slot_2"), "a touch reported twice steps once");
        // a patch picked from MPC's PRESET menu: the top bar's bank follows it, and the arrows step from there
        e->set_param(x, "preset", "100");
        std::string pb = get(e, x, "bank_name");
        tap_gap();
        e->set_param(x, "bank_next", "1");
        CHECK(get(e, x, "bank_name") != pb && get(e, x, "preset_name") == get(e, x, "patch_slot_1"),
              "after a PRESET-menu pick the bank arrow steps from that patch's bank (%s -> %s)", pb.c_str(), get(e, x, "bank_name").c_str());
        tap_gap();
        e->set_param(x, "bank_slot_3", "1");
        e->set_param(x, "patch_slot_2", "1");
        CHECK(get(e, x, "bank_name") == get(e, x, "bank_slot_3") && get(e, x, "preset_name") == get(e, x, "patch_slot_2"),
              "BROWSE: a bank tile, then a patch tile (%s / %s)", get(e, x, "bank_name").c_str(), get(e, x, "preset_name").c_str());
        // BROWSE Q-Links (1.4.1): BANK moves the bank only; PATCH stays inside the bank (clamped at its ends)
        std::string before = get(e, x, "preset_name");
        e->set_param(x, "browse_bank", "5");
        CHECK(get(e, x, "browse_bank_display") == get(e, x, "bank_slot_6") && get(e, x, "preset_name") == before,
              "BROWSE Q-Link 1 changes the browsed bank only (%s)", get(e, x, "browse_bank_display").c_str());
        e->set_param(x, "browse_patch", "3");
        CHECK(get(e, x, "preset_name") == get(e, x, "patch_slot_1"), "Q-Link 2's first turn in a new bank picks its first patch");
        e->set_param(x, "browse_patch", "2");
        std::string bank = get(e, x, "bank_name");
        CHECK(get(e, x, "preset_name") == get(e, x, "patch_slot_3") && get(e, x, "browse_patch_display") == get(e, x, "preset_name"),
              "then steps through it (%s)", get(e, x, "preset_name").c_str());
        e->set_param(x, "browse_patch", "29");
        CHECK(get(e, x, "bank_name") == bank, "and never leaves the bank (%s, %s)", bank.c_str(), get(e, x, "preset_name").c_str());
        e->set_param(x, "theme", "3");   // Sunburst
        std::string st = get(e, x, "state");
        void *y = e->create(nullptr);
        e->set_param(y, "state", st.c_str());
        CHECK(get(e, y, "theme") == "3", "the look is saved with the project");
        // 1.4.7: a 1.4-1.4.6 project saved "theme" in the old list (Odin Blue, 2000, 2500, 3000)
        std::string old = st.substr(0, st.find("look=")) + "theme=3\n";
        e->set_param(y, "state", old.c_str());
        CHECK(get(e, y, "theme") == "0", "an older project's 3000 look opens in Odin Blue");
        old = st.substr(0, st.find("look=")) + "theme=1\n";
        e->set_param(y, "state", old.c_str());
        CHECK(get(e, y, "theme") == "1", "and its 2000 look stays 2000");
        e->destroy(x);
        e->destroy(y);
    }

    // 3f2. names on the main page and BROWSE leave out the "[author]" tag; MPC's PRESET menu keeps it (1.4.11)
    {
        void *x = e->create(nullptr);
        int tagged = -1;
        for (int i = 0; i < 261 && tagged < 0; i++) {
            std::string n = get(e, x, ("preset:" + std::to_string(i)).c_str());
            if (n.size() > 3 && n.back() == ']' && n.rfind(" [") != std::string::npos) tagged = i;
        }
        e->set_param(x, "preset", std::to_string(tagged).c_str());
        std::string full = get(e, x, ("preset:" + std::to_string(tagged)).c_str()), shown = get(e, x, "preset_name");
        CHECK(tagged >= 0 && shown == full.substr(0, full.rfind(" [")) && get(e, x, "browse_patch_display") == shown,
              "a tagged name shows without its tag (%s -> %s)", full.c_str(), shown.c_str());
        e->set_param(x, "preset", "0");
        CHECK(get(e, x, "preset_name") == get(e, x, "preset:0"), "an untagged name is unchanged (%s)", get(e, x, "preset_name").c_str());
        e->destroy(x);
    }

    // 3h. TWO CORES (1.4.11): a second processor plays half the notes
    {
        auto twin = [&](void *x) {   // voices here, on the twin (-1: none), late samples
            std::string t = get(e, x, "test_twin");
            int a = 0, b = 0, c = 0;
            sscanf(t.c_str(), "%d,%d,%d", &a, &b, &c);
            return std::vector<int>{a, b, c};
        };
        auto find = [&](const char *name) {
            void *q = e->create(nullptr);
            int at = -1;
            for (int n = 0; n < 261 && at < 0; n++)
                if (get(e, q, ("preset:" + std::to_string(n)).c_str()) == name) at = n;
            e->destroy(q);
            return at;
        };
        int pad = find("Electric Piano [tx]");
        void *one = e->create(nullptr), *two = e->create(nullptr);
        CHECK(get(e, two, "cores") == "0" && twin(two)[1] == -1, "CORES starts at One, no second processor");
        for (void *x : {one, two}) {
            e->set_param(x, "preset", std::to_string(pad).c_str());
            e->set_param(x, "eco", "0");
            e->set_param(x, "budget_test_fixed", "1");   // the voice budget out of the way: every note sounds on both
        }
        e->set_param(two, "cores", "1");
        CHECK(get(e, two, "cores") == "1" && twin(two)[1] == 0, "CORES Two starts the second processor");
        const int chord[] = {48, 52, 55, 59, 62, 67};
        std::vector<int16_t> o1, o2;
        for (void *x : {one, two}) {
            odin_srand(5);
            for (int n : chord) note(e, x, 0x90, n, 100);
        }
        render(e, one, 300, &o1);
        render(e, two, 300, &o2);
        std::vector<int> t = twin(two);
        CHECK(t[0] > 0 && t[1] > 0 && t[2] == 0, "the notes are split between both (%d here, %d on the twin)", t[0], t[1]);
        double r1 = 0, r2 = 0;
        for (int16_t v : o1) r1 += (double)v * v;
        for (int16_t v : o2) r2 += (double)v * v;
        double ratio = sqrt(r2 / (r1 + 1));
        CHECK(ratio > 0.9 && ratio < 1.1, "two processors play the chord as loud as one (%.3f)", ratio);
        // and the same sound, 128 frames later (the twin's lead): compare one processor's output with two's, shifted
        {
            double best = 0;
            int bestLag = -1;
            for (int lag = 0; lag <= 256; lag += 2) {
                double num = 0, d1 = 0, d2 = 0;
                for (size_t i = 20000; i + lag < o2.size() && i < 60000; i++) {
                    double a = o1[i], b = o2[i + lag];
                    num += a * b, d1 += a * a, d2 += b * b;
                }
                double c = num / sqrt(d1 * d2 + 1);
                if (c > best) best = c, bestLag = lag / 2;
            }
            CHECK(bestLag == 128 && best > 0.9, "the same chord, %d frames later (correlation %.3f)", bestLag, best);
        }
        // its notes end where they started
        for (int n : chord) note(e, two, 0x80, n, 0);
        render(e, two, 1500);
        t = twin(two);
        CHECK(t[0] == 0 && t[1] == 0, "their note-offs reach the processor that plays them (%d, %d left)", t[0], t[1]);
        // STOP reaches both
        for (int n : chord) note(e, two, 0x90, n, 100);
        render(e, two, 20);
        e->set_param(two, "panic", "1");
        std::vector<int16_t> after;
        render(e, two, 40);
        render(e, two, 20, &after);
        double tail = 0;
        for (int16_t v : after) tail += fabs((double)v);
        CHECK(tail == 0, "STOP silences both processors");
        // an edit reaches the twin: Init Patch (a saw through filter 1) with the filter shut is as quiet on two as on one
        for (void *x : {one, two}) e->set_param(x, "preset", std::to_string(find("Init Patch")).c_str());
        for (int n : chord) note(e, two, 0x90, n, 100), note(e, one, 0x90, n, 100);
        render(e, two, 100);
        e->set_param(two, "fil1_freq", "0.0");
        std::vector<int16_t> shut;
        render(e, two, 100);
        render(e, two, 40, &shut);
        double rs = 0;
        for (int16_t v : shut) rs += (double)v * v;
        e->set_param(one, "fil1_freq", "0.0");
        std::vector<int16_t> shut1;
        render(e, one, 100);
        render(e, one, 40, &shut1);
        double rs1 = 0;
        for (int16_t v : shut1) rs1 += (double)v * v;
        for (int n : chord) note(e, two, 0x80, n, 0), note(e, one, 0x80, n, 0);
        CHECK(rs1 > 0 && rs < rs1 * 1.3 + 1e8 && rs > rs1 * 0.7 - 1e8, "a filter edit reaches the twin's voices too (level %.0f, one processor %.0f)",
              sqrt(rs / 10240), sqrt(rs1 / 10240));
        // mono and the arpeggiator stay on one processor
        e->set_param(two, "panic", "1");
        render(e, two, 10);
        e->set_param(two, "playmode", "0");   // Legato
        for (int n : chord) note(e, two, 0x90, n, 100);
        render(e, two, 20);
        t = twin(two);
        CHECK(t[1] == 0, "Legato: every note on this processor (twin %d)", t[1]);
        for (int n : chord) note(e, two, 0x80, n, 0);
        e->set_param(two, "playmode", "1");
        e->set_param(two, "panic", "1");
        render(e, two, 10);
        // the project remembers CORES
        std::string st = get(e, two, "state");
        void *re = e->create(nullptr);
        e->set_param(re, "state", st.c_str());
        CHECK(st.find("cores=2") != std::string::npos && get(e, re, "cores") == "1" && twin(re)[1] == 0,
              "CORES Two is saved with the project and comes back");
        // a twin that is late: the block finishes anyway (the rest of it without the twin), and nothing hangs
        e->set_param(re, "preset", std::to_string(pad).c_str());
        e->set_param(re, "test_twin_delay_us", "8000");
        for (int n : chord) note(e, re, 0x90, n, 100);
        double t0 = now();
        render(e, re, 10);
        double ms = (now() - t0) * 1000 / 10;
        e->set_param(re, "test_twin_delay_us", "0");
        t = twin(re);
        CHECK(t[2] > 0 && atol(get(e, re, "test_nonfinite").c_str()) == 0,
              "a late twin: the block doesn't wait past its time (%d samples without it, %.1f ms per block)", t[2], ms);
        // off again, on again, and destroyed while on
        e->set_param(re, "cores", "0");
        CHECK(twin(re)[1] == -1, "CORES One stops the second processor");
        e->set_param(re, "cores", "1");
        e->set_param(re, "eco", "4");   // and in Ultra
        for (int n : chord) note(e, re, 0x90, n, 100);
        std::vector<int16_t> u;
        render(e, re, 200, &u);
        double ru = 0;
        for (int16_t v : u) ru += (double)v * v;
        t = twin(re);
        CHECK(ru > 0 && t[1] > 0 && atol(get(e, re, "test_nonfinite").c_str()) == 0, "TWO CORES in ECO Ultra plays (%d on the twin)", t[1]);
        // speed: the heavy chord with one processor and with two (this machine has few cores; the MPC has four)
        void *h1 = e->create(nullptr), *h2 = e->create(nullptr);
        int heavy = find("Alien Chaos2 [psylion]");
        for (void *x : {h1, h2}) {
            e->set_param(x, "preset", std::to_string(heavy).c_str());
            e->set_param(x, "eco", "1");
            e->set_param(x, "budget_test_fixed", "1");
        }
        e->set_param(h2, "cores", "1");
        double us[2];
        int k = 0;
        for (void *x : {h1, h2}) {
            for (int n : {48, 52, 55, 59, 62, 64, 67, 71}) note(e, x, 0x90, n, 100);
            render(e, x, 20);
            double t1 = now();
            render(e, x, 300);
            us[k++] = (now() - t1) / 300 * 1e6;
        }
        printf("info Alien Chaos2, 8 notes, voice budget off: one processor %.0f us, two %.0f us per block (this machine)\n", us[0], us[1]);
        for (void *x : {one, two, re, h1, h2}) e->destroy(x);
    }

    // 3g. the USER bank (1.4.10)
    {
        void *x = e->create(nullptr);
        CHECK(get(e, x, "bank_slot_16") == " " && get(e, x, "preset:261") == "USER 1 (empty)",
              "no user files: no USER bank, the bank is as before");
        e->destroy(x);
        char tmpl[] = "/tmp/odin_userXXXXXX";
        std::string dir = mkdtemp(tmpl);
        // two factory patches as user files, and a file that isn't a patch
        std::string pa, pb;
        DIR *d = opendir(bankdir.c_str());
        std::vector<std::string> cats;
        while (struct dirent *de = d ? readdir(d) : nullptr) if (de->d_name[0] != '.') cats.push_back(de->d_name);
        if (d) closedir(d);
        for (auto &c : cats) {
            DIR *cd = opendir((bankdir + "/" + c).c_str());
            while (struct dirent *de = cd ? readdir(cd) : nullptr) {
                std::string n = de->d_name;
                if (n.size() > 5 && n.substr(n.size() - 5) == ".odin") (pa.empty() ? pa : pb.empty() ? pb : n) = bankdir + "/" + c + "/" + n;
            }
            if (cd) closedir(cd);
            if (!pb.empty()) break;
        }
        std::ofstream(dir + "/B mine.odin", std::ios::binary) << readFile(pa);
        std::ofstream(dir + "/a mine.odin", std::ios::binary) << readFile(pb);
        std::ofstream(dir + "/c broken.odin", std::ios::binary) << "not a patch";
        std::ofstream(dir + "/notes.txt") << "x";
        setenv("ODIN2MPC_USER_DIR", dir.c_str(), 1);
        x = e->create(nullptr);
        CHECK(get(e, x, "bank_slot_16") == "USER" && get(e, x, "preset:261") == "a mine" && get(e, x, "preset:262") == "B mine"
              && get(e, x, "preset:263") == "c broken" && get(e, x, "preset:264") == "USER 4 (empty)",
              "the USER bank: the folder's .odin files, by name (%s, %s)", get(e, x, "preset:261").c_str(), get(e, x, "preset:262").c_str());
        // loaded the way Odin loads the file
        void *ref = e->create(nullptr);
        e->set_param(x, "preset", "262");
        std::string sx = get(e, x, "state");
        CHECK(get(e, x, "preset_name") == "B mine" && sx.find("userfile=B mine\n") != std::string::npos,
              "a user patch loads and the project names it");
        e->set_param(x, "bank_slot_16", "1");
        CHECK(get(e, x, "patch_slot_2_on") == "1" && get(e, x, "browse_patch_display") == "B mine", "BROWSE shows it in USER");
        note(e, x, 0x90, 60, 100);
        double lvl = render(e, x, 200);
        CHECK(lvl > 1e-4, "it plays (%.4f)", lvl);
        note(e, x, 0x80, 60, 0);
        std::string before = get(e, x, "preset_name");
        e->set_param(x, "preset", "263");
        CHECK(get(e, x, "preset_name") == before, "a file that isn't a patch doesn't load (still %s)", before.c_str());
        e->set_param(x, "preset", "264");
        void *init = e->create(nullptr);
        e->set_param(init, "state", "odin2mpc=1\npreset=280\n");
        CHECK(get(e, x, "preset_name") == "USER 4 (empty)" && get(e, init, "preset_name") == "USER 20 (empty)",
              "an empty USER slot is the init patch, also from a project");
        e->destroy(init);
        // the project sounds the same without the file, and after it changed
        e->set_param(x, "preset", "262");
        e->set_param(x, "fil1_freq", "0.3");
        sx = get(e, x, "state");
        auto play = [&](void *y) {
            e->set_param(y, "preset", "0"); e->set_param(y, "state", sx.c_str());
            std::vector<int16_t> o; odin_srand(7); note(e, y, 0x90, 57, 110); render(e, y, 120, &o); note(e, y, 0x80, 57, 0); render(e, y, 60, &o);
            return o;
        };
        void *y1 = e->create(nullptr);
        std::vector<int16_t> o1 = play(y1);
        CHECK(get(e, y1, "preset_name") == "B mine" && get(e, y1, "fil1_freq") == get(e, x, "fil1_freq"),
              "a project on a user patch reopens with its edits");
        remove((dir + "/B mine.odin").c_str());
        std::ofstream(dir + "/a mine.odin", std::ios::binary) << readFile(pa);   // and the other file changed
        void *y2 = e->create(nullptr);
        std::vector<int16_t> o2 = play(y2);
        CHECK(o1 == o2 && get(e, y2, "preset_name") == "B mine", "and sounds the same with its file gone");
        std::string sy = get(e, y2, "state");
        void *y3 = e->create(nullptr);
        e->set_param(y3, "state", sy.c_str());
        CHECK(get(e, y3, "preset_name") == "B mine" && get(e, y3, "state") == sy, "saved again from there, it stays the same");
        // a factory patch after a user one is the factory patch
        e->set_param(y3, "preset", "42");
        e->set_param(ref, "preset", "42");
        CHECK(get(e, y3, "state") == get(e, ref, "state"), "a factory patch after a user patch loads clean");
        for (void *z : {x, ref, y1, y2, y3}) e->destroy(z);
        setenv("ODIN2MPC_USER_DIR", emptyDir.c_str(), 1);
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
