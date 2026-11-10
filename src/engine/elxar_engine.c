/* ELXAR-Two: glue between the plugin and the LXR-02 firmware sound engine.
 *
 * Everything that makes sound is the firmware's own code. This file only
 *   1. provides the few things the engine and the sequencer expect from the rest of the firmware (menu,
 *      front panel lights, SD card, trigger jacks, faders, MIDI ports, random number chip) as small stand-ins, and
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
#include "Soundengine/Sequencer/EuklidGenerator.h"
#include "Soundengine/TriggerOut.h"
#include "Soundengine/MIDI/CortexParser.h"
#include "Soundengine/DSPAudio/automationNode.h"
#include "Preset/PresetManager.h"
#include "ledHandler.h"
#include "buttonHandler.h"
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

static float g_fader[6] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
static float g_bpm = 120.f;
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

/* ---- what the firmware's sequencer (sequencer.c, compiled whole since Milestone 2) expects from the front
 * panel, the SD card, the trigger jacks and the MIDI ports. None of these exist in the plugin. ---- */
uint8_t frontParser_shownPattern = 0;
uint8_t menu_playedPattern = 0, menu_shownPattern = 0, menu_activeBar = 0, menu_songMenuPosition = 0;
uint8_t menu_selectedPresetNr[NUM_PRESET_LOCATIONS];
uint8_t menu_loadedPresetNr[NUM_CURRENT];
uint8_t midi_process_block = 0;
uint8_t trigger_clockReceived = 0;
uint32_t sync_delayedStepTimer = 0;

void sync_tick(void) {}
void sync_midiResetPosition(void) {}
void sync_midiStartStop(uint8_t isStart) { (void)isStart; }
void trigger_clockTick(uint8_t onOff, uint8_t cnt) { (void)onOff; (void)cnt; }
void trigger_reset(uint8_t value) { (void)value; }
void preset_touchTimer(PresetTimer type) { (void)type; }
void preset_loadDrumsetRam(uint8_t presetNr, uint8_t isMorph, uint8_t sendImmediate) { (void)presetNr; (void)isMorph; (void)sendImmediate; }
uint8_t buttonHandler_getMode(void) { return 0; }
void led_blinkHandler(void) {}
void led_clearActive_step(void) {}
void led_clearAllBlinkLeds(void) {}
void led_clearSequencerLeds(void) {}
uint8_t led_idx2StepLed(uint8_t idx) { return idx; }
void led_initPatternSelectLeds(void) {}
void led_resetBlinkTimer(void) {}
void led_setActive_step(uint8_t stepNr) { (void)stepNr; }
void led_setBlinkLed(const uint8_t ledNr, const uint8_t onOff) { (void)ledNr; (void)onOff; }
void led_setValue(uint8_t val, uint8_t ledNr) { (void)val; (void)ledNr; }
void led_updateSeqLeds(void) {}
void menu_repaintAll(void) {}
void menu_setShownPattern(uint8_t patternNr) { menu_shownPattern = patternNr; }
void uart_sendFrontpanelSysExByte(uint8_t data) { (void)data; }

/* menu / front panel / MIDI ports */
uint8_t menu_getActiveVoice(void) { return 0; }
uint8_t menu_getSubPage(void) { return 0; }
uint8_t menu_getViewedPattern(void) { return menu_shownPattern; }
void menu_repaint(void) {}
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle) { (void)subPageNr; (void)toggle; }
void uart_sendMidi(MidiMsg msg) { (void)msg; }
void usb_sendMidi(MidiMsg msg) { (void)msg; }

/* The firmware tells the front panel about every note (to flash a light): "note on" byte, then the track.
 * The plugin uses that to count what the sequencer plays (for the headless test). */
