# ELXAR-Two (work in progress)

An unofficial Audio Unit (for Logic Pro) and VST3 instrument based on the open-source firmware of the Erica Synths
LXR-02 drum synthesizer. **Not made, approved or supported by Erica Synths.** Do not contact them about this plugin.
ELXAR-Two is a modified port and is marked as such.

## Status
Milestone 2 (done, version 0.2.0, tested Nov 10, 2026): everything that plays and saves. Tested on both test Macs:
Logic Pro 10.7.8 on macOS 12.7.8 (Intel) and Logic Pro 11.2 on macOS Tahoe (Apple silicon). All 21 checks passed: the
sequencer follows Logic's transport, tempo changes and jumps to another bar; grid editing, row lengths, bars 2 to 4,
the step editor (velocity, probability, flam, shift, pitch, automation), shuffle, pattern change, copy / paste /
clear, both generators, drag MIDI out, "Always" mode, the Sound page, kit store / recall / init, automation in Logic,
all window sizes, save and reopen, and a Milestone 1 project all work. No clicks, dropouts or stuck notes; Logic's
CPU meter was low (about 7 %). This was also the first test of the Intel build's sound. One part of check 4 is not
confirmed: looping with Logic's Cycle switched on (jumping between bars was tested and stays in time).

- **The firmware's own step sequencer** now plays the patterns, locked to Logic's song position and tempo (or
  running always). 16 patterns; 7 rows; up to 64 steps per row (4 bars of 16), and every row can have its own
  length. Per step: velocity, probability, pitch, flam (x2 to x8), shift (earlier or later), and two per-step
  automation slots (a sound parameter jumps to a value when the step plays). Shuffle per pattern. A pattern
  chosen while the sequencer runs starts when the playing pattern reaches its end.
- **Pattern generator** built on the firmware's Euclidean generator: "Generate beat" for a whole pattern, and
  "Fill selected row" (length, hits, rotate) for one row.
- **Drag MIDI out**: the pattern as a MIDI file (1 to 8 bars) with the row lengths, flams, shift, shuffle and
  probability worked out the same way the sequencer plays them.
- **Sound page**: every sound parameter of each voice (about 35 per voice: oscillator, envelopes, FM or
  modulation oscillators, transient, filter, velocity, LFO, mix), named in full words.
- **Kits**: the complete sound (all 247 firmware sound parameters) is saved with the Logic project, and there are
  eight kit slots inside the project (Store kit, Recall kit, Init kit).
- **Automation in Logic**: the main knobs are plugin parameters and can be automated: per voice Pitch, Decay,
  Tone, Level, Pan, Mute, Solo, To FX; the open hi-hat decay; effect type, dry/wet and the four effect knobs;
  master level.
- **Window size**: a "Window size" list (75 to 150 %) and a drag corner at the bottom right. The size is saved
  with the project.
- Still one effect unit with the "To FX" buttons; it gained a "Kind" list (drive type, ring-mod waveform,
  compressor ratio, delay type).
