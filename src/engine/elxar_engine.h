/* ELXAR-Two: plain-C front door to the LXR-02 sound engine (the firmware code in third_party/lxr-firmware).
 *
 * The firmware keeps all of its state in global variables, so there is ONE engine per process.
 * Only a single plugin instance makes sound; see README "Known limits".
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

/* ------------------------------------------------------------------------------------------------------------
 * The firmware's own step sequencer (sequencer.c). The firmware clocks it from a millisecond timer; the plugin
 * clocks it from the host's song position instead: one elxar_seq_pulse() per 1/24 of a step (a step is a
 * 16th note), the firmware's internal resolution. Everything else (which step plays, track lengths,
 * probability, flam, shift, per-step automation, pattern changes) is the firmware code.
 * ---------------------------------------------------------------------------------------------------------- */
#define ELXAR_NUM_PATTERNS 64     /* the firmware holds 64 patterns */
#define ELXAR_NUM_STEPS 64        /* up to 64 steps (4 bars of 16) per track */
#define ELXAR_PULSES_PER_STEP 24
#define ELXAR_SHIFT_NONE 7        /* shift values are 0..14 meaning -7..+7 */

typedef struct ElxarStep {
    uint8_t on;            /* 1 = the step plays */
    uint8_t velocity;      /* 0..127 */
    uint8_t probability;   /* 0..100 percent */
    uint8_t note;          /* 0..127; ELXAR_DEFAULT_NOTE plays the voice at its own pitch */
    uint8_t flam;          /* 0..5: off, x2, x3, x4, x6, x8 hits inside the step */
    uint8_t shift;         /* 0..14: moves the step earlier or later by up to 7/8 of a step; 7 = no shift */
    uint8_t autoTarget[2]; /* per-step automation: 0 = none, otherwise a position in the modulation target list */
    uint8_t autoValue[2];  /* 0..127 */
} ElxarStep;

void elxar_seq_write_track(uint8_t pattern, uint8_t track, const ElxarStep* steps /* ELXAR_NUM_STEPS */, uint8_t length /* 1..64 */);
void elxar_seq_write_shuffle(uint8_t pattern, uint8_t shuffle /* 0..127 */);
void elxar_seq_start(uint32_t absoluteStep);   /* run; the next pulse plays this step (counted from the song start) */
void elxar_seq_locate(uint32_t absoluteStep);  /* jump while running */
void elxar_seq_stop(void);
int  elxar_seq_running(void);
void elxar_seq_pulse(void);                    /* one clock pulse = 1/24 step */
void elxar_seq_select_pattern(uint8_t pattern);/* stopped: at once. running: when the longest track ends */
uint8_t elxar_seq_active_pattern(void);
int  elxar_seq_position(uint8_t track);        /* step 0..63 the track is on, or -1 */
float elxar_seq_shuffle(void);                 /* shuffle of the playing pattern, 0..1 */
uint32_t elxar_seq_trigger_count(uint8_t track); /* how many times the sequencer has played this track (for tests) */

/* The firmware's list of modulation targets (targets for the LFOs, velocity and per-step automation).
 * These only read fixed tables and may be called from any thread. */
int elxar_num_mod_targets(void);
int elxar_mod_target_param(int index);   /* the kit parameter number */
int elxar_mod_target_voice(int index);   /* 1..6, or 0 for "no voice" */

/* How the firmware places the hits of a step (fixed tables, any thread): which of the 24 pulses of a step play
 * for a flam setting (bit 23 = pulse 0), and by how many pulses a shift setting moves the step (-21..+21). */
uint32_t elxar_flam_bits(uint8_t flam);
int elxar_shift_pulses(uint8_t shift);
/* The order in which the firmware sends a kit's parameters to the engine (position 0..246 -> parameter number). */
int elxar_param_order(int position);
/* The value a parameter has in the firmware's built-in init kit. */
uint8_t elxar_init_kit_value(int par);

/* The firmware's Euclidean pattern generator: `hits` spread over `length` steps (1 to 16, the firmware's limit:
 * its calculation does not work for longer patterns), rotated. Bit 0 = step 1.
 * Uses its own scratch memory: call it from one thread at a time, but it does not touch the playing engine. */
uint64_t elxar_euclid(uint8_t length, uint8_t hits, uint8_t rotation);

#ifdef __cplusplus
}
#endif
#endif
