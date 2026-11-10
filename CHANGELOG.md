# Changelog

## 0.0.1 (unreleased): Milestone 0
- Imported the Erica Synths LXR-02 firmware source (unmodified, commit a43cd4f) into `third_party/lxr-firmware`.
- Empty AU instrument and VST3 shell (JUCE 8.0.6), stereo output, placeholder tone on MIDI notes.
- GitHub Actions build (universal, ad-hoc signed), install and uninstall scripts.
- Result (Nov 9, 2026): tested on both test Macs. Logic Pro 10.7.8 on macOS 12.7.8 (Intel) and Logic Pro 11.2 on macOS
  Tahoe (Apple silicon): the plugin appears in the instrument list, the placeholder window opens, and MIDI notes play
  the test tone. Milestone 0 is complete.
