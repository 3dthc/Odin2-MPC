![Odin 2 on the MPC Live II](odin2-mpc-main.png)
# Odin 2 for MPC

[Odin 2](https://github.com/TheWaveWarden/odin2), TheWaveWarden's semi-modular synthesizer, as a native
VST2 instrument for Akai MPC OS standalone devices (built for a Gen 1 MPC Live II), with its own touchscreen pages in
Odin's look. It is built with the [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) kit.

This is Odin's own sound engine (oscillators, filters, envelopes, LFOs, mod matrix, arpeggiator, effects), compiled
for the MPC's ARM processor without JUCE. Odin's 261 factory patches are built in and load exactly as Odin loads them
(checked patch by patch against Odin's own patch reader, `tools/engine_test.sh`).

## What's on the MPC

Five pages, laid out for playing standing up on a 4-knob MPC (Live II): each page is four columns of four big
knobs, and each column is one Q-Link column, so the four Q-Links turn exactly the four knobs outlined on screen
(press the Q-Link button to move to the next column). Knobs carry a ring in Odin blue that shows their position from
across the room; names and values are drawn large. Choices you set and leave (types, routing, play mode) are in the
panel on the right, where MPC's Q-Link panel slides in while you turn a Q-Link.

- **Top bar (every page):** ECO and its light, the patch browser (◀ name ▶, also in MPC's PRESET menu), VOICES
  (how many voices the patch can sound right now; Odin sets it, see below), the CPU meter, STOP.
- **MAIN:** Voice (master, glide, unison detune and width), Filter 1 (cutoff, resonance, envelope, key follow, and
  its type), the amp envelope, Space (delay and reverb amounts, with their on buttons). Right: play mode, unison
  voices, velocity, bend range, arpeggiator.
- **OSC:** oscillators 1–3 (volume, SHAPE, octave, semitone; the type picker on top), fine tune for all three plus
  **TUNE A4**, the master tune (415–465 Hz, 440 in the middle; it belongs to the plugin, so it stays when you change
  patches, and is saved with the project; new notes take it). Glide is on MAIN. **SHAPE** turns whatever matters for the oscillator's type and is named after it: PW (analog), POSITION
  (wavetable, multi), VECTOR X, FM/PM AMOUNT, LOWPASS (noise). Each type's extra choice sits under its knobs (analog
  wave, wavetable, chip wave and noise, FM/PM ratios, vector Y, noise high-pass). Right: unison.
- **FILTER:** filters 1 and 2 (per voice) and 3 (on the stereo sum): cutoff, resonance, envelope, key follow, plus
  velocity and the type's own control (SEM shape, formant vowel, ring-mod amount); Drive (filter saturation,
  distortion amount and mix). Right: routing (which oscillators feed each filter, filter 1 into 2, which filters
  reach the amp) and the distortion type.
- **ENV:** amp, filter and mod envelopes (A/D/S/R), the four LFO rates. Right: the LFO waves.
- **FX:** delay, reverb, chorus, phaser, each with its on button. Right: flanger.

Left out because they are awkward on a touchscreen or rarely touched live (the patch still has them, factory
patches set them, projects keep them): the mod matrix, the arpeggiator's step editor, drawing waveforms, vector
corner waves, FM/PM carrier and modulator waves, LFO sync, effect order, per-filter gain, amp pan and gain, the
reverb EQ and the delay's high-pass and ducking.

### CPU meter, VOICES, ECO and STOP

The MPC gives a plugin about 2.9 ms to make each block of 128 samples, shared with everything else in the project.
Odin computes every voice live, so a big patch with many notes held can run out of time and crackle. Since 1.3 Odin
looks after this itself; there is no voices knob to turn.

- **CPU** shows how much of that block time Odin uses, averaged over about a tenth of a second (so a single busy
  moment, like a chord starting, doesn't read as an overload), then held and falling slowly. Green is fine; amber
  (60–80 %) is getting busy; red is where a busy project will start to crackle. It only updates a few times a
  second, so the meter itself costs next to nothing.
- **VOICES** shows how many voices the current patch may sound at once. Odin works it out: every factory patch was
  measured (calibrated on a Live II: Antarctica [RS], 4 voices at audio rate = 80 % on the meter), and while you
  play it follows what each voice really costs, so the voices fill about three quarters of the block and no more.
  A light patch gets 16 voices, a heavy one as few as 3 or 4. Your own edits are measured the same way.
  - Notes held count as voices, and so do notes still releasing; a unison patch uses several voices per note.
  - When a chord needs more voices than there are, a unison patch plays its chord notes with fewer unison voices
    each instead of dropping notes (ECO Auto, Eco and Lite); past that, the oldest note gives way, as on any synth.
  - If the load still climbs toward the limit (an edit that makes the patch heavier, a busy project), the oldest
    note fades out in 15 ms before anything crackles, and VOICES drops. It comes back up by itself once the load
    is low again.
- **ECO** chooses how much Odin may simplify to fit more voices:

  | Setting | What it does |
  |---|---|
  | **Off** | each voice exactly as Odin plays it (still within the voice budget) |
  | **Auto** (default) | modulation, oscillator pitch and filter cutoff worked out every 8 samples instead of every sample (5.5 kHz, about a quarter less work, as good as inaudible); envelopes, LFOs, FM/PM and the audio itself stay per sample. If Odin's load still climbs past the voice budget (about 85 %), new notes also start with at most 3 unison voices (2 above about 95 %, 1 if Odin runs out of time altogether), back to the full patch after 1.5 s of low load. Notes already playing are never changed |
  | **Eco** | modulation every 16 samples (about a third less work) and at most 3 unison voices per note: very close to the original, about half the CPU on the heaviest patches |
  | **Lite** | as Eco, at most 2 unison voices, and the per-voice distortion without oversampling: slightly harsher distortion on high notes |

  A patch that modulates from an oscillator or filter output (audio-rate modulation in the mod matrix) always runs
  per sample. The light next to ECO is on while ECO is cutting unison or the distortion's oversampling. ECO is saved
  with the project and stays as you set it when you change patches.
- **STOP** silences everything at once: every voice, held notes, the arpeggiator, and the delay and reverb tails.

## Build (Ubuntu, Docker)

Put this folder next to your `mpc-vst-plugins` checkout, in the same Ubuntu folder you build the other ports from:

```
~/mpc-vst-plugins
~/odin2-mpc
```

Then:

```
cd ~/odin2-mpc
./build.sh
```

That runs the kit's `tools/build_port.sh` (the armhf compile in the `arm32v7/gcc:11-bullseye` container, the skin
in the `mpc-vst-html-art` container) and packages the result with the kit's `tools/release.py` as
`dist/Odin-2-1.3.0-mpc-armv7.zip`. The first build is slow (allow 20–40 minutes): the compiler runs under ARM
emulation, and Odin's wavetable data is one very large file. If `mpc-vst-plugins` is somewhere else, run
`MPC_VST=/path/to/mpc-vst-plugins ./build.sh`.

## Install on the MPC

The zip has an installer that stops MPC, backs up `MPC.settings`, installs the plugin folder into `/sdcard/Synths`,
registers it and starts MPC again. **Save your project first.** From Ubuntu, with the MPC's IP address:

```
cd ~/odin2-mpc/dist
unzip Odin-2-1.3.0-mpc-armv7.zip
scp -r Odin-2-1.3.0 root@<mpc-ip>:/tmp/
ssh root@<mpc-ip> sh /tmp/Odin-2-1.3.0/install.sh
```

`INSTALL.md` in the zip has the same steps and the manual ones (copy the folder, edit `MPC.settings` yourself with
MPC stopped). The plugin shows up as **Odin 2** (manufacturer 3dthc) in MPC's plugin browser.
`ssh root@<mpc-ip> sh /tmp/Odin-2-1.3.0/uninstall.sh` removes it again.

## First test on the device

1. Insert Odin 2 on a track and play a few notes (the Init patch).
2. Step through some patches with ◀ ▶ and from the PRESET menu.
3. Hold a big chord on a heavy patch (Antarctica [RS], Alien Chaos2): VOICES shows what it allows, CPU stays out
   of the red, and it plays without crackling.
4. Press STOP while a long reverb/delay tail is ringing.
5. On OSC, turn TUNE A4 away from 440 Hz and play: the pitch follows; change patch, it stays.
6. Turn every page and the Q-Links; change an oscillator and a filter type and check the panel follows.
7. Save the project, reload it, and check the patch, your edits and the tune came back.

## Folder

| Path | What |
|---|---|
| `build.sh` | build and package (above) |
| `vst/vst.json` | the port's build description for mpc-vst-plugins |
| `vst/make_params.py` → `vst/params.json`, `src/odin_mpc_params.inc` | the controls the MPC sees and the Odin state each drives |
| `vst/make_skin.py` → `vst/layout.conf`, `vst/art/gen/` | the pages, drawn from Odin's artwork in `vst/art/odin/` |
| `src/odin_mpc.cpp` | the engine the kit's VST2 wrapper drives: controls, patches, project state, voice budget, ECO, STOP, CPU meter |
| `src/odin_state.h` | a patch as text lines (the bank and the project state store differences) |
| `src/presets/` | the factory bank, generated by `tools/make_bank.sh` from `presets_src/`, and each patch's measured load (`bank_cost.inc`, generated by `tools/patch_cost.sh`) |
| `src/odin/` | Odin 2's engine, vendored; what changed is in `src/VENDORED.md` |
| `tools/engine_test.sh` | offline engine test (bank vs Odin's loader, state round trip, STOP, ECO, voice budget, timings) |
| `tools/patch_cost.sh` | measures every factory patch's load and writes `src/presets/bank_cost.inc` |

Regenerate after editing: `python3 vst/make_params.py` (parameter list), `python3 vst/make_skin.py` (pages, needs
Pillow), `tools/make_bank.sh` (bank, then `tools/patch_cost.sh` for its costs). Keep the parameter order: projects store values by position; add new controls
at the end of `make_params.py`.

## Licences

GPL-3.0-or-later (`LICENSE`), as Odin 2. Odin 2 and its artwork and patches are by TheWaveWarden and contributors.
The Aldrich font is under the SIL Open Font License (`vst/art/fonts/OFL.txt`). Surge's tuning library is MIT.
