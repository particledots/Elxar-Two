# ELXAR-Two (work in progress)

An unofficial Audio Unit (for Logic Pro) and VST3 instrument based on the open-source firmware of the Erica Synths
LXR-02 drum synthesizer. **Not made, approved or supported by Erica Synths.** Do not contact them about this plugin.
ELXAR-Two is a modified port and is marked as such.

## Status
Milestone 0: an empty instrument shell that builds on GitHub, loads in Logic, and plays a placeholder tone on MIDI notes.
The LXR-02 sound engine (in `third_party/lxr-firmware`, imported unmodified) is not connected yet.

## Planned
- 6 voices and the LXR-02 effects, stereo output.
- Step sequencer synced to Logic, with a MIDI drum-pattern generator.
- Original user interface (the LXR hardware design is not licensed).

## Get a build
`.github/workflows/build.yml` builds the AU and VST3 (universal, macOS 11+) on GitHub. Download `ELXAR-Two-macOS`
from the latest run's Artifacts (Actions tab), unzip, and double-click `Install-ELXAR-Two.command`.
`Uninstall-ELXAR-Two.command` removes it.

## Licence
GPL-3.0, with the additional terms of the LXR firmware (see `LICENSE`, `THIRD-PARTY-LICENSES.txt` and
`third_party/lxr-firmware/NOTICE.md`).