- Play the voices from the keyboard or from a MIDI region: C1 Drum 1, D1 Drum 2, E1 Drum 3, F1 Snare,
  G1 Hi-hat closed, A1 Hi-hat open, B1 Cymbal (Logic's note names; MIDI notes 36, 38, 40, 41, 43, 45, 47).
- Projects saved with Milestone 1 open in this version (their 4 patterns and slider settings are kept).

Milestone 1 (done, tested Nov 9, 2026): the LXR-02 sound engine runs inside the plugin and there is a rough, plain
interface. Tested in Logic Pro 11.2 on an Apple silicon Mac (macOS Tahoe): all six voices sound from the Hit buttons
and from the keys C1 to B1; the step grid follows Logic's transport and tempo and also runs in "Always" mode; the
sliders, Mute and Solo work; all five effects work on the voices sent to them; the pattern generator works; dragging
the MIDI pattern from the plugin window onto a Logic track works (and so does dragging the file from Finder);
settings and patterns come back after saving and reopening the project; a second instance shows its warning and
stays silent. (The Intel Mac was first tested with sound in Milestone 2.)

Milestone 0 (done, tested Nov 9, 2026): the empty instrument shell loaded and played a test tone in Logic Pro 10.7.8
(Intel Mac, macOS 12.7.8) and Logic Pro 11.2 (Apple silicon Mac, macOS Tahoe).

## Known limits (Milestone 2)
- **One ELXAR-Two per Logic project.** The firmware keeps its state in one shared place, so only the first
  instance makes sound; a second one shows a red warning and stays silent. Decided again in Milestone 2: this
  stays as it is for now (see CHANGELOG).
- The LXR-02 has **one effect unit**, not separate sends: a voice goes either straight to the output or
  completely through the effect. The Reverb exists in the firmware but is switched off in the hardware; it is
  switched on here and should be treated as experimental.
- **What the plugin uses from the firmware sequencer, and what it leaves out.** Used: steps, row lengths,
  probability, flam, shift, per-step pitch and automation, shuffle, pattern changes, the Euclidean generator.
  Replaced: the clock (the firmware's millisecond timer is replaced by Logic's song position). Left out, because
  they belong to the hardware's buttons and SD card: song mode (pattern chains), live recording and erasing,
  roll, a kit per pattern, 64 patterns (the plugin offers the first 16), MIDI out and trigger outputs.
- The firmware's Euclidean generator only works up to 16 steps (its menu allows 1 to 16), so "Fill selected row"
  is limited to 16. Longer rows can be drawn by hand.
- The dragged MIDI file cannot carry per-step pitch or per-step automation (those only exist inside the
  sequencer). The sequencer always starts counting at the beginning of the Logic project (bar 1).
- The sliders show the firmware's raw values (0 to 127); the hardware shows some of them as -63 to +63.
- Tempo-synced LFOs use the tempo rounded to a whole BPM (as the firmware does). The sequencer itself follows
  Logic's exact tempo.
- The Intel build needs an Intel processor from 2012 or later.
- There is no hardware LXR-02 to compare against; the port is checked against the source code only.

## How it is put together
- `third_party/lxr-firmware`: the firmware source. Modified since Milestone 1; every change is marked
  `ELXAR-Two change` and listed in `third_party/lxr-firmware/ELXAR-CHANGES.md`.
- `src/engine/shim`: small stand-ins for the STM32 hardware headers.
- `src/engine/elxar_engine.c/.h`: plain-C front door to the engine and the sequencer, plus empty stand-ins for
  the front panel, SD card and MIDI ports. `src/engine/ElxarEngine.h`: converts the engine's fixed 44.14 kHz
  output to the host's sample rate, and (`SeqClock`) turns the host's song position into the 24 clock pulses per
  step that the firmware sequencer expects.
- `tools/engine_test`: a headless test that renders drum hits to WAV files and checks them, and checks the
  sequencer step by step (timing, row lengths, flam, shift, probability, pattern change, automation, Euclid).
  GitHub runs it on every build and keeps the WAV files as the `ELXAR-Two-test-renders` artifact.
- `tools/plugin_test`: a second test that runs the real plugin code with a pretend host (tempo, song position,
  start, stop, jumps) and checks the timing of the hits, pattern changes, saving and loading (also a Milestone 1
  project), kits, the MIDI file and the generator. GitHub runs it on every build as well.
- `src/PluginProcessor.*`, `src/PluginEditor.*`: the plugin and its window (JUCE 8). The plugin keeps its own copy
  of the patterns and the kit (that copy is what is saved); the audio thread copies changes into the firmware.
- `src/SoundParams.h`: the list of sound parameters per voice with their names (taken from the hardware's menu
  pages).

## Get a build
`.github/workflows/build.yml` builds the AU and VST3 (universal, macOS 11+) on GitHub. Download `ELXAR-Two-macOS`
from the latest run's Artifacts (Actions tab), unzip, and double-click `Install-ELXAR-Two.command`.
`Uninstall-ELXAR-Two.command` removes it.

## Thanks
- LXRXXL by L'Cronx (the same engine as a plugin for Akai MPC / Force) showed which firmware files a plugin needs.
- OpenElixir-02 by Moritz Waser found and fixed memory errors in the stock firmware; those fixes are applied here.

## Licence
GPL-3.0, with the additional terms of the LXR firmware (see `LICENSE`, `THIRD-PARTY-LICENSES.txt` and
`third_party/lxr-firmware/NOTICE.md`).
