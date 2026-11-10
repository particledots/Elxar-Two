/* ELXAR-Two: glue between the plugin and the LXR-02 firmware sound engine.
 *
 * Everything that makes sound is the firmware's own code. This file only
 *   1. provides the few things the engine expects from the rest of the firmware (menu, sequencer, front panel,
 *      faders, MIDI ports, random number chip) as small stand-ins, and
 *   2. offers the simple functions declared in elxar_engine.h.
 *
 * GPL-3.0-only. The approach follows LXRXXL (github.com/sunskiefer/LXR02XXL, src/lxr/lxr_dsp.c). */
#include <string.h>
#include <math.h>
#include "elxar_engine.h"

#include "Soundengine/config.h"
#include "Soundengine/globals.h"
#include "Parameters.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/MIDI/ParameterArray.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/random.h"
#include "Soundengine/DSPAudio/FX/FxRamBuffer.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/clockSync.h"
#include "Soundengine/USB/usb_manager.h"
#include "Preset/InitKit.h"
#include "menu/menu.h"
#include "menu/CcNr2Text.h"
#include "IO/adcPots.h"
#include "frontPanelParser_ui.h"
#include "main.h"

_Static_assert(ELXAR_BLOCK == OUTPUT_DMA_SIZE, "one render call is one firmware block");
_Static_assert(ELXAR_NUM_KIT_PARAMS == END_OF_SOUND_PARAMETERS, "a kit is the firmware's sound parameters");
_Static_assert(ELXAR_NUM_TRACKS == NUM_TRACKS, "track count");
_Static_assert(ELXAR_DEFAULT_NOTE == SEQ_DEFAULT_NOTE, "default note");

/* ------------------------------------------------------------------------------------------------------------
 * Stand-ins for the parts of the firmware that are not in the plugin
 * ---------------------------------------------------------------------------------------------------------- */
uint8_t activeParameters[NUM_PARAMS];        /* the menu's edit buffer: the kit as last sent */
volatile uint32_t systick_ticks;
volatile uint8_t bCurrentSampleValid;
ElxarGpio elxar_jacks = { 1u };              /* every output jack "plugged in" (see shim/main.h) */
uint8_t menu_activePage = 0;
uint8_t frontParser_activeTrack = 0;
RNG_HandleTypeDef hrng;
PatternSet seq_patternSet;                   /* only touched by the firmware's MIDI note recording, unused here */

static float g_fader[6] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
static float g_bpm = 120.f;
static uint8_t g_mutes;
static float g_dcXL, g_dcYL, g_dcXR, g_dcYR;

/* faders */
float adc_getFaderVolume(uint8_t idx) { return idx < 6 ? g_fader[idx] : 1.f; }

/* random numbers: the hardware has a random-number chip; here a small xorshift generator */
static uint32_t g_rng = 0x9E3779B9u;
void initRng(void) { if (!g_rng) g_rng = 0x9E3779B9u; }
uint32_t GetRngValue(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* sequencer (the firmware sequencer is not connected in Milestone 1) */
uint16_t seq_getBpm(void) { return (uint16_t)(g_bpm + 0.5f); }
uint8_t seq_getExtSync(void) { return 0; }
uint8_t seq_isRunning(void) { return 0; }
uint8_t seq_isTrackMuted(uint8_t trackNr) { return trackNr < 8 ? (uint8_t)((g_mutes >> trackNr) & 1u) : 0; }
void seq_setMute(uint8_t trackNr, uint8_t isMuted)
{
    if (trackNr > 7) return;
    if (isMuted) g_mutes |= (uint8_t)(1u << trackNr); else g_mutes &= (uint8_t)~(1u << trackNr);
}
void seq_addNote(uint8_t trackNr, uint8_t vel, uint8_t note) { (void)trackNr; (void)vel; (void)note; }
void seq_midiNoteOff(uint8_t chan) { (void)chan; }
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc) { (void)channel; (void)note; (void)veloc; }
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value) { (void)voice; (void)dest; (void)value; }
void seq_setNextPattern(const uint8_t patNr) { (void)patNr; }
void seq_setRunning(uint8_t isRunning) { (void)isRunning; }
void seq_sync(void) {}
void sync_midiResetPosition(void) {}
void sync_midiStartStop(uint8_t isStart) { (void)isStart; }

