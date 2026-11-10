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
- Result: not yet tested in Logic.

## 0.0.1 (unreleased): Milestone 0
- Imported the Erica Synths LXR-02 firmware source (unmodified, commit a43cd4f) into `third_party/lxr-firmware`.
- Empty AU instrument and VST3 shell (JUCE 8.0.6), stereo output, placeholder tone on MIDI notes.
- GitHub Actions build (universal, ad-hoc signed), install and uninstall scripts.
- Result (Nov 9, 2026): tested on both test Macs. Logic Pro 10.7.8 on macOS 12.7.8 (Intel) and Logic Pro 11.2 on macOS
  Tahoe (Apple silicon): the plugin appears in the instrument list, the placeholder window opens, and MIDI notes play
  the test tone. Milestone 0 is complete.
