/* Where Odin's time goes (dev tool, build machine): plays a chord on the given factory patches for a few seconds through
 * the engine interface, so a profiler (tools/profile.sh: valgrind --tool=callgrind) can split the cost by function.
 *   profile <eco 0-3> <seconds> <preset>...   (the voice budget runs as it does on the device) */
extern "C" {
#include "engine.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
static void note(const mpc_engine_t *e, void *s, int st, int n, int v) { uint8_t m[3] = {(uint8_t)st, (uint8_t)n, (uint8_t)v}; e->midi(s, m, 3); }
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "profile <eco> <seconds> <preset>...\n"); return 1; }
    const mpc_engine_t *e = mpc_engine();
    void *x = e->create(nullptr);
    e->set_param(x, "eco", argv[1]);
    int blocks = (int)(atof(argv[2]) * 44100 / 128);
    int16_t b[256];
    const int chord[] = {48, 55, 60, 64, 67, 72};
    for (int a = 3; a < argc; a++) {
        e->set_param(x, "preset", argv[a]);
        for (int n : chord) note(e, x, 0x90, n, 100);
        for (int k = 0; k < blocks; k++) e->render(x, b, 128);
        for (int n : chord) note(e, x, 0x80, n, 0);
        for (int k = 0; k < 200; k++) e->render(x, b, 128);
    }
    e->destroy(x);
    return 0;
}
