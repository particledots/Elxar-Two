# ELXAR-Two

An unofficial Audio Unit (for Logic Pro) and VST3 drum synthesizer instrument for macOS, based on the open-source
firmware of the Erica Synths LXR-02. **Not made, approved or supported by Erica Synths.** Do not contact them about
this plugin. ELXAR-Two is a modified port and is marked as such.

Six voices (three drums, snare, hi-hat, cymbal), the firmware's effect unit, and the firmware's own step sequencer
locked to Logic's transport, in a window with one colour per voice.

## Status
Version 1.1.0: kit files and Random kit (added on top of the finished 1.0.0). Tested in Logic on both test Macs on
Nov 10, 2026: all 9 checks answered YES (see Test history). Versions 1.0.0 and 1.1.0 are complete.

## Install
1. Download `ELXAR-Two-macOS` from the latest run's Artifacts (GitHub, Actions tab) and unzip it.
2. Double-click `Install-ELXAR-Two.command`. It asks for the Mac password once and installs the Audio Unit and the
   VST3 for all users, replacing an older copy.
3. Open Logic. ELXAR-Two is under Instrument > AU Instruments > particledots. If it is missing: Logic's Settings >
   Plug-in Manager > Reset & Rescan Selection.

`Uninstall-ELXAR-Two.command` removes the plugin again. GitHub deletes a run's download after 90 days, so keep the
zip if you want to install again later. The build is universal (Apple silicon and Intel), needs macOS 11 or later,
and is not signed with an Apple developer ID: on a Mac that downloaded it from the internet, macOS may ask for
approval the first time (`INSTALL.txt` in the zip says what to do).

## The window
- **Top bar**: Pattern list (1 to 16) with "playing pattern N, next: M"; "Sequencer runs" (with Logic's transport, or
  always); "Window size" (75 to 150 %, or drag the bottom right corner). Tempo always follows Logic.
- **Six voice strips**, each in its own colour: Drum 1 coral, Drum 2 orange, Drum 3 yellow, Snare green, Hi-hat
  cyan, Cymbal violet. Each has Mute, Solo, To FX and Hit buttons and the faders Pitch, Decay, Tone, Level and Pan
  (the hi-hat also has Open, the decay of the open hi-hat). A strip is dimmed while its voice is silent (muted, or
  another voice is soloed).
- **Pattern page**: the step grid (7 rows, the hi-hat has a closed and an open row), 16 steps of one bar at a time
  with Bar buttons 1 to 4, a length per row (1 to 64 steps), Shuffle, Copy / Paste / Clear, a step editor for the
  selected step (velocity, probability, pitch, flam, shift, two automation slots), the beat generator, the Euclid
  box for one row, and the MIDI file box ("DRAG MIDI OUT": drag it onto a Logic track).
  - Click a square: step on or off (drag to paint). Right-click a square (or ctrl-click / option-click): select it
    without changing it. Click a row's name: select the row. The step that is playing lights up; the selected step
    has a white frame. Letters on a step: F flam, < or > shifted, % does not always play, N pitch changed,
    A automation.
- **Sound page**: every sound parameter of one voice, and eight kit slots (Store kit, Recall kit, Init kit). Under
  the slots: the **name of the kit** (8 letters, like the hardware), **Load kit file...** (an LXR-02 kit file, `.SND`;
  or drag a `.SND` file onto the page) and **Random kit** with a **How wild** slider. A kit slot remembers the kit's
  name. Loading a kit or making a random kit replaces the whole sound (all six voices and the effect); the patterns
  are not touched.
- **Effect and master**: one effect at a time (Drive, Ring mod, Compressor, Delay, Reverb) for the voices with
  "To FX" switched on, with Kind, Dry / wet and four knobs whose names change with the effect; Master level.
- **Keys**: C1 Drum 1, D1 Drum 2, E1 Drum 3, F1 Snare, G1 Hi-hat closed, A1 Hi-hat open, B1 Cymbal (Logic's note
  names; MIDI notes 36, 38, 40, 41, 43, 45, 47). The dragged MIDI file uses the same notes.
- **Automation in Logic**: per voice Pitch, Decay, Tone, Level, Pan, Mute, Solo, To FX; the open hi-hat decay;
  effect type, dry/wet and the four effect knobs; master level.
- Everything (all 247 sound parameters, 16 patterns, 8 kit slots, the window size) is saved with the Logic project.

## Known limits
- **One ELXAR-Two per Logic project.** The firmware keeps its state in one shared place, so only the first
  instance makes sound; a second one shows a red warning and stays silent. This is final (decided Nov 10, 2026):
  several instances are not planned.
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
- **Kit files.** Only `.SND` kit files are read (the hardware's kit: 8 name bytes + 247 values = 255 bytes; layout
  checked against the firmware's preset manager). Pattern, song and project files are refused. A short file (older
  firmware) loads with the missing values set to 0, as the hardware does. Values the window cannot show (a list
  position past the end of its list, a number above 127) are brought back into range, and the status line says how
  many. Kits from other people are their work: they are not part of ELXAR-Two and are not in this repository.
  Not built: saving a kit as a `.SND` file, reading pattern files, a kit per pattern.
- **Random kit** is a guess from the firmware's parameter meanings, made without a hardware LXR-02 to listen to.
  The automatic test checks that its kits are not silent, not broken and not clipping; whether they sound good is
  for the ear. It leaves volume, pan, "To FX", the effect, LFOs and velocity targets at the starting kit's values.
  Found while testing: on the hi-hat and the cymbal the Pulse waveform is silent at pulse width 0 (and Noise on
  the hi-hat's main oscillator is nearly silent); Random kit avoids them.
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
  project), kits, the MIDI file and the generator, and that the window opens, draws itself and closes. GitHub runs
  it on every build as well.