static uint32_t g_seqTriggers[NUM_TRACKS];
static uint8_t g_panelByteState;
void uart_sendFrontpanelByte(uint8_t data)
{
    if (g_panelByteState == 0) { if (data == NOTE_ON) g_panelByteState = 1; }
    else if (g_panelByteState == 1) { if (data < NUM_TRACKS) g_seqTriggers[data]++; g_panelByteState = 2; }
    else g_panelByteState = 0;
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
    /* the sequencer part of soundengine_init() */
    seq_init();
    euklid_init();
    seq_setExtSync(0);
    seq_setSongModeEnabled(0);
    seq_setRecordingMode(0);
    seq_setErasingMode(0);
    seq_setRunning(0);
    seq_setNextPattern(0);
    seq_copyPatternToBackupBuffer(0);
    memset(g_seqTriggers, 0, sizeof g_seqTriggers);
    g_panelByteState = 0;
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
        seq_setBpm((uint16_t)(bpm + 0.5f));   /* the firmware keeps whole BPM; used for tempo-synced LFOs */
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
    /* a little spare room after each buffer: the firmware was written for buffers that sit next to each other in memory */
    int16_t out1[ELXAR_BLOCK * 2 + 16], out2[ELXAR_BLOCK * 2 + 16];
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

/* ------------------------------------------------------------------------------------------------------------
 * The firmware sequencer, clocked by the plugin
 * ---------------------------------------------------------------------------------------------------------- */

/* A step's automation target is stored the way the firmware's front panel code stores it (CortexParser.c,
 * FRONT_SET_P1_DEST): parameters 1..127 as number + 1, parameters from 128 as the number itself, and
 * NO_AUTOMATION for none. */
static uint8_t automation_code(uint8_t targetIndex)
{
    uint16_t par;
    if (targetIndex == 0 || targetIndex >= getNumModTargets()) return NO_AUTOMATION;
    par = modTargets[targetIndex].param;
    if (par == PAR_NONE || par >= END_OF_SOUND_PARAMETERS) return NO_AUTOMATION;
    return (uint8_t)(par < 128u ? par + 1u : par);
}

void elxar_seq_write_track(uint8_t pattern, uint8_t track, const ElxarStep* steps, uint8_t length)
{
    uint8_t i, bar;
    uint16_t mainSteps[NUM_BARS];
    if (pattern >= NUM_PATTERN || track >= NUM_TRACKS || steps == 0) return;
    if (length < 1) length = 1;
    if (length > NUM_STEPS) length = NUM_STEPS;
    memset(mainSteps, 0, sizeof mainSteps);
    for (i = 0; i < NUM_STEPS; i++) {
        Step* d = &seq_patternSet.seq_subStepPattern[pattern][track][i];
        const ElxarStep* s = &steps[i];
        d->volume = (uint8_t)((s->velocity > 127 ? 127 : s->velocity) | (s->on ? STEP_ACTIVE_MASK : 0));
        d->prob = (uint8_t)(s->probability > 100 ? 100 : s->probability);
        d->note = (uint8_t)(s->note > 127 ? 127 : s->note);
        d->param1Nr = automation_code(s->autoTarget[0]);
        d->param1Val = (uint8_t)(s->autoValue[0] > 127 ? 127 : s->autoValue[0]);
        d->param2Nr = automation_code(s->autoTarget[1]);
        d->param2Val = (uint8_t)(s->autoValue[1] > 127 ? 127 : s->autoValue[1]);
        seq_patternSet.seq_stepTiming[pattern][track][i].flam = (uint8_t)(s->flam > 5 ? 0 : s->flam);
        seq_patternSet.seq_stepTiming[pattern][track][i].shift = (uint8_t)(s->shift > 14 ? ELXAR_SHIFT_NONE : s->shift);
        if (s->on) mainSteps[i / 16] |= (uint16_t)(1u << (i % 16));
    }
    for (bar = 0; bar < NUM_BARS; bar++) seq_patternSet.seq_mainSteps[bar][pattern][track] = mainSteps[bar];
    seq_patternSet.length[pattern][track] = length;
}

void elxar_seq_write_shuffle(uint8_t pattern, uint8_t shuffle)
{
    if (pattern >= NUM_PATTERN) return;
    if (shuffle > 127) shuffle = 127;
    seq_patternSet.shuffle[pattern] = shuffle;
    if (pattern == seq_activePattern) {   /* as the firmware does when a pattern becomes active */
        activeParameters[PAR_SHUFFLE] = shuffle;
        seq_setShuffle(shuffle / 127.f);
    }
}

void elxar_seq_start(uint32_t absoluteStep)
{
    seq_setRunning(1);
    seq_elxarLocate(absoluteStep);
}

void elxar_seq_locate(uint32_t absoluteStep)
{
    voiceControl_noteOff(0xFF);
    seq_elxarLocate(absoluteStep);
}

void elxar_seq_stop(void)
{
    if (!seq_isRunning()) return;
    seq_setRunning(0);
    voiceControl_noteOff(0xFF);
    seq_elxarResetAutomation();   /* give automated parameters their normal values back */
}

int elxar_seq_running(void) { return seq_isRunning(); }

/* The firmware's seq_tick() without its millisecond timer: step on pulse 0, then the flam / shift handling
 * for this pulse (see seq_tick in sequencer.c, the part for the internal clock). */
void elxar_seq_pulse(void)
{
    if (!seq_isRunning()) return;
    if (seq_prescaleCounter >= ELXAR_PULSES_PER_STEP) seq_prescaleCounter = 0;
    if (seq_prescaleCounter == 0) seq_nextStep();
    if (!seq_isRunning()) return;
    seq_processFlam(seq_prescaleCounter);
    seq_prescaleCounter++;
    if (seq_prescaleCounter >= ELXAR_PULSES_PER_STEP) seq_prescaleCounter = 0;
}

void elxar_seq_select_pattern(uint8_t pattern) { if (pattern < NUM_PATTERN) seq_setNextPattern(pattern); }
uint8_t elxar_seq_active_pattern(void) { return seq_activePattern; }
int elxar_seq_position(uint8_t track)
{
    if (track >= NUM_TRACKS) return -1;
    return (int)(int8_t)seq_getStepIndex(track);
}
float elxar_seq_shuffle(void) { extern float seq_shuffle; return seq_shuffle; }
uint32_t elxar_seq_trigger_count(uint8_t track) { return track < NUM_TRACKS ? g_seqTriggers[track] : 0; }

int elxar_num_mod_targets(void) { return getNumModTargets(); }
int elxar_mod_target_param(int index) { return (index >= 0 && index < getNumModTargets()) ? (int)modTargets[index].param : 0; }
int elxar_mod_target_voice(int index) { return (index >= 0 && index < getNumModTargets()) ? (int)voiceFromModTargValue((uint8_t)index) : 0; }

uint64_t elxar_euclid(uint8_t length, uint8_t hits, uint8_t rotation) { return euklid_elxarCompute(length, hits, rotation); }

uint32_t flamIdxToPattern(uint8_t flamIdx);   /* sequencer.c */
int8_t getShiftValue(uint8_t in);             /* sequencer.c */
uint32_t elxar_flam_bits(uint8_t flam) { return flamIdxToPattern(flam) & 0xFFFFFFu; }
int elxar_shift_pulses(uint8_t shift) { return shift > 14 ? 0 : 3 * (int)getShiftValue(shift); }
int elxar_param_order(int position) { return (position >= 0 && position < END_OF_SOUND_PARAMETERS) ? (int)parameterOrder[position] : 0; }
uint8_t elxar_init_kit_value(int par) { return (par >= 0 && par + 8 < INIT_KIT_SIZE && par < END_OF_SOUND_PARAMETERS) ? initKit[par + 8] : 0; }
