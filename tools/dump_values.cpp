/* Dev tool for tools/device_preview.py: load a factory patch (and a theme) into the engine and print every control's
 * value as MPC would read it: one line per key from stdin, "key<TAB>value<TAB>display<TAB>name<TAB>on".
 *   dump_values "<patch name>" <theme> [key=value ...] < keys.txt
 * GPL-3.0-or-later, as Odin 2. */
#include "odin_state.h"
extern "C" {
#include "engine.h"
}
#include <cstdio>
#include <iostream>
#include <string>

static std::string get(const mpc_engine_t *e, void *s, const std::string &k) {
    char b[512] = {0};
    if (e->get_param(s, k.c_str(), b, sizeof b) <= 0) return "";
    return b;
}

int main(int argc, char **argv) {
    const mpc_engine_t *e = mpc_engine();
    void *s = e->create(nullptr);
    if (argc > 1) {
        for (int i = 0;; i++) {
            std::string nm = get(e, s, "preset:" + std::to_string(i));
            if (nm.empty()) break;
            if (nm == argv[1]) { e->set_param(s, "preset", std::to_string(i).c_str()); break; }
        }
    }
    if (argc > 2) e->set_param(s, "theme", argv[2]);
    for (int a = 3; a < argc; a++) {
        std::string kv = argv[a];
        size_t eq = kv.find('=');
        if (eq != std::string::npos) e->set_param(s, kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str());
    }
    int16_t out[256];
    for (int b = 0; b < 40; b++) e->render(s, out, 128);   // let the CPU meter and voice budget settle
    std::string k;
    while (std::getline(std::cin, k)) {
        if (k.empty()) continue;
        printf("%s\t%s\t%s\t%s\t%s\n", k.c_str(), get(e, s, k).c_str(), get(e, s, k + "_display").c_str(),
               get(e, s, k + "_name").c_str(), get(e, s, k + "_on").c_str());
    }
    e->destroy(s);
}