/* menu / front panel / MIDI ports */
uint8_t menu_getActiveVoice(void) { return 0; }
uint8_t menu_getSubPage(void) { return 0; }
uint8_t menu_getViewedPattern(void) { return 0; }
void menu_repaint(void) {}
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle) { (void)subPageNr; (void)toggle; }
void uart_sendFrontpanelByte(uint8_t data) { (void)data; }
void uart_sendMidi(MidiMsg msg) { (void)msg; }
void usb_sendMidi(MidiMsg msg) { (void)msg; }

/* MidiVoiceControl.c: voiceControl_noteOn without the MIDI-out and LED parts */
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel)
{
    if (voice < 3) Drum_trigger(voice, vel, note);
    else if (voice < 4) Snare_trigger(vel, note);
    else if (voice < 5) Cymbal_trigger(vel, note);
    else if (voice < 7) HiHat_trigger(vel, (uint8_t)(voice - 5), note);
}

/* ------------------------------------------------------------------------------------------------------------
 * The plugin-facing functions
 * ---------------------------------------------------------------------------------------------------------- */

/* menu -> engine, as the firmware does it: "CC n" sets parameter n - 1, "CC2 n" sets parameter 128 + n */
static void send_cc(uint16_t par, uint8_t value)
{
    MidiMsg msg;
    memset(&msg, 0, sizeof msg);
    if (par < 128u) {
        msg.status = MIDI_CC;
        msg.data1 = (uint8_t)((par + 1u) & 0x7fu);
    } else {
        msg.status = MIDI_CC2;
        msg.data1 = (uint8_t)(par - 128u);
    }
    msg.data2 = value;
    midiParser_ccHandler(msg, 1, 0, 0.f);
}

static void set_lfo_target(uint8_t lfo, uint16_t target)
{
    switch (lfo) {   /* FRONT_CC_LFO_TARGET in the firmware's CortexParser.c */
    case 0: case 1: case 2: modNode_setDestination(&voiceArray[lfo].lfo.modTarget, target); break;
    case 3: modNode_setDestination(&snareVoice.lfo.modTarget, target); break;
    case 4: modNode_setDestination(&cymbalVoice.lfo.modTarget, target); break;
    case 5: modNode_setDestination(&hatVoice.lfo.modTarget, target); break;
    default: break;
    }
}

/* Highest value the hardware menu allows for the parameters where a larger value would make the engine read
 * outside its tables. Everything else is 0..127. */
static uint8_t max_value(uint16_t par)
{
    if (par == PAR_FX_TYPE) return FX_REVERB;
    if (par == PAR_FX_ROUTING) return MIXER_ROUTING_DAC2_R;
    if (par == PAR_FX_DRY_WET) return 100;
    if (par == PAR_FX_DIST_TYPE) return 2;
    if (par == PAR_FX_DELAY_TYPE) return 1;
    if (par == PAR_FX_REVERB_TYPE) return 0;
    if (par == PAR_FX_RATIO) return 5;
    if (par >= PAR_AUDIO_OUT1 && par <= PAR_AUDIO_OUT6) return MIXER_ROUTING_FX_BUS;
    if (par >= PAR_FILTER_TYPE_1 && par <= PAR_FILTER_TYPE_6) return 7;
    if ((par >= PAR_OSC_WAVE_DRUM1 && par <= PAR_OSC_WAVE_SNARE) || par == PAR_WAVE1_CYM || par == PAR_WAVE1_HH ||
        (par >= PAR_MOD_WAVE_DRUM1 && par <= PAR_WAVE3_HH) || par == PAR_FX_WAVE)
        return 5;   /* the six built-in waveforms; higher numbers would select user samples (not in the plugin) */
    return 127;
}

void elxar_set_param(uint16_t par, uint8_t value)
{
    const uint8_t mx = max_value(par);
    if (par >= END_OF_SOUND_PARAMETERS) return;
    if (value > mx) value = mx;
    activeParameters[par] = value;
    if (par == PAR_NONE || par == PAR_BANK_CHANGE || par == PAR_MORPH_KIT_DEST) return;   /* no sound */
    if (par == PAR_NRPN_DATA_ENTRY_COARSE || par == PAR_NRPN_FINE || par == PAR_NRPN_COARSE) return;
    /* A kit stores modulation targets as positions in the firmware's target list; the engine wants the
     * parameter number (preset_sendDrumsetParameters in the firmware). */
    if (par >= PAR_VEL_DEST_1 && par <= PAR_VEL_DEST_6) {
        if (value >= getNumModTargets()) value = 0;
        modNode_setDestination(&velocityModulators[par - PAR_VEL_DEST_1], modTargets[value].param);
        return;
    }
    if (par >= PAR_TARGET_LFO1 && par <= PAR_TARGET_LFO6) {
        if (value >= getNumModTargets()) value = 0;
        set_lfo_target((uint8_t)(par - PAR_TARGET_LFO1), modTargets[value].param);
        return;
    }
    if (par >= PAR_VOICE_LFO1 && par <= PAR_VOICE_LFO6) return;   /* menu-only helper value */
    send_cc(par, value);
}

