# Changes ELXAR-Two made to the LXR firmware source

Original: https://github.com/erica-synths/lxr-desktop-app at commit a43cd4f (GPL-3.0-only, see LICENSE and NOTICE.md).
This copy is **modified** and is not the official Erica Synths firmware. Every change is marked with the words
`ELXAR-Two change` in the file itself.

## Files added after Milestone 0 (unmodified copies from the same commit)
- `Core/Src/menu/Cc2Text.c` (list of modulation targets)
- `Core/Src/Preset/initKit.c` (the built-in starting kit)

## Files removed (hardware only; replaced by stand-ins in `src/engine/shim/`)
- `Core/Inc/main.h`, `Core/Inc/delay.h`

## Files compiled into the plugin
`Core/Src/Soundengine/DSPAudio/**` (except `random.c`), `Soundengine/FxBus.c`, `Soundengine/MIDI/ParameterArray.c`,
`Soundengine/MIDI/MidiParser.c`, `Parameters.c`, `menu/Cc2Text.c`, `Preset/initKit.c`. The sequencer, USB, UART,
audio-chip and trigger files are in the folder but are not compiled.

## Changes inside files
| File | Change | Why |
| --- | --- | --- |
| `Inc/Soundengine/config.h` | Memory-section macros are empty when `ELXAR_TWO_HOST` is defined | A computer has no STM32 memory regions |
| `Inc/Soundengine/FxBus.h` | `USE_REVERB` can be set from the build (the plugin sets 1) | The plate reverb exists in the firmware but ships switched off |
| `DSPAudio/Oscillator.c` | `calcNoise` compares phases instead of reading an ARM processor flag | The flag does not exist on a computer |
| `DSPAudio/Oscillator.c` | Wavetable interpolation wraps its second index (`& 0x3FF`), 8 places | It read one entry past the 1024-entry table (firmware review item D13) |
| `DSPAudio/Oscillator.c`, `DrumVoice.c`, `lfo.c`, `MIDI/MidiParser.c` | Decimal-to-whole-number conversions go through `ELXAR_F2U` (19 places) | The ARM chip clips out-of-range values; Intel processors wrap around. Without this, FM and pulse-width sounds would differ between Intel and Apple silicon |
| `DSPAudio/FX/Delay.c` | Ping-pong delay keeps its pointers inside its half of the buffer | Memory overrun after switching delay type (found by OpenElixir-02 and LXRXXL) |
| `DSPAudio/FX/Distortion.c` | Diode table read is limited to the last entry | Read past the table (fix from OpenElixir-02) |
| `DSPAudio/FX/Compressor.c` | Mono path clips instead of wrapping around | Fix from OpenElixir-02 |
| `DSPAudio/Snare.c` | Output clips instead of wrapping around (2 places) | Fix from OpenElixir-02 |
| `DSPAudio/Decay.c` | Pitch-envelope slope 127 no longer divides by zero | It silenced the oscillator (fix from OpenElixir-02) |
| `DSPAudio/modulationNode.c` | Skips a modulation destination that has no parameter | Reading address 0 crashes on a computer (found by LXRXXL) |
