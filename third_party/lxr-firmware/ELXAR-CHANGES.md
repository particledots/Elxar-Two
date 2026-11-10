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
`Soundengine/MIDI/MidiParser.c`, `Soundengine/MIDI/MidiVoiceControl.c`, `Soundengine/Sequencer/sequencer.c`,
`Soundengine/Sequencer/EuklidGenerator.c`, `Parameters.c`, `menu/Cc2Text.c`, `Preset/initKit.c`.
The sequencer and `MidiVoiceControl.c` are compiled since Milestone 2. The clock-sync, USB, UART, audio-chip and
trigger files are in the folder but are not compiled.

## How the sequencer is clocked (Milestone 2)
The firmware's sequencer (`sequencer.c`) is compiled whole. On the hardware a millisecond timer calls `seq_tick()`,
which makes 24 pulses per step. The plugin does not use `seq_tick()` or `clockSync.c`: it makes the same 24 pulses per
step from the host's song position (`elxar_seq_pulse()` in `src/engine/elxar_engine.c`, which calls the firmware's
`seq_nextStep()` and `seq_processFlam()` exactly as `seq_tick()` does, and `SeqClock` in `src/engine/ElxarEngine.h`,
which also reproduces the firmware's shuffle timing). The front-panel lights, the SD card, the trigger jacks and the
MIDI ports that the sequencer talks to are empty stand-ins in `src/engine/elxar_engine.c`.

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
| `DSPAudio/FX/Reverb.c` | With a mono output routing the reverb writes one channel only | It always wrote a stereo pair and ran one sample past the output buffer (found by the ELXAR-Two engine test on Apple silicon) |
| `Sequencer/sequencer.c` | `seq_setPendingPatternActive`: the "copy the backup over the pattern" step is skipped | The hardware throws away unsaved pattern edits when the pattern changes. The plugin has no "save pattern" step, so edits must stay |
| `Sequencer/sequencer.c` | `seq_triggerVoice`: uses the step that is really playing (new variable `seq_elxarPlayStep`, set in `seq_processFlam`) for per-step automation and velocity, and never reads step -1 | A shifted or flammed step used the automation of the step the track counter was on (firmware review S-B3). While stopped the counter is -1, which is outside the pattern (review S-A2) |
| `Sequencer/sequencer.c` | `seq_processFlam`: one pair of "choke" flags per track instead of one pair for all tracks | An early (shifted) hit on one track dropped the hits of the tracks after it (firmware review S-B4) |
| `Sequencer/sequencer.c` | `seq_processFlam`: a stored track length of 0 counts as 16, lengths above 64 are cut, and a track that has not started is skipped | `% 0` stops a program on a computer (firmware review S-B1, issue 7 item 1) |
| `Sequencer/sequencer.c`, `sequencer.h` | Added `seq_elxarLocate()` | Puts every track on the step that belongs to the host's song position (each track wraps at its own length) |
| `Sequencer/sequencer.c`, `sequencer.h` | Added `seq_elxarResetAutomation()` | Ends all per-step automation when the sequencer stops, so a sound played by hand afterwards has its normal settings |
| `Sequencer/EuklidGenerator.c` | `euklid_transferPattern`: length 16 is stored as 16, not as 0 | Most of the sequencer does not treat 0 as 16 (firmware review S-B1). (The plugin does not call this function; it is fixed for completeness) |
| `Sequencer/EuklidGenerator.c`, `EuklidGenerator.h` | Added `euklid_elxarCompute()`; `euklid_calcRecursive` is declared in the header | The same calculation as `euklid_generate()`, but the result is handed back instead of being written into a pattern. Limited to length 16 as in the firmware's menu: for lengths from 18 the firmware's calculation never ends or gives the wrong number of hits (found by the ELXAR-Two engine test) |

## Firmware behaviour that was left as it is
- Probability: the firmware's random numbers start at 0 after power-up, so in the first one or two passes a step can
  play even if its probability is 0.
- Tempo-synced LFOs use the tempo rounded to a whole BPM (`seq_setBpm` takes a whole number).

