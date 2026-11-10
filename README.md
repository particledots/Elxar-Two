# ELXAR-Two (work in progress)

An unofficial Audio Unit (for Logic Pro) and VST3 instrument based on the open-source firmware of the Erica Synths
LXR-02 drum synthesizer. **Not made, approved or supported by Erica Synths.** Do not contact them about this plugin.
ELXAR-Two is a modified port and is marked as such.

## Status
Milestone 1 (done, tested Nov 9, 2026): the LXR-02 sound engine runs inside the plugin and there is a rough, plain
interface. Tested in Logic Pro 11.2 on an Apple silicon Mac (macOS Tahoe): all six voices sound from the Hit buttons
and from the keys C1 to B1; the step grid follows Logic's transport and tempo and also runs in "Always" mode; the
sliders, Mute and Solo work; all five effects work on the voices sent to them; the pattern generator works; dragging
the MIDI pattern from the plugin window onto a Logic track works (and so does dragging the file from Finder);
settings and patterns come back after saving and reopening the project; a second instance shows its warning and
stays silent. Not yet tested on the Intel Mac (planned for Milestone 3).

- Six voices (Drum 1, Drum 2, Drum 3, Snare, Hi-hat, Cymbal) from the firmware's own sound code, starting with
  the firmware's built-in "init kit".
- Per voice: Mute, Solo, "To FX", a Hit button, and sliders for Pitch, Decay, Tone, Level and Pan (the hi-hat
  also has Open, the decay of the open hi-hat).
- Play the voices from the keyboard or from a MIDI region: C1 Drum 1, D1 Drum 2, E1 Drum 3, F1 Snare,
  G1 Hi-hat closed, A1 Hi-hat open, B1 Cymbal (Logic's note names; MIDI notes 36, 38, 40, 41, 43, 45, 47).
- A 16-step grid (7 rows, 4 patterns) that follows Logic's tempo and transport, or runs always.
- A simple pattern generator (Density, Variation, Euclid hits) and a "Drag MIDI out" test box.
- One effect at a time (Drive, Ring mod, Compressor, Delay or Reverb) for the voices with "To FX" on, and a
  master level.
- Settings and patterns are stored with the Logic project.

Milestone 0 (done, tested Nov 9, 2026): the empty instrument shell loaded and played a test tone in Logic Pro 10.7.8
(Intel Mac, macOS 12.7.8) and Logic Pro 11.2 (Apple silicon Mac, macOS Tahoe).

## Known limits (Milestone 1)
- **One ELXAR-Two per Logic project.** The firmware keeps its state in one shared place, so only the first
  instance makes sound; a second one shows a red warning and stays silent.
- The LXR-02 has **one effect unit**, not separate sends: a voice goes either straight to the output or
  completely through the effect. The Reverb exists in the firmware but is switched off in the hardware; it is
  switched on here and should be treated as experimental.
- The step grid and the pattern generator are simple stand-ins. The firmware's own sequencer (64 steps,
  probability, parameter locks, its Euclidean generator) is planned for Milestone 2.
- Only a handful of the roughly 240 sound parameters have sliders so far.
- The window has a fixed size (a resizable window is planned).
- The Intel build needs an Intel processor from 2012 or later.
- There is no hardware LXR-02 to compare against; the port is checked against the source code only.

## How it is put together
- `third_party/lxr-firmware`: the firmware source. Modified since Milestone 1; every change is marked
  `ELXAR-Two change` and listed in `third_party/lxr-firmware/ELXAR-CHANGES.md`.
- `src/engine/shim`: small stand-ins for the STM32 hardware headers.
- `src/engine/elxar_engine.c/.h`: plain-C front door to the engine. `src/engine/ElxarEngine.h`: converts the
  engine's fixed 44.14 kHz output to the host's sample rate.
- `tools/engine_test`: a headless test that renders drum hits to WAV files and checks them. GitHub runs it on
  every build and keeps the WAV files as the `ELXAR-Two-test-renders` artifact.
- `src/PluginProcessor.*`, `src/PluginEditor.*`: the plugin and its window (JUCE 8).

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
