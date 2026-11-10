// ELXAR-Two: C++ wrapper around the C engine. Adds sample-rate conversion from the engine's fixed rate
// (44.14 kHz, the hardware's real rate) to whatever rate the host runs at. No JUCE here, so the same code is
// used by the plugin and by the headless engine_test.
//
// One engine per process (the firmware keeps its state in global variables): only the instance that owns the
// engine makes sound. See README "Known limits".
//
// Threads: everything on Engine must be called from the audio thread (or before audio starts), except where a
// function says otherwise.
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
    bool ready() const                   { return owner && initialised; }

    // ---- the firmware's step sequencer (see elxar_engine.h) ----
    void seqWriteTrack (int pattern, int track, const ElxarStep* steps, int length)
    {
        if (owner && initialised) elxar_seq_write_track ((uint8_t) pattern, (uint8_t) track, steps, (uint8_t) std::clamp (length, 1, ELXAR_NUM_STEPS));
    }
    void seqWriteShuffle (int pattern, int shuffle) { if (owner && initialised) elxar_seq_write_shuffle ((uint8_t) pattern, (uint8_t) std::clamp (shuffle, 0, 127)); }
    void seqStart (uint32_t absoluteStep)  { if (owner && initialised) elxar_seq_start (absoluteStep); }
    void seqLocate (uint32_t absoluteStep) { if (owner && initialised) elxar_seq_locate (absoluteStep); }
    void seqStop()                         { if (owner && initialised) elxar_seq_stop(); }
    void seqPulse()                        { if (owner && initialised) elxar_seq_pulse(); }
    bool seqRunning() const                { return owner && initialised && elxar_seq_running() != 0; }
    void seqSelectPattern (int pattern)    { if (owner && initialised) elxar_seq_select_pattern ((uint8_t) std::clamp (pattern, 0, ELXAR_NUM_PATTERNS - 1)); }
    int  seqActivePattern() const          { return owner && initialised ? (int) elxar_seq_active_pattern() : 0; }
    int  seqPosition (int track) const     { return owner && initialised ? elxar_seq_position ((uint8_t) track) : -1; }
    float seqShuffle() const               { return owner && initialised ? elxar_seq_shuffle() : 0.0f; }
    uint32_t seqTriggerCount (int track) const { return owner && initialised ? elxar_seq_trigger_count ((uint8_t) track) : 0u; }

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
// Turns the host's song position into clock pulses for the firmware sequencer.
//
// The firmware sequencer wants 24 pulses for every step (a step is a 16th note). On the hardware a millisecond
// timer makes them; here they are worked out from where the song is, so the pattern stays locked to the host:
//   - when the host starts, or jumps (cycle, locate), the sequencer is put on the matching step,
//   - shuffle delays every second step, by the same amount as the firmware's own clock does
//     (shuffle x 16 pulses; the pulses that fall inside the delay are sent together when it ends).
// The same class is used by the plugin and by the headless engine_test.
class SeqClock
{
public:
    static constexpr int kMaxPulses = 2048;

    void reset() { running = false; expected = -1.0e18; nextPulse = 0; }
    bool isRunning() const { return running; }

    // Call once per audio block, before rendering it.
    //   run            : should the sequencer run in this block?
    //   posSteps       : song position at the start of the block, in steps (quarter notes x 4)
    //   stepsPerSample : tempo
    // Writes the sample offsets (inside this block, rising) at which engine.seqPulse() must be called, returns how many.
    int advance (Engine& engine, bool run, double posSteps, double stepsPerSample, int numSamples, int* offsets)
    {
        if (! run || ! (stepsPerSample > 0.0) || numSamples <= 0 || ! engine.ready())
        {
            if (running) { engine.seqStop(); running = false; }
            expected = -1.0e18;
            return 0;
        }
        const double end = posSteps + stepsPerSample * (double) numSamples;
        // Not running yet, or the song position is not where the last block left it (more than half a step away).
        if (! running || std::abs (posSteps - expected) > 0.5)
        {
            // Start on the next step line (or this one, if the position is exactly on it). Nothing plays before the song start.
            double k0 = std::ceil (posSteps - 1.0e-6);
            if (k0 < 0.0) k0 = 0.0;
            if (k0 > 4.0e9) k0 = std::fmod (k0, 3840.0);
            const auto step = (uint32_t) k0;
            if (! running) engine.seqStart (step); else engine.seqLocate (step);
            running = true;
            nextPulse = (long long) step * ELXAR_PULSES_PER_STEP;
        }
        expected = end;
        const double shuffleDelay = (double) engine.seqShuffle() * 16.0 / (double) ELXAR_PULSES_PER_STEP;   // in steps
        int n = 0;
        while (n < kMaxPulses)
        {
            const long long k = nextPulse / ELXAR_PULSES_PER_STEP;
            const int p = (int) (nextPulse % ELXAR_PULSES_PER_STEP);
            double t = (double) k + (double) p / (double) ELXAR_PULSES_PER_STEP;
            if ((k & 1) != 0 && shuffleDelay > 0.0) t = std::max (t, (double) k + shuffleDelay);
            if (t >= end - 1.0e-9) break;
            offsets[n++] = std::clamp ((int) ((t - posSteps) / stepsPerSample), 0, numSamples - 1);
            ++nextPulse;
        }
        return n;
    }

private:
    bool running = false;
    double expected = -1.0e18;   // where the song should be at the start of the next block
    long long nextPulse = 0;     // step x 24 + pulse inside the step
};
} // namespace elxar