- `src/ElxarLook.h`: the look of the window (colours, faders, buttons). An original design, not a copy of the
  hardware panel.
- `src/PluginProcessor.*`, `src/PluginEditor.*`: the plugin and its window (JUCE 8). The plugin keeps its own copy
  of the patterns and the kit (that copy is what is saved); the audio thread copies changes into the firmware.
- `src/SoundParams.h`: the list of sound parameters per voice with their names (taken from the hardware's menu
  pages).

## How a build is made
`.github/workflows/build.yml` runs on GitHub with every change: the engine test, the plugin test, then the AU and
VST3 (universal, macOS 11+, ad-hoc signed), zipped as `ELXAR-Two-macOS` with the install and uninstall scripts, the
licence texts, the list of firmware changes and `SOURCE.txt` (which names the exact commit the build was made from).

## Test history
Version 1.1.0 (done, tested Nov 10, 2026): kit files and Random kit. Automatic tests passed on every run (kit test,
plugin test, engine test, memory checker). Tested on both test Macs: Logic Pro 10.7.8 on macOS 12.7.8 (Intel) and
Logic Pro 11.2 on macOS Tahoe (Apple silicon). All 9 checks answered YES: the title shows version 1.1.0; a new
instance shows "Kit: Initkit"; Random kit pressed six times with the sequencer running gave sound on every voice and
nothing harsh or clipping (no bad kit to report); "How wild" at 0 % sounds like the starting kit; Store kit in slot 3,
another Random kit, then Recall kit brings back the earlier sound and name, and the slot list shows the name; kit name
and slot name survive save and reopen; "Load kit file..." with a `.SND` file changes the sound; dragging a `.SND` file
onto the Sound page loads it, and a `.PAT` file gets a red message with the sound unchanged; an LFO target of
"Hi-hat: Filter / Frequency" (a target above 127) survives Store kit, Init kit and Recall kit. Nothing odd reported.
Status line after loading real kit files (photos from Bryn, later the same evening): most kits only say "Loaded kit
NAME" (for example "Loaded kit SpaceD"); the kit "Necro" says "Loaded kit Necro - 1 value outside the range the window
can show was changed" (its cymbal transient sound is outside the list and goes to the first one, as in the firmware).
Bryn noticed this message on no other kit.

Milestone 3 (done, version 1.0.0, tested Nov 10, 2026): looks and release. Tested on both test Macs: Logic Pro 10.7.8
on macOS 12.7.8 (Intel) and Logic Pro 11.2 on macOS Tahoe (Apple silicon). All 20 checks passed: install script
("Installed" twice, then "Done"); a project saved with 0.2.0 opens with its patterns and sounds; the title shows
version 1.0.0; each strip and grid row has its voice colour; all lettering readable on the Pattern and Sound pages;
Mute (white, strip dimmed, voice silent), Solo and the Hit buttons; playback in time with the playing step moving;
Logic's Cycle over bars 1 to 2 and 3 to 4, looping at least four times, with no missing or doubled hit, click or
hanging sound (this closes the part of Milestone 2 check 4 that was open); step editing, selected-step frame and
velocity; row length over two bars; pattern change at the end of the pattern; both generators; drag MIDI out; the
delay effect on the snare; kit store / init / recall; all window sizes and the drag corner; save and reopen; the
red warning and silence of a second instance. No sound problems; colours as expected.

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

## Thanks
- LXRXXL by L'Cronx (the same engine as a plugin for Akai MPC / Force) showed which firmware files a plugin needs.
- OpenElixir-02 by Moritz Waser found and fixed memory errors in the stock firmware; those fixes are applied here.

## Licence
ELXAR-Two is free software under the GNU General Public License version 3 (`LICENSE`), with the additional terms of
the LXR firmware (`third_party/lxr-firmware/NOTICE.md`). It comes with no warranty.

Licence check for version 1.0.0 (Nov 10, 2026; made against the licence texts, it is not legal advice):
- **LXR firmware** (Erica Synths, originally Julian Schmidt / Sonic Potions): GPL-3.0-only with two extra terms.
  (1) The names "Erica Synths", "LXR" and "LXR-02" may not be used in a way that suggests the plugin comes from or
  is approved by Erica Synths: the plugin is called ELXAR-Two and says "unofficial" and "not affiliated" in its
  window and here. (2) Modified versions must be marked: every change carries the words `ELXAR-Two change` and is
  listed in `third_party/lxr-firmware/ELXAR-CHANGES.md`. The hardware design (panel, artwork) is not licensed: the
  window is an original design.
- **JUCE 8.0.6**: used under its AGPL-3.0 option (JUCE is offered under AGPL-3.0 or a paid licence). GPL-3.0 and
  AGPL-3.0 code may be combined in one program (section 13 of both licences). No paid JUCE licence is needed as long
  as the plugin stays open source.
- **Inside JUCE**: Apple's Audio Unit SDK (Apache 2.0), Steinberg's VST3 SDK (used under its GPL-3.0 option), and
  small libraries under permissive licences (zlib, libpng, jpeglib, FLAC, Ogg Vorbis, HarfBuzz, SheenBidi).
- **What follows**: using the plugin yourself needs nothing more. Anyone who passes a build on must pass on the
  licence texts with it and offer the complete source; the download already contains both (`SOURCE.txt` points to
  the exact commit in this public repository). The plugin cannot be made closed source.

Details: `THIRD-PARTY-LICENSES.txt`. VST is a registered trademark of Steinberg Media Technologies GmbH. Audio
Units, Logic Pro and macOS are trademarks of Apple Inc.
