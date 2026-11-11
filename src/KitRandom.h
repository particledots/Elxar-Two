// ELXAR-Two: the "Random kit" maker, with names for its kits. No JUCE in this file.
//
// A random kit starts from the firmware's init kit and changes the sound settings of the six voices a little or a lot,
// depending on "wild" (0 = the init kit unchanged, 1 = as far as the ranges below go). Every voice gets ranges that suit
// it (a kick keeps a long, falling pitch; the hi-hat stays high and short; the cymbal stays long and noisy).
//
// Left alone on purpose, so a random kit is never suddenly loud, off to one side or sent through an effect you did not ask
// for: voice volume, pan, audio routing ("To FX"), the effect and its settings, LFOs and velocity targets. These come
// from the init kit, so a random kit also clears them.
//
// The name is made from the sound: the first word says how low or high the three drums are tuned, the second how gritty,
// long or short the kit is. Both words have 3 or 4 letters, so a name always fits the hardware's 8 characters.
//
// The ranges are an educated guess from the firmware's parameter meanings and the numbers in its init kit; there is no
// LXR-02 here to listen to. tools/kit_test renders thousands of random kits and checks that they are not silent, not
// broken and not mostly clipping; how good they sound is for the ear.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include "KitFile.h"

namespace elxar
{
namespace randomkit
{
struct Rng
{
    uint64_t s;
    explicit Rng (uint32_t seed) : s ((uint64_t) seed * 0x9E3779B97F4A7C15ull + 1u) {}
    uint32_t next()
    {
        s += 0x9E3779B97F4A7C15ull;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (uint32_t) ((z ^ (z >> 31)) >> 32);
    }
    float f() { return (float) next() * (1.0f / 4294967296.0f); }          // 0 up to (not including) 1
    int range (int lo, int hi) { return lo + (int) (next() % (uint32_t) (hi - lo + 1)); }   // lo..hi
    bool chance (float p) { return f() < p; }
};

struct Result
{
    kitfile::Params params {};
    std::string name;
};

namespace detail
{
inline const char* const lowWords[]   = { "DEEP", "DARK", "DUSK", "SLOW", "MOSS", "TIDE" };
inline const char* const midWords[]   = { "WARM", "CLAY", "SALT", "WOOD", "IRON", "DUST" };
inline const char* const highWords[]  = { "ZINC", "GLOW", "TINY", "SNAP", "SPIT", "LACE" };
inline const char* const grittyWords[] = { "RUST", "GRIT", "FUZZ", "BURN", "SOOT", "TAR" };
inline const char* const longWords[]  = { "TAIL", "HAZE", "WASH", "DRIP", "MIST", "ECHO" };
inline const char* const shortWords[] = { "TICK", "SNIP", "CLIP", "FLEA", "HOPS", "PIPS" };
inline const char* const plainWords[] = { "BEAT", "KIT", "LOOP", "PACK", "SET", "BOX" };
template <size_t N> const char* pick (Rng& r, const char* const (&list)[N]) { return list[r.next() % N]; }
}

inline Result make (uint32_t seed, float wild, int numModTargets)
{
    wild = std::max (0.0f, std::min (1.0f, wild));
    Rng r (seed);
    Result out;
    auto& k = out.params;
    for (int p = 0; p < ELXAR_NUM_KIT_PARAMS; ++p) k[(size_t) p] = elxar_init_kit_value (p);

    auto clampTo = [] (int v, int lo, int hi) { return (uint8_t) std::max (lo, std::min (hi, v)); };
    // around the init value, up to `spread` either way, scaled by wild
    auto around = [&] (int par, int spread, int lo = 0, int hi = 127)
    {
        const float d = (r.f() * 2.0f - 1.0f) * (float) spread * wild;
        k[(size_t) par] = clampTo ((int) k[(size_t) par] + (int) (d >= 0 ? d + 0.5f : d - 0.5f), lo, hi);
    };
    auto sometimes = [&] (float p) { return r.chance (p * wild); };

    for (int v = 0; v < ELXAR_NUM_VOICES; ++v)
    {
        const bool drum = v < 3, snare = v == 3, cymbal = v == 4, hat = v == 5;

        // ---- oscillator ----
        around (PAR_COARSE1 + 2 * v, drum ? 28 : (snare ? 18 : 14));
        const int oscWave = drum ? PAR_OSC_WAVE_DRUM1 + v : (snare ? (int) PAR_OSC_WAVE_SNARE : (cymbal ? (int) PAR_WAVE1_CYM : (int) PAR_WAVE1_HH));
        if (sometimes (0.5f))
        {
            // waveforms: 0 Sine, 1 Triangle, 2 Saw, 3 Rectangle, 4 Noise, 5 Pulse
            if (drum)        k[(size_t) oscWave] = (uint8_t) (r.chance (0.7f) ? r.range (0, 1) : r.range (2, 3));
            else if (snare)  k[(size_t) oscWave] = (uint8_t) (r.chance (0.6f) ? r.range (0, 1) : r.range (2, 3));
            else if (cymbal) k[(size_t) oscWave] = (uint8_t) (r.chance (0.7f) ? 4 : r.range (2, 3));
            else             k[(size_t) oscWave] = (uint8_t) r.range (0, 3);   // measured: on the hi-hat "Noise" is nearly silent and "Pulse" silent (at pulse width 0)
        }

        // ---- amp envelope (a bigger number is a longer sound) ----
        if (hat)
        {
            around (PAR_VELOD6_CLOSED, 12, 2, 40);
            around (PAR_VELOD6_OPEN, 25, 8, 80);
        }
        else
        {
            const int lo = cymbal ? 40 : 2, hi = cymbal ? 127 : (snare ? 50 : 70);
            around (PAR_VELOD1 + 2 * v, cymbal ? 30 : (snare ? 22 : 30), lo, hi);
        }
        around (PAR_VOL_SLOPE1 + v, 15);
        if (snare && sometimes (0.25f)) k[PAR_REPEAT4] = (uint8_t) r.range (10, 80);
        if (cymbal && sometimes (0.15f)) k[PAR_REPEAT5] = (uint8_t) r.range (10, 80);

        // ---- pitch envelope (drums and snare) ----
        if (drum || snare)
        {
            around (PAR_MOD_EG1 + v, 30);
            around (PAR_PITCH_SLOPE1 + v, 20);
            around (PAR_MODAMNT1 + v, 45);
        }

        // ---- FM (drums) / the two modulation oscillators (cymbal, hi-hat) ----
        if (drum && sometimes (0.5f))
        {
            k[(size_t) (PAR_FMAMNT1 + 2 * v)] = (uint8_t) r.range (0, 70);
            k[(size_t) (PAR_FM_FREQ1 + 2 * v)] = (uint8_t) r.range (0, 127);
            k[(size_t) (PAR_MOD_WAVE_DRUM1 + v)] = (uint8_t) r.range (0, 3);
            k[(size_t) (PAR_MIX_MOD_1 + v)] = (uint8_t) r.range (0, 1);
        }
        if (cymbal || hat)
        {
            const int f1 = cymbal ? PAR_MOD_OSC_F1_CYM : PAR_MOD_OSC_F1, f2 = cymbal ? PAR_MOD_OSC_F2_CYM : PAR_MOD_OSC_F2;
            const int g1 = cymbal ? PAR_MOD_OSC_GAIN1_CYM : PAR_MOD_OSC_GAIN1, g2 = cymbal ? PAR_MOD_OSC_GAIN2_CYM : PAR_MOD_OSC_GAIN2;
            const int w1 = cymbal ? PAR_WAVE2_CYM : PAR_WAVE2_HH, w2 = cymbal ? PAR_WAVE3_CYM : PAR_WAVE3_HH;
            around (f1, 40);
            around (f2, 40);
            around (g1, 40);
            around (g2, 40);
            if (sometimes (0.4f)) { k[(size_t) w1] = (uint8_t) r.range (0, 4); k[(size_t) w2] = (uint8_t) r.range (0, 4); }   // not "Pulse": nearly silent at pulse width 0
        }
        if (snare)
        {
            around (PAR_NOISE_FREQ1, 40);
            around (PAR_MIX1, 35);
        }

        // ---- transient (the short click at the start) ----
        if (sometimes (0.5f))
        {
            k[(size_t) (PAR_TRANS1_WAVE + v)] = (uint8_t) r.range (0, 13);
            k[(size_t) (PAR_TRANS1_VOL + v)] = (uint8_t) r.range (20, 90);
            k[(size_t) (PAR_TRANS1_FREQ + v)] = (uint8_t) r.range (0, 127);
        }

        // ---- filter ----
        if (cymbal || hat) around (PAR_FILTER_FREQ_1 + v, 25, 40, 104);
        else if (sometimes (0.4f)) k[(size_t) (PAR_FILTER_FREQ_1 + v)] = (uint8_t) r.range (45, 127);
        if (sometimes (cymbal || hat ? 0.2f : 0.3f)) k[(size_t) (PAR_FILTER_TYPE_1 + v)] = (uint8_t) r.range (0, 6);   // never "Off"
        {
            // Filter types: 0 low-pass, 1 high-pass, 2 and 3 band-pass, 4 notch, 5 peak, 6 low-pass 2. A high-pass or band-pass
            // at the top of its range lets nothing through, so a drum would be silent: keep the cutoff where sound passes.
            const int type = k[(size_t) (PAR_FILTER_TYPE_1 + v)];
            uint8_t& cutoff = k[(size_t) (PAR_FILTER_FREQ_1 + v)];
            if (type == 1 && (drum || snare) && cutoff > 60) cutoff = (uint8_t) r.range (5, 60);
            else if ((type == 2 || type == 3) && (cutoff > 100 || cutoff < 20)) cutoff = (uint8_t) r.range (20, 100);
        }
        if (sometimes (0.4f)) k[(size_t) (PAR_RESO_1 + v)] = (uint8_t) r.range (0, 70);
        if (sometimes (0.3f)) k[(size_t) (PAR_FILTER_DRIVE_1 + v)] = (uint8_t) r.range (0, 60);

        // ---- mix: drive and sample rate (volume and pan stay as in the init kit) ----
        const int drive = drum ? PAR_DRIVE1 + v : (snare ? (int) PAR_SNARE_DISTORTION : (cymbal ? (int) PAR_CYMBAL_DISTORTION : (int) PAR_HAT_DISTORTION));
        around (drive, 20, 20, 110);
        if (sometimes (0.15f)) k[(size_t) (PAR_VOICE_DECIMATION1 + v)] = (uint8_t) r.range (90, 126);
    }

    kitfile::sanitise (k, numModTargets);

    // ---- the name ----
    const int tuneAverage = ((int) k[PAR_COARSE1] + (int) k[PAR_COARSE2] + (int) k[PAR_COARSE3]) / 3;       // init kit: 43
    int decayAverage = 0, gritCount = 0;
    for (int v = 0; v < 4; ++v) decayAverage += k[(size_t) (PAR_VELOD1 + 2 * v)];
    decayAverage /= 4;                                                                                       // init kit: 11
    for (int v = 0; v < ELXAR_NUM_VOICES; ++v)
    {
        if (k[(size_t) (PAR_TRANS1_VOL + v)] > 0) ++gritCount;
        if (k[(size_t) (PAR_FILTER_DRIVE_1 + v)] > 25) ++gritCount;
        if (k[(size_t) (PAR_VOICE_DECIMATION1 + v)] < 127) ++gritCount;
    }
    Rng nameRng (seed ^ 0xA5A5A5A5u);
    const char* first = tuneAverage < 34 ? detail::pick (nameRng, detail::lowWords)
                       : (tuneAverage > 52 ? detail::pick (nameRng, detail::highWords) : detail::pick (nameRng, detail::midWords));
    const char* second = gritCount >= 5 ? detail::pick (nameRng, detail::grittyWords)
                        : (decayAverage > 22 ? detail::pick (nameRng, detail::longWords)
                        : (decayAverage < 8 ? detail::pick (nameRng, detail::shortWords) : detail::pick (nameRng, detail::plainWords)));
    out.name = std::string (first) + second;
    if (out.name.size() > (size_t) kitfile::kNameBytes) out.name.resize ((size_t) kitfile::kNameBytes);
    return out;
}
} // namespace randomkit
} // namespace elxar
