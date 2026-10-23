# Testing

## 1.3 (2026-10-10, offline)

- **ECO Off is still exactly Odin:** with the random generator pinned and the voice budget out of the way, all 259
  deterministic factory patches render sample for sample the same as 1.2 (4-note chords, 200 blocks).
- **Control rate** (x86, all 261 patches, 4 notes): Auto costs about 68 % of Off, Eco about 64 % (with its unison
  cap). Compared with Off in 24 frequency bands, Auto differs by a median 0.02 dB and by under 1 dB on 255 of 259
  patches. The outliers: Something Awakens (chaotic LFO-modulated delay time, about 5 dB), Messy Seq X-Y, SynKeys 2
  and Alien Chaos (1.0-1.4 dB). Eco differs by a median 0.05 dB, under 1 dB on 230.
- **Voice budget** (`tools/engine_test.sh`, x86 and armhf under QEMU): Init Patch gets 16 voices, Antarctica 4. A
  4-note chord on Antarctica (unison 2) keeps all 4 notes within its 4 voices. A forced overload fades out the
  oldest notes and keeps playing, and the budget comes back to 16 once the load drops. A 1.2 project with VOICES at 3
  opens with automatic voices.
- **Per-patch costs** (`tools/patch_cost.sh`): measured on x86, scaled so Antarctica at audio rate with 4 voices
  reads 80 % (your Live II reading). The budget starts there and then follows what the device measures.
- **Master tune:** 440 Hz by default changes nothing, 465 Hz plays higher, and it stays across patches and in the
  project.
- **Kit host test** (ASan/UBSan): PASSED, 301 checks. Parameters 0-178 unchanged, `voices` and `tune` appended.
  The `polyphony` parameter is now the most voices ever (default 16) and is off the page.
- **On the device:** Antarctica [RS] and Alien Chaos2 with chords in Auto. Watch VOICES and CPU (it should stay below
  red) and listen for crackles. Then check TUNE A4.


## 1.2 (2026-10-09, offline)

- New pages (MAIN, OSC, FILTER, ENV, FX): built with the kit's generator, every page and type previewed; the skin
  checker finds no overlapping touch areas (only open popup lists, which cover the page by design) and nothing past
  the screen edge. 16 Q-Links per page in four columns of four. Skin images decode to about 66 MB on the MPC
  (1.1: about 100 MB): only an oscillator/filter column's title band changes colour with its type.
- OSC SHAPE (`tools/engine_test.sh`): drives PW on an analog oscillator and position on a wavetable one, named
  after it; turns nothing when the oscillator is off. Parameters 0-175 unchanged, 3 appended (1.0/1.1 projects open).
- On the device: check the knob rings, the size of names and values from a standing distance, and that each Q-Link
  column turns the four outlined knobs on every page.


## 1.1 (2026-10-09, offline)

- **Speed-ups don't change the sound:** with Odin's clock-seeded random generator pinned, all 259 factory patches
  that are deterministic render sample for sample the same as 1.0 with ECO off (4-note chords, 200 blocks). The
  other two (RandArpChords, PM Sequence) use the random arpeggiator, which differs on every run in 1.0 too.
- About 21 % fewer instructions on Alien Chaos2, the heaviest patch (callgrind, x86).
- **ECO** (`tools/engine_test.sh`): Lite leaves the Init Patch unchanged; on Alien Chaos2 with 4 notes, Eco costs
  about 54 % and Lite about 35 % of Off (this machine); ECO is saved with the project; a 1.0 project opens in Auto.
- Kit host test (ASan/UBSan): PASSED, 301 checks. Parameters 0-173 are unchanged; `eco` and `eco_active` are
  appended, so 1.0 projects keep their values.
- On the device: repeat Alien Chaos2 with ECO on Off, Auto and Eco and note the CPU meter for each.


## Offline (2026-10-09, on the development machine, not on an MPC)

- **Kit host test**, `../mpc-vst-plugins/tools/test_port.sh vst/vst.json` (x86, ASan + UBSan): PASSED, 301 checks
  (two instances, names, set/get, option stepping, wheel/Q-Link/sweep stepping, popups, note to audio, chunk
  round trip, CC/NRPN, two-thread access). No sanitizer reports.
- **Engine test**, `tools/engine_test.sh` (x86, and armhf under QEMU with a cross gcc 13): PASSED on both.
  - all 260 factory patches, loaded the plugin's way, give exactly the state Odin's own patch reader gives for the
    `.odin` file;
  - a project state (patch plus edits, polyphony, play mode) restores into a new instance and saves back identically;
  - STOP takes a held chord with reverb and delay to silence at once;
  - VOICES = 2 with six notes held keeps playing (voice stealing inside the limit).
- **ARM link check**: a cross build of the `.so` links with `-Wl,--no-undefined` and exports only `VSTPluginMain`.
  (The real build is `build.sh`, in the kit's bullseye container, glibc 2.31.)
- **Skin**: built with the kit's generator and previewed page by page (`tools/studio.py preview`); the skin checker
  reports only overlaps with open popup lists (a list covers the page while it is open).
- Timings on the development machine (x86, so only relative): a factory patch loads in under 25 ms, a project
  state saves in under 1 ms, 8 notes of a typical patch render in about 0.2 ms per 128-sample block.

## On the MPC (to do)

| # | Check | How | Expect |
|---|---|---|---|
| 1 | Loads | insert Odin 2 on a track | the OSC page, Init patch, sound on pads/keys |
| 2 | Patches | ◀ ▶ in the top bar, and MPC's PRESET menu | name updates, every page follows, no crash; a short click on a patch change is normal |
| 3 | CPU | hold a big chord on a heavy patch (Antarctica, Alien Chaos2) | VOICES shows the patch's voices, CPU stays out of the red, no crackle |
| 4 | STOP | press it with a long release/reverb ringing | silence at once, playing again works |
| 5 | Pages | every page, every Q-Link column, oscillator and filter types | panels change colour/title with the type, values move |
| 6 | Project | save, reload | same patch, edits, ECO and tune |
| 7 | Bench | `../mpc-vst-plugins/tools/bench.sh vst/build/odin2.so <ip> -j` | PASS or WARN; record the result here |

## 1.4.4: rebuilt on the 2026-10-21 wrapper
The wrapper no longer asks MPC to rebuild its parameter screen on every parameter write (Q-Links on the track's automation
screen snapped back). The oscillator types and the preset number still do, because the shape knob's name and value and the
preset name follow them (`"refresh_display"`, written by `vst/make_params.py`). Check on the device:
- **track edit → automation, turn the Q-Links**: each moves its parameter and stays;
- change an oscillator's type: its shape knob's name changes at once; the preset arrows still update the preset name.
`vst/params.lock` (written by gen_vst.py) is committed: the build now fails if a parameter changes index.
Release zips are built by GitHub Actions (Actions → Release → Run workflow).
