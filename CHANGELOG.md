# Changelog

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
