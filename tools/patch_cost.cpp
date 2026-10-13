/* Measure every factory patch's load: the render time with no voice sounding (effects only) and per sounding voice,
 * at audio rate (ECO Off) and at control rate (ECO Auto). Prints one line per patch:
 *   <index> <base us> <per-voice us, Off> <per-voice us, control rate> <voices sounding> <name>
 * tools/patch_cost.sh turns this into src/presets/bank_cost.inc, scaled to the MPC Live II.
 * Built with -DODIN_MPC_NO_BUDGET (all 24 voices, nothing shed) so the patch is measured as it is.
 * GPL-3.0-or-later, as Odin 2. */
#include "odin_state.h"
extern "C" {
#include "engine.h"
}
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

static std::string get(const mpc_engine_t *e, void *s, const char *k) {
    char b[256] = {0};
    e->get_param(s, k, b, sizeof b);
    return b;
}

static void note(const mpc_engine_t *e, void *s, int st, int key, int vel) {
    uint8_t m[3] = {(uint8_t)st, (uint8_t)key, (uint8_t)vel};
    e->midi(s, m, 3);
}

/* the median render time of `blocks` blocks, in us; *voices: the mean number of voices sounding */
static double timed(const mpc_engine_t *e, void *s, int blocks, double *voices = nullptr) {
    int16_t out[256];
    std::vector<double> t;
    double v = 0;
    for (int b = 0; b < blocks; b++) {
        auto t0 = std::chrono::steady_clock::now();
        e->render(s, out, 128);
        t.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
        v += atoi(get(e, s, "voices_active").c_str());
    }
    std::sort(t.begin(), t.end());
    if (voices) *voices = v / blocks;
    return t[t.size() / 2];
}

/* one patch in one ECO mode: base and per-voice time (best of `runs`) */
static void measure(const mpc_engine_t *e, int preset, const char *eco, int runs, double &base, double &per,
                    double &voices) {
    base = per = 1e9;
    for (int r = 0; r < runs; r++) {
        void *s = e->create(nullptr);
        e->set_param(s, "preset", std::to_string(preset).c_str());
        e->set_param(s, "eco", eco);
        srand(1);
        timed(e, s, 20);
        double b = timed(e, s, 60);   // no notes: effects (and whatever runs without voices)
        const int keys[4] = {48, 55, 60, 64};
        for (int k : keys) note(e, s, 0x90, k, 100);
        timed(e, s, 10);              // attacks
        double v = 0;
        double t = timed(e, s, 120, &v);
        for (int k : keys) note(e, s, 0x80, k, 0);
        e->destroy(s);
        if (b < base) base = b;
        double p = v > 0 ? (t - b) / v : 0;
        if (p < 0) p = 0;
        if (p < per) per = p, voices = v;
    }
}

int main(int argc, char **argv) {
    const mpc_engine_t *e = mpc_engine();
    int runs = argc > 1 ? atoi(argv[1]) : 3;
    int only = argc > 2 ? atoi(argv[2]) : -1;
    void *s = e->create(nullptr);
    int n = atoi(get(e, s, "preset_count").c_str());
    std::vector<std::string> names;
    for (int i = 0; i < n; i++) names.push_back(get(e, s, ("preset:" + std::to_string(i)).c_str()));
    e->destroy(s);
    for (int i = 0; i < n; i++) {
        if (only >= 0 && i != only) continue;
        double b0, p0, v0, b1, p1, v1;
        measure(e, i, "0", runs, b0, p0, v0);
        measure(e, i, "1", runs, b1, p1, v1);
        printf("%d %.2f %.2f %.2f %.1f %s\n", i, std::min(b0, b1), p0, p1, v0, names[i].c_str());
        fflush(stdout);
    }
}
