// ELXAR-Two: C++ wrapper around the C engine. Adds sample-rate conversion from the engine's fixed rate
// (44.14 kHz, the hardware's real rate) to whatever rate the host runs at. No JUCE here, so the same code is
// used by the plugin and by the headless engine_test.
//
// One engine per process (the firmware keeps its state in global variables): only the instance that owns the
// engine makes sound. See README "Known limits".
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>
#include "elxar_engine.h"

namespace elxar
{
// Windowed-sinc streaming resampler, adapted from particledots/beads-plugin (src/Resampler.h) so that it never
// allocates memory while audio is running.
class BlockResampler
{
public:
    static constexpr int kTaps = 32;
    static constexpr int kPhases = 1024;
    static constexpr int kMaxIn = ELXAR_BLOCK;
    static constexpr int kMaxOutPerBlock = 512;   // 64 input frames at up to ~8x

    void init (double inRate, double outRate)
    {
        step = inRate / outRate;
        const double cutoff = std::min (1.0, outRate / inRate) * 0.97;
        table.assign ((size_t) kPhases * (size_t) kTaps, 0.0f);
        const double pi = 3.14159265358979323846;
        for (int p = 0; p < kPhases; ++p)
        {
            const double frac = (double) p / (double) kPhases;
            double sum = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
                const double d = (double) (k - (kTaps / 2 - 1)) - frac;
                const double x = d * cutoff;
                const double s = std::abs (x) < 1e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
                const double w = 0.42 + 0.5 * std::cos (pi * d / (kTaps / 2)) + 0.08 * std::cos (2.0 * pi * d / (kTaps / 2));
                const double h = s * w * cutoff;
                table[(size_t) p * (size_t) kTaps + (size_t) k] = (float) h;
                sum += h;
            }
            for (int k = 0; k < kTaps; ++k)
                table[(size_t) p * (size_t) kTaps + (size_t) k] /= (float) sum;
        }
        reset();
    }

    void reset()
    {
        hist.fill (0.0f);
        histLen = kTaps;
        pos = 0.0;
    }

    // Feed exactly kMaxIn input frames; writes up to kMaxOutPerBlock output frames to `out`, returns how many.
    int process (const float* in, float* out)
    {
        std::memcpy (hist.data() + histLen, in, sizeof (float) * (size_t) kMaxIn);
        histLen += kMaxIn;
        int produced = 0;
        while ((int) pos + kTaps <= histLen && produced < kMaxOutPerBlock)
        {
            const int i = (int) pos;
            int ph = (int) ((pos - (double) i) * (double) kPhases);
            if (ph >= kPhases) ph = kPhases - 1;
            const float* h = &table[(size_t) ph * (size_t) kTaps];
            const float* x = &hist[(size_t) i];
            float acc = 0.0f;
            for (int k = 0; k < kTaps; ++k) acc += h[k] * x[k];
            out[produced++] = acc;
            pos += step;
        }
        const int drop = std::min ((int) pos, histLen);
        if (drop > 0)
        {
            std::memmove (hist.data(), hist.data() + drop, sizeof (float) * (size_t) (histLen - drop));
            histLen -= drop;
            pos -= (double) drop;
        }
        return produced;
    }

private:
    std::vector<float> table;
    std::array<float, (size_t) (kTaps * 4 + kMaxIn * 2)> hist {};
    int histLen = kTaps;
    double step = 1.0, pos = 0.0;
};

class Engine
{
public:
    Engine() : owner (! taken().exchange (true)) {}
    ~Engine() { if (owner) taken().store (false); }
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    // false = another instance in this process already owns the one engine; this one stays silent.
    bool ownsEngine() const { return owner; }

    void prepare (double hostRate)
    {
        rate = hostRate > 1000.0 ? hostRate : 44100.0;
        rsL.init (ELXAR_NATIVE_RATE, rate);
        rsR.init (ELXAR_NATIVE_RATE, rate);
        fifoRead = fifoCount = 0;
        if (owner && ! initialised) { elxar_init(); initialised = true; }
    }

    void setParam (int par, int value)   { if (owner && initialised) elxar_set_param ((uint16_t) par, (uint8_t) std::clamp (value, 0, 127)); }
    int  getParam (int par) const        { return owner && initialised ? elxar_get_param ((uint16_t) par) : 0; }
    void trigger (int track, int velocity, int note = ELXAR_DEFAULT_NOTE)
    {
        if (owner && initialised) elxar_trigger ((uint8_t) track, (uint8_t) std::clamp (velocity, 0, 127), (uint8_t) std::clamp (note, 0, 127));
    }
    void setTempo (double bpm)           { if (owner && initialised) elxar_set_tempo ((float) bpm); }
    void setFader (int voice, float g)   { if (owner && initialised) elxar_set_fader ((uint8_t) voice, g); }
    void silenceFx()                     { if (owner && initialised) elxar_silence_fx(); }

    // Render n frames at the host rate.
    void render (float* outL, float* outR, int n)
    {
        if (! owner || ! initialised)
        {
            std::fill (outL, outL + n, 0.0f);
            std::fill (outR, outR + n, 0.0f);
            return;
        }
        int done = 0;
        while (done < n)
        {
            if (fifoCount == 0) fillFifo();
            const int take = std::min (n - done, fifoCount);
            for (int i = 0; i < take; ++i)
            {
                outL[done + i] = fifoL[(size_t) ((fifoRead + i) & kFifoMask)];
                outR[done + i] = fifoR[(size_t) ((fifoRead + i) & kFifoMask)];
            }
            fifoRead = (fifoRead + take) & kFifoMask;
            fifoCount -= take;
            done += take;
        }
    }

private:
    static constexpr int kFifoSize = 2048, kFifoMask = kFifoSize - 1;

    static std::atomic<bool>& taken() { static std::atomic<bool> t { false }; return t; }

    void fillFifo()
    {
        float inL[ELXAR_BLOCK], inR[ELXAR_BLOCK];
        float outL[BlockResampler::kMaxOutPerBlock], outR[BlockResampler::kMaxOutPerBlock];
        elxar_render_block (inL, inR);
        const int a = rsL.process (inL, outL);
        const int b = rsR.process (inR, outR);
        const int got = std::min (a, b);
        const int w = (fifoRead + fifoCount) & kFifoMask;
        for (int i = 0; i < got; ++i)
        {
            // a bad number must never reach the host
            fifoL[(size_t) ((w + i) & kFifoMask)] = std::isfinite (outL[i]) ? std::clamp (outL[i], -4.0f, 4.0f) : 0.0f;
            fifoR[(size_t) ((w + i) & kFifoMask)] = std::isfinite (outR[i]) ? std::clamp (outR[i], -4.0f, 4.0f) : 0.0f;
        }
        fifoCount += got;
    }

    const bool owner;
    bool initialised = false;
    double rate = 44100.0;
    BlockResampler rsL, rsR;
    std::array<float, kFifoSize> fifoL {}, fifoR {};
    int fifoRead = 0, fifoCount = 0;
};
} // namespace elxar