uint8_t elxar_get_param(uint16_t par) { return par < END_OF_SOUND_PARAMETERS ? activeParameters[par] : 0; }

static void send_kit(const uint8_t* kit)
{
    uint16_t i;
    for (i = 0; i < 6; i++) {
        elxar_set_param((uint16_t)(PAR_VEL_DEST_1 + i), kit[PAR_VEL_DEST_1 + i]);
        elxar_set_param((uint16_t)(PAR_TARGET_LFO1 + i), kit[PAR_TARGET_LFO1 + i]);
    }
    for (i = 0; i < END_OF_SOUND_PARAMETERS; i++) {   /* the firmware's own sending order */
        const uint16_t p = parameterOrder[i];
        if (p >= END_OF_SOUND_PARAMETERS) continue;
        if ((p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6) || (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6)) continue;
        elxar_set_param(p, kit[p]);
    }
}

void elxar_load_init_kit(void)
{
    uint8_t kit[END_OF_SOUND_PARAMETERS];
    uint16_t i;
    memset(kit, 0, sizeof kit);
    /* initKit = 8 name bytes, then the parameters (preset_init in the firmware) */
    for (i = 8; i < INIT_KIT_SIZE && (i - 8) < END_OF_SOUND_PARAMETERS; i++) kit[i - 8] = initKit[i];
    send_kit(kit);
}

void elxar_init(void)
{
    uint8_t i;
    /* soundengine_init() of the firmware, without the audio chip and the sequencer */
    initRng();
    memset(activeParameters, 0, sizeof activeParameters);
    memset(FX_BUFFER, 0, sizeof FX_BUFFER);
    mixer_init();
    parameterArray_init();
    for (i = 0; i < 6; i++) modNode_init(&velocityModulators[i]);
    memset(midi_MidiChannels, 0, 8);
    initDrumVoice();
    Snare_init();
    HiHat_init();
    Cymbal_init();
    paramToModTargetInit();
    g_mutes = 0;
    g_dcXL = g_dcYL = g_dcXR = g_dcYR = 0.f;
    for (i = 0; i < 6; i++) g_fader[i] = 1.f;
    elxar_load_init_kit();
}

void elxar_trigger(uint8_t track, uint8_t velocity, uint8_t note)
{
    if (track >= NUM_TRACKS || velocity == 0) return;
    if (velocity > 127) velocity = 127;
    if (note > 127) note = 127;
    voiceControl_noteOn(track, note, velocity);
}

void elxar_set_tempo(float bpm)
{
    if (!(bpm >= 1.f)) bpm = 1.f;
    if (bpm > 999.f) bpm = 999.f;
    if (bpm != g_bpm) {
        g_bpm = bpm;
        lfo_recalcSync();
    }
}

void elxar_set_fader(uint8_t voice, float gain01)
{
    if (voice < 6) g_fader[voice] = gain01 < 0.f ? 0.f : (gain01 > 1.f ? 1.f : gain01);
}

void elxar_silence_fx(void)
{
    uint16_t p;
    memset(FX_BUFFER, 0, sizeof FX_BUFFER);
    FxBus_init();
    for (p = PAR_FX_TYPE; p <= PAR_FX_DELAY_TYPE; p++) send_cc(p, activeParameters[p]);
}

void elxar_render_block(float* outL, float* outR)
{
    int16_t out1[ELXAR_BLOCK * 2], out2[ELXAR_BLOCK * 2];
    int i;
    systick_ticks++;
    mixer_calcNextSampleBlock(out1, out2);
    /* The hardware outputs block DC with a capacitor; a gentle DC blocker (about 7 Hz) does the same here. */
    for (i = 0; i < ELXAR_BLOCK; i++) {
        const float l = ((float)out1[2 * i] + (float)out2[2 * i]) * (1.f / 32768.f);
        const float r = ((float)out1[2 * i + 1] + (float)out2[2 * i + 1]) * (1.f / 32768.f);
        g_dcYL = l - g_dcXL + 0.999f * g_dcYL; g_dcXL = l;
        g_dcYR = r - g_dcXR + 0.999f * g_dcYR; g_dcXR = r;
        outL[i] = g_dcYL;
        outR[i] = g_dcYR;
    }
}
