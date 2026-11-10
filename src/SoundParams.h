// ELXAR-Two: the list of sound parameters each voice has, with plain names, for the "Sound" page.
//
// The grouping and the names follow the hardware's own menu pages (Core/Inc/menu/menuPages.h and MenuText.h in the
// firmware): Oscillator, Amp envelope, Pitch modulation, FM / Mod oscillators, Transient, Filter, LFO, Mix.
// The value lists (waveforms, filter types and so on) are the firmware's lists, written out in full words.
// No JUCE in this file.
#pragma once
#include <vector>

extern "C" {
#include "Parameters.h"
}

namespace elxar
{
enum class ParamKind
{
    value,           // 0..127
    choice,          // one of a list of names (the value is the position in the list)
    onOff,           // 0 or 1
    velocityTarget,  // a modulation target on the same voice
    lfoTarget        // a modulation target on any voice
};

struct ParamInfo
{
    int par;                      // the firmware's parameter number (Parameters.h)
    const char* group;
    const char* name;
    ParamKind kind;
    const char* const* choices;   // for ParamKind::choice
    int numChoices;
};

namespace lists
{
inline const char* const waveforms[]  = { "Sine", "Triangle", "Saw", "Rectangle", "Noise", "Pulse (PWM)" };
inline const char* const filterTypes[] = { "Low-pass", "High-pass", "Band-pass", "Band-pass (unity)", "Notch", "Peak", "Low-pass 2", "Off" };
inline const char* const fmModes[]    = { "FM", "Mix" };
inline const char* const transients[] = { "Snap", "Offset", "Click", "Click 2", "Tick", "Kick", "Rim", "Drip", "Hat", "Clap", "Kick 2", "Snare", "Tom", "Snap 2" };
inline const char* const lfoWaves[]   = { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random", "Exp up", "Exp down" };
inline const char* const lfoSync[]    = { "Off (free)", "4 bars", "2 bars", "1 bar", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/32" };
inline const char* const lfoRetrigger[] = { "Off", "Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hi-hat" };
inline const char* const distTypes[]  = { "Tube", "Fold", "Clip" };
inline const char* const compRatios[] = { "1:1", "2:1", "3:1", "4:1", "6:1", "8:1" };
inline const char* const delayTypes[] = { "Delay", "Ping-pong" };
}

inline const char* voiceName (int voice)   // the engine's voice order
{
    static const char* const names[6] = { "Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hi-hat" };
    return voice >= 0 && voice < 6 ? names[voice] : "";
}

inline int panParam (int voice) { return voice < 3 ? PAR_PAN1 + voice : PAR_PAN4 + (voice - 3); }

// All sound parameters of one voice (engine voice 0..5: Drum 1, Drum 2, Drum 3, Snare, Cymbal, Hi-hat).
inline std::vector<ParamInfo> voiceParams (int v)
{
    using K = ParamKind;
    std::vector<ParamInfo> p;
    auto value  = [&p] (int par, const char* g, const char* n) { p.push_back ({ par, g, n, K::value, nullptr, 0 }); };
    auto choice = [&p] (int par, const char* g, const char* n, const char* const* list, int count) { p.push_back ({ par, g, n, K::choice, list, count }); };
#define ELXAR_LIST(x) lists::x, (int) (sizeof (lists::x) / sizeof (lists::x[0]))
    if (v < 0 || v > 5) return p;
    const bool drum = v < 3, snare = v == 3, cymbal = v == 4, hat = v == 5;

    // Oscillator
    value (PAR_COARSE1 + 2 * v, "Oscillator", "Coarse tune");
    value (PAR_FINE1 + 2 * v, "Oscillator", "Fine tune");
    const int oscWave = drum ? PAR_OSC_WAVE_DRUM1 + v : (snare ? (int) PAR_OSC_WAVE_SNARE : (cymbal ? (int) PAR_WAVE1_CYM : (int) PAR_WAVE1_HH));
    choice (oscWave, "Oscillator", "Waveform", ELXAR_LIST (waveforms));
    value (PAR_PWM_1 + v, "Oscillator", "Pulse width");
    if (snare)
    {
        value (PAR_NOISE_FREQ1, "Oscillator", "Noise frequency");
        value (PAR_MIX1, "Oscillator", "Noise / tone mix");
    }

    // Amp envelope
    value (PAR_VELOA1 + 2 * v, "Amp envelope", "Attack");
    if (hat)
    {
        value (PAR_VELOD6_CLOSED, "Amp envelope", "Decay (closed)");
        value (PAR_VELOD6_OPEN, "Amp envelope", "Decay (open)");
    }
    else
    {
        value (PAR_VELOD1 + 2 * v, "Amp envelope", "Decay");
    }
    value (PAR_VOL_SLOPE1 + v, "Amp envelope", "Slope");
    if (snare)  value (PAR_REPEAT4, "Amp envelope", "Repeat (clap)");
    if (cymbal) value (PAR_REPEAT5, "Amp envelope", "Repeat");

    // Pitch modulation (drums and snare)
    if (drum || snare)
    {
        value (PAR_MOD_EG1 + v, "Pitch envelope", "Decay");
        value (PAR_PITCH_SLOPE1 + v, "Pitch envelope", "Slope");
        value (PAR_MODAMNT1 + v, "Pitch envelope", "Amount");
    }

    // FM (drums) or the two modulation oscillators (cymbal, hi-hat)
    if (drum)
    {
        value (PAR_FMAMNT1 + 2 * v, "FM", "Amount");
        value (PAR_FM_FREQ1 + 2 * v, "FM", "Frequency");
        choice (PAR_MOD_WAVE_DRUM1 + v, "FM", "Waveform", ELXAR_LIST (waveforms));
        choice (PAR_MIX_MOD_1 + v, "FM", "Mode", ELXAR_LIST (fmModes));
    }
    if (cymbal)
    {
        value (PAR_MOD_OSC_F1_CYM, "Mod oscillators", "Frequency 1");
        value (PAR_MOD_OSC_GAIN1_CYM, "Mod oscillators", "Gain 1");
        choice (PAR_WAVE2_CYM, "Mod oscillators", "Waveform 1", ELXAR_LIST (waveforms));
        value (PAR_MOD_OSC_F2_CYM, "Mod oscillators", "Frequency 2");
        value (PAR_MOD_OSC_GAIN2_CYM, "Mod oscillators", "Gain 2");
        choice (PAR_WAVE3_CYM, "Mod oscillators", "Waveform 2", ELXAR_LIST (waveforms));
    }
    if (hat)
    {
        value (PAR_MOD_OSC_F1, "Mod oscillators", "Frequency 1");
        value (PAR_MOD_OSC_GAIN1, "Mod oscillators", "Gain 1");
        choice (PAR_WAVE2_HH, "Mod oscillators", "Waveform 1", ELXAR_LIST (waveforms));
        value (PAR_MOD_OSC_F2, "Mod oscillators", "Frequency 2");
        value (PAR_MOD_OSC_GAIN2, "Mod oscillators", "Gain 2");
        choice (PAR_WAVE3_HH, "Mod oscillators", "Waveform 2", ELXAR_LIST (waveforms));
    }

    // Transient (the short click at the start of the sound)
    choice (PAR_TRANS1_WAVE + v, "Transient", "Sound", ELXAR_LIST (transients));
    value (PAR_TRANS1_VOL + v, "Transient", "Volume");
    value (PAR_TRANS1_FREQ + v, "Transient", "Frequency");

    // Filter
    value (PAR_FILTER_FREQ_1 + v, "Filter", "Frequency");
    value (PAR_RESO_1 + v, "Filter", "Resonance");
    choice (PAR_FILTER_TYPE_1 + v, "Filter", "Type", ELXAR_LIST (filterTypes));
    value (PAR_FILTER_DRIVE_1 + v, "Filter", "Drive");

    // Velocity
    p.push_back ({ PAR_VEL_DEST_1 + v, "Velocity", "Target", K::velocityTarget, nullptr, 0 });
    value (PAR_VELO_MOD_AMT_1 + v, "Velocity", "Amount");
    p.push_back ({ PAR_VOLUME_MOD_ON_OFF1 + v, "Velocity", "Velocity to volume", K::onOff, nullptr, 0 });

    // LFO
    value (PAR_FREQ_LFO1 + v, "LFO", "Frequency");
    choice (PAR_SYNC_LFO1 + v, "LFO", "Tempo sync", ELXAR_LIST (lfoSync));
    value (PAR_AMOUNT_LFO1 + v, "LFO", "Amount");
    choice (PAR_WAVE_LFO1 + v, "LFO", "Waveform", ELXAR_LIST (lfoWaves));
    choice (PAR_RETRIGGER_LFO1 + v, "LFO", "Restart on", ELXAR_LIST (lfoRetrigger));
    value (PAR_OFFSET_LFO1 + v, "LFO", "Phase offset");
    p.push_back ({ PAR_TARGET_LFO1 + v, "LFO", "Target", K::lfoTarget, nullptr, 0 });

    // Mix
    value (PAR_VOL1 + v, "Mix", "Volume");
    value (panParam (v), "Mix", "Pan");
    value (PAR_VOICE_DECIMATION1 + v, "Mix", "Sample rate");
    const int drive = drum ? PAR_DRIVE1 + v : (snare ? (int) PAR_SNARE_DISTORTION : (cymbal ? (int) PAR_CYMBAL_DISTORTION : (int) PAR_HAT_DISTORTION));
    value (drive, "Mix", "Drive");
#undef ELXAR_LIST
    return p;
}

// "Filter: Frequency" style name for one parameter of a voice, or nullptr if the voice has no such parameter.
inline const ParamInfo* findVoiceParam (const std::vector<ParamInfo>& list, int par)
{
    for (const auto& i : list)
        if (i.par == par)
            return &i;
    return nullptr;
}
} // namespace elxar
