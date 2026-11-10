# Changelog

## 0.2.0 (unreleased): Milestone 2
- The firmware's own step sequencer (`sequencer.c`) and Euclidean generator (`EuklidGenerator.c`) are compiled into
  the plugin and play the patterns. The stand-in grid player of Milestone 1 is gone.
  - Kept from the firmware: which step plays, a length per row (1 to 64 steps), probability, flam, shift, per-step
    pitch, two per-step automation slots, shuffle, pattern changes at the end of the pattern.
  - Replaced: the clock. The firmware's millisecond timer is replaced by 24 pulses per step worked out from Logic's
    song position (`SeqClock`), so the pattern stays locked to Logic, also after a jump or a cycle. Shuffle is timed
    the way the firmware's clock does it.
  - Left out (they depend on the hardware's buttons and SD card): song mode, live recording, roll, a kit per pattern,
    patterns 17 to 64, MIDI out and trigger outputs.
- Window: a Pattern page (bar buttons, shuffle, copy / paste / clear, the grid with a length per row, a step editor
  for the selected step, the generator and the MIDI file box) and a Sound page (every sound parameter of each voice,
  kit slots). The six voice strips and the effect section are always visible.
- Pattern generator rebuilt on the firmware's Euclidean generator ("Generate beat", "Fill selected row").
- Drag MIDI out now writes what the sequencer would play: row lengths, flams, shift, shuffle, probability; 1 to 8 bars.
- Kits: all 247 sound parameters are saved with the Logic project; eight kit slots in the project; Init kit.
- 16 patterns (were 4), all saved with the project. Projects saved with Milestone 1 still open.
- Effect section: new "Kind" list (drive type, ring-mod waveform, compressor ratio, delay type).
- Window size: "Window size" list (75 / 100 / 125 / 150 %) and a drag corner; saved with the project.
- Decision: still one plugin instance per project. Allowing several needs every instance's copy of the firmware's
  shared memory to be swapped in and out for each audio block, behind a lock, because Logic renders tracks on
  several processor cores at once. That is a change to the foundation which cannot be checked without Logic, so it
  is not mixed into this milestone.
- Firmware changes (all marked, listed in `third_party/lxr-firmware/ELXAR-CHANGES.md`): pattern edits are kept when
  the pattern changes; three sequencer faults from the firmware review fixed (automation taken from the wrong step,
  one track's shifted hit dropping other tracks' hits, reading step -1 / dividing by a length of 0); added functions
  to follow the host position, to end automation on stop, and to get a Euclidean pattern back as a value.
- Finding: the firmware's Euclidean generator never finishes, or gives the wrong number of hits, for lengths from 18
  (its menu only allows 1 to 16). The plugin limits it to 16.
- New plugin test (`tools/plugin_test`): runs the real plugin code with a pretend host and checks transport
  following (hits land 1 to 2 ms after the grid line, the same small delay as in Milestone 1), a start in the middle
  of a bar, a jump back to bar 1, row lengths, pattern change, "Always" mode, keys, Mute, kits, saving and loading
  (including a Milestone 1 project), the MIDI file and the generator. Runs on GitHub with every build.
- Engine test: new sequencer checks (step timing, row length, song position, flam, shift, probability, pattern
  change, per-step automation, Euclid for all lengths 1 to 16), a sequenced beat rendered through the same clock code
  the plugin uses, and the random stress test now also drives the sequencer.
- Result (Nov 10, 2026): tested on both test Macs, Logic Pro 10.7.8 on macOS 12.7.8 (Intel) and Logic Pro 11.2 on
  macOS Tahoe (Apple silicon). All 21 checks passed: version shows 0.2.0; the beat follows Logic's transport, tempo
  changes and jumps to another bar; grid editing, row lengths and bars 2 to 4 work; velocity, probability, flam,
  shift, pitch and per-step automation are audible; shuffle swings; pattern change happens at the end of the pattern;
  copy, paste and clear work; "Generate beat" and "Fill selected row" work; the dragged MIDI region matches the
  pattern including flam and shift; "Always" mode works; the Sound page, kit store / recall / init, Logic automation
  of a strip slider and all window sizes work; everything comes back after save and reopen; a Milestone 1 project
  opens with its patterns and settings. No audible clicks or glitches; CPU meter about 7 %. First test of the Intel
  build's sound: passed. Not confirmed: looping with Logic's Cycle switched on (the tester was unsure where Cycle is;
  jumping between bars was tested). Milestone 2 is complete.

## 0.1.0 (unreleased): Milestone 1
- The LXR-02 sound engine now runs in the plugin. The firmware's sound code is compiled with small stand-ins for
  the STM32 hardware (`src/engine/shim`) and a plain-C front door (`src/engine/elxar_engine.c`).
- The firmware source is now modified (marked `ELXAR-Two change`, listed in
  `third_party/lxr-firmware/ELXAR-CHANGES.md`): memory-error fixes known from OpenElixir-02 and LXRXXL, a wavetable
  read past the end of its table, and number conversions made to behave like the hardware's ARM chip on both
  Intel and Apple silicon. Added `Cc2Text.c` and `initKit.c` from the same firmware commit; removed `main.h` and
  `delay.h` (hardware only).
- Sample-rate conversion from the engine's fixed 44.14 kHz to the host rate (`src/engine/ElxarEngine.h`).
- Headless `engine_test`: renders each voice and a beat through every effect to WAV, then stress-tests random
  settings. Runs on GitHub with every build; the WAV files are kept as the `ELXAR-Two-test-renders` artifact.
- Rough interface: six voice strips, 16-step grid (4 patterns) following Logic's tempo and transport, simple
  pattern generator, effect and master section, and a "Drag MIDI out" test box.
- Decision: one plugin instance per project for now (a second instance stays silent and says so).
- Finding: the LXR-02 has one effect unit (a voice is routed through it or not), not separate delay and reverb
  sends; the reverb is switched off in the hardware firmware and switched on here as experimental.
- Settings and patterns are stored with the Logic project.
- Fixed during the build: the firmware's reverb wrote one sample past its output buffer with a mono effect routing
  (found by the engine test on Apple silicon).
- Result (Nov 9, 2026): tested in Logic Pro 11.2 on macOS Tahoe (Apple silicon). All 14 checks passed: the plugin loads
  as version 0.1.0; all six voices sound from the Hit buttons and from the keys C1 to B1; the step grid follows Logic's
  transport and tempo and runs in "Always" mode; grid editing, sliders, Mute and Solo work; Delay, Drive, Compressor,
  Ring mod and Reverb work; Generate makes playable beats; dragging MIDI out of the plugin window onto a Logic track
  works, and so does the Finder fallback; settings and patterns survive save and reopen; a second instance warns and
  stays silent. No clicks, crackles or level problems reported. Milestone 1 is complete.
- Requested for later: an option to resize the window.

## 0.0.1 (unreleased): Milestone 0
- Imported the Erica Synths LXR-02 firmware source (unmodified, commit a43cd4f) into `third_party/lxr-firmware`.
- Empty AU instrument and VST3 shell (JUCE 8.0.6), stereo output, placeholder tone on MIDI notes.
- GitHub Actions build (universal, ad-hoc signed), install and uninstall scripts.
- Result (Nov 9, 2026): tested on both test Macs. Logic Pro 10.7.8 on macOS 12.7.8 (Intel) and Logic Pro 11.2 on macOS
  Tahoe (Apple silicon): the plugin appears in the instrument list, the placeholder window opens, and MIDI notes play
  the test tone. Milestone 0 is complete.
