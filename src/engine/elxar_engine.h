/* ELXAR-Two: plain-C front door to the LXR-02 sound engine (the firmware code in third_party/lxr-firmware).
 *
 * The firmware keeps all of its state in global variables, so there is ONE engine per process.
 * Milestone 1 supports a single plugin instance making sound; see README "Known limits".
 * Nothing here is thread-safe: call everything from the audio thread (or before audio starts).
 *
 * GPL-3.0-only. The approach (which firmware files to compile, how a kit is sent to the engine) follows the
 * prior art LXRXXL by L'Cronx (github.com/sunskiefer/LXR02XXL); the code here is written for ELXAR-Two. */
#ifndef ELXAR_ENGINE_H
#define ELXAR_ENGINE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define ELXAR_BLOCK 64            /* the firmware renders 64 frames at a time */
#define ELXAR_NATIVE_RATE 44140.0 /* the hardware's real sample rate (REAL_FS in the firmware) */
#define ELXAR_NUM_VOICES 6        /* Drum 1, Drum 2, Drum 3, Snare, Cymbal, Hi-hat */
#define ELXAR_NUM_TRACKS 7        /* the six voices; track 6 is the open hi-hat (same voice as track 5) */
#define ELXAR_NUM_KIT_PARAMS 247  /* one kit = sound parameters 0..246 (Parameters.h) */
#define ELXAR_DEFAULT_NOTE 60     /* trigger note that plays a voice at its own pitch */

void elxar_init(void);                                 /* power-up + load the firmware's init kit */
void elxar_load_init_kit(void);
void elxar_set_param(uint16_t par, uint8_t value);     /* one kit parameter, as the hardware menu would send it */
uint8_t elxar_get_param(uint16_t par);
void elxar_trigger(uint8_t track, uint8_t velocity, uint8_t note);   /* track 0..6, velocity 1..127 */
void elxar_set_tempo(float bpm);                       /* for tempo-synced LFOs */
void elxar_set_fader(uint8_t voice, float gain01);     /* the hardware's six volume faders, 0..1 */
void elxar_silence_fx(void);                           /* clear delay / reverb tails */
/* Render ELXAR_BLOCK frames at ELXAR_NATIVE_RATE. Output is float, roughly -1..1 (OUT 1 + OUT 2 summed). */
void elxar_render_block(float* outL, float* outR);

#ifdef __cplusplus
}
#endif
#endif
