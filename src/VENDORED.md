# Vendored Odin 2 source

`src/odin/` is Odin 2's synthesizer engine, copied from
[TheWaveWarden/odin2](https://github.com/TheWaveWarden/odin2) at commit
`265e9e227581357efb87ed7217381f8a40c5e2bf` (2025-05-30, Odin 2.3.4), GPL-3.0-or-later (`LICENSE` at this folder's
root). It is committed here so the build never fetches anything; a re-vendor from a newer upstream is a diff against
this list.

## What was copied

- `Source/audio/` (oscillators, filters, envelopes, LFOs, effects, wavetable data), minus files nothing uses here:
  `ImpulseResponseCreator.*`, `FX/FeedbackDelayNetwork.*`, `FX/SurgeReverb.*`, `FX/Bitcrusher.*`,
  `FX/IntegerDelayLine.*`.
- `Source/gui/ModMatrix.*` and `Source/gui/OdinArpeggiator.*` (audio code that upstream keeps under `gui/`).
- The processor: `Source/PluginProcessor*.cpp`, `PluginProcessor.h`, `AudioValueTree.h`, `AudioVarDeclarations.h`,
  `ProcessorInitializerList.h`, `AudioParameterConnections.h`, `GlobalIncludes.h`, `OdinTreeListener.h`.
- `libs/tuning-library/` (Surge's tuning library, MIT; its tests and command-line tools removed).
- `presets_src/` (this folder's root): Odin's factory patches (`assets/Soundbanks`) and `init_patch.odin`, the input of
  `tools/make_bank.sh`.
- `vst/art/odin/`: the GUI images the MPC page is made from (`assets/graphics`: panels, knob filmstrips joined into
  strips, buttons, LEDs, faders, the chassis/logo image), and `vst/art/fonts/` Odin's font Aldrich (SIL OFL, its
  `OFL.txt` alongside).

## What was changed

JUCE is replaced by `JuceLibraryCode/JuceHeader.h`, a small stand-in for what the code uses (String, Identifier,
var, ValueTree with JUCE's binary patch format, the parameter tree, Decibels, JUCE's fast math approximations,
MidiMessage). Odin's own files changed as little as possible:

| File | Change |
|---|---|
| `GlobalIncludes.h` | GUI-only includes, typedefs and helper classes removed |
| `PluginProcessor.h` | no `AudioProcessor` interface or editor; `prepare()`, `processStereo()`, `loadPatchFromMemory()`, `savePatchToMemory()`, `panic()`, `applyFXOrderFromTree()` added; the private section made public for the MPC engine; the drawn-table cache members |
| `PluginProcessor.cpp` | includes `PluginProcessorMPC.cpp` instead of the editor |
| `PluginProcessorProcess.cpp` | `processBlock(AudioBuffer, MidiBuffer)` became `processStereo(float*, float*, frames, raw MIDI)`; the host tempo comes from the wrapper; the sample loop is unchanged |
| `PluginProcessorConstructor.cpp` | no JUCE `AudioProcessor` base or wrapper-type check |
| `PluginProcessorMisc.cpp` | JUCE plugin methods (programs, editor, chunk XML) removed; GUI callbacks removed |
| `PluginProcessorMidi.cpp` | JUCE plugin capability methods removed |
| `PluginProcessorNonAudioParams.cpp` | no documents-folder default for the (unused) patch browser path |
| `PluginProcessorReadPatch.cpp` | `createDrawTablesFromValueTree()` rebuilds a drawn table only when its values changed |
| `audio/Voice.h` | `VoiceManager::m_voice_limit`: how many of the 24 voices new notes may use (the plugin's voice budget); `m_steal_from_stacks`: steal from a unison stack before taking a note's last voice; `voiceByAge()`, `getPlayMode()`; `MIDINoteToFreq()` times the plugin's master tune (`m_master_tune`, 1 = A4 at 440 Hz) |
| `audio/ADSR.*` | `fastRelease()`: release in at most 15 ms (the voice budget fading out the oldest note) |
| `audio/Oscillators/Oscillator.h`, `MultiOscillator.cpp`, `AnalogOscillator.cpp`, `DriftGenerator.*` | control rate: `m_ctrl_steps`, the samples since the last `update()`; glide and drift advance by all of them (same result at audio rate) |
| `PluginProcessorProcess.cpp` (control rate) | with `m_ctrl_rate` > 1 the mod matrix, the wavetable-based oscillators' `update()` and the filters' `update()` run once every `m_ctrl_rate` samples and on every sample with MIDI; never for a patch whose mod matrix reads an oscillator or filter output (`ModMatrix::readsAudio()`) |
| `audio/Oscillators/WavetableContainer.cpp` | drawn tables built in one pass with a sine table (same output to ~1e-15, about 10x faster; it runs while audio waits on a patch load); a loop bound fixed (upstream indexed past the 13 LFO tables) |
| `audio/Filters/VAOnePoleFilter.*` | `update()` skips its `tan()` when the frequency hasn't changed (same coefficients) |
| `gui/ModMatrix.*` | `zeroAllDestinations()` clears the whole destination block only after the matrix changes, otherwise just the destinations the active slots write (the only ones that can be non-zero) |
| `audio/FX/OversamplingDistortion.*` | the downsampling filter's history in local variables for its three steps (same arithmetic, same output); ECO Lite: `setLite()` distorts without the 3x oversampling |
| `PluginProcessorMidi.cpp`, `PluginProcessor.h` | ECO: `m_unison_cap`, the most unison voices a new note may start (rounded down to one of Odin's own unison counts), and `setDistortionLite()`; `m_unison_fit`: a note starts no more unison voices than are free within the voice limit; `fastReleaseVoice()`; `m_ctrl_rate` |
| added `PluginProcessorMPC.cpp` | prepare, patch load/save in Odin's own format, STOP, FX order from the patch (upstream only the editor applied it) |
| added `InitPatchData.h` | `assets/init_patch.odin` as bytes (upstream's BinaryData) |

With `m_ctrl_rate` = 1 (ECO Off), `m_unison_fit` and `m_steal_from_stacks` off and the voice limit at 24, the engine
is unchanged by all of the above. The speed changes were checked sample for sample: with Odin's clock-seeded random generator pinned, the engine's
output is identical to the unchanged code's on the factory patches tried (heaviest ones included), and about a fifth
fewer instructions on Alien Chaos2 (with `-O3 -fno-math-errno`, which don't change floating-point results either).

Everything MPC-specific outside `src/odin/` is this port's own: `src/odin_mpc.cpp` (the engine the
mpc-vst-plugins wrapper drives), `src/odin_state.h` (patch state as text), `src/odin_mpc_params.inc` and
`vst/params.json` (generated by `vst/make_params.py`), `src/presets/` (generated by `tools/make_bank.sh`).
