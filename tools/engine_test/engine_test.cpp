// ELXAR-Two headless engine test: no plugin, no Logic. Renders drum hits through the real engine code and
// writes WAV files, then checks the result for obvious faults. Run by GitHub Actions on every build.
//   engine_test [output folder] [sample rate]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include "ElxarEngine.h"

extern "C" {
#include "Parameters.h"
}

static bool writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int rate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4u;
    auto u32 = [f] (uint32_t v) { std::fputc ((int) (v & 255), f); std::fputc ((int) ((v >> 8) & 255), f); std::fputc ((int) ((v >> 16) & 255), f); std::fputc ((int) ((v >> 24) & 255), f); };
    auto u16 = [f] (uint32_t v) { std::fputc ((int) (v & 255), f); std::fputc ((int) ((v >> 8) & 255), f); };
    std::fputs ("RIFF", f); u32 (36u + dataBytes); std::fputs ("WAVEfmt ", f); u32 (16); u16 (1); u16 (2);
    u32 ((uint32_t) rate); u32 ((uint32_t) rate * 4u); u16 (4); u16 (16); std::fputs ("data", f); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        const float a = std::fmax (-1.0f, std::fmin (1.0f, l[i])), b = std::fmax (-1.0f, std::fmin (1.0f, r[i]));
        u16 ((uint32_t) (uint16_t) (int16_t) std::lrintf (a * 32767.0f));
        u16 ((uint32_t) (uint16_t) (int16_t) std::lrintf (b * 32767.0f));
    }
    std::fclose (f);
    return true;
}

struct Stats { float peak = 0; double rms = 0; bool finite = true; float tailPeak = 0; };

static Stats measure (const std::vector<float>& l, const std::vector<float>& r)
{
    Stats s;
    double sum = 0;
    const size_t n = l.size(), tailFrom = n - n / 10;
    for (size_t i = 0; i < n; ++i)
    {
        if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) s.finite = false;
        const float a = std::fmax (std::fabs (l[i]), std::fabs (r[i]));
        s.peak = std::fmax (s.peak, a);
        if (i >= tailFrom) s.tailPeak = std::fmax (s.tailPeak, a);
        sum += (double) l[i] * l[i] + (double) r[i] * r[i];
    }
    s.rms = std::sqrt (sum / (double) (2 * (n ? n : 1)));
    return s;
}

static void render (elxar::Engine& e, std::vector<float>& l, std::vector<float>& r, int frames)
{
    const size_t at = l.size();
    l.resize (at + (size_t) frames);
    r.resize (at + (size_t) frames);
    int done = 0;
    while (done < frames)   // odd chunk sizes on purpose, like a host
    {
        const int n = std::min (frames - done, 37 + (done % 480));
        e.render (l.data() + at + (size_t) done, r.data() + at + (size_t) done, n);
        done += n;
    }
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // print each line at once, so nothing is lost if the test stops early
    const std::string dir = argc > 1 ? argv[1] : ".";
    const int rate = argc > 2 ? std::atoi (argv[2]) : 48000;
    static const char* names[7] = { "drum1", "drum2", "drum3", "snare", "cymbal", "hihat_closed", "hihat_open" };
    int failures = 0;

    elxar::Engine engine;
    if (! engine.ownsEngine()) { std::printf ("FAIL: engine not available\n"); return 1; }
    engine.prepare ((double) rate);
    engine.setTempo (120.0);

    // 1. each voice on its own, 1.5 seconds
    for (int t = 0; t < 7; ++t)
    {
        std::vector<float> l, r;
        render (engine, l, r, rate / 10);
        engine.trigger (t, 127);
        render (engine, l, r, rate * 3 / 2);
        const Stats s = measure (l, r);
        const bool ok = s.finite && s.peak > 0.02f && s.peak <= 1.5f;
        std::printf ("%-13s peak %.3f  rms %.4f  tail %.4f  %s\n", names[t], (double) s.peak, s.rms, (double) s.tailPeak, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
        if (! writeWav (dir + "/elxar_" + names[t] + ".wav", l, r, rate)) { std::printf ("FAIL: cannot write WAV in %s\n", dir.c_str()); return 1; }
        for (int i = 0; i < 80; ++i)   // let it ring out (up to 8 seconds) before the next voice
        {
            std::vector<float> dl, dr;
            render (engine, dl, dr, rate / 10);
            if (measure (dl, dr).peak < 1.0e-4f) break;
        }
    }

    // 2. a two-bar beat at 120 BPM through each effect type
    static const char* fxNames[6] = { "off", "drive", "ringmod", "compressor", "delay", "reverb" };
    for (int fx = 0; fx < 6; ++fx)
    {
        engine.setParam (PAR_FX_TYPE, fx);
        engine.setParam (PAR_FX_DRY_WET, 50);
        engine.setParam (PAR_FX_P1, 64); engine.setParam (PAR_FX_P2, 64); engine.setParam (PAR_FX_P3, 64); engine.setParam (PAR_FX_P4, 64);
        for (int v = 0; v < 6; ++v) engine.setParam (PAR_AUDIO_OUT1 + v, fx == 0 ? 0 : 6);   // 6 = through the effect
        std::vector<float> l, r;
        const int step = rate / 8;   // 16th notes at 120 BPM
        for (int i = 0; i < 32; ++i)
        {
            if (i % 4 == 0) engine.trigger (0, 127);
            if (i % 8 == 4) engine.trigger (3, 110);
            if (i % 2 == 0) engine.trigger (5, 90);
            if (i % 16 == 14) engine.trigger (6, 100);
            if (i == 0) engine.trigger (4, 100);
            if (i % 16 == 10) engine.trigger (1, 100);
            if (i % 16 == 13) engine.trigger (2, 100);
            render (engine, l, r, step);
        }
        render (engine, l, r, rate);
        const Stats s = measure (l, r);
        const bool ok = s.finite && s.peak > 0.02f && s.peak <= 2.5f;
        std::printf ("beat fx=%-10s peak %.3f  rms %.4f  %s\n", fxNames[fx], (double) s.peak, s.rms, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
        writeWav (dir + "/elxar_beat_fx_" + fxNames[fx] + ".wav", l, r, rate);
        engine.silenceFx();
    }

    // 3. stress: random values on every kit parameter while triggering, to catch crashes and bad numbers
    {
        uint32_t seed = 12345u;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        std::vector<float> l, r;
        bool finite = true;
        for (int round = 0; round < 3000; ++round)
        {
            for (int k = 0; k < 24; ++k) engine.setParam ((int) (rnd() % ELXAR_NUM_KIT_PARAMS), (int) (rnd() % 128));
            for (int t = 0; t < 7; ++t) if (rnd() % 3 == 0) engine.trigger (t, 1 + (int) (rnd() % 127), 36 + (int) (rnd() % 48));
            engine.setTempo (40.0 + (double) (rnd() % 200));
            l.clear(); r.clear();
            render (engine, l, r, rate / 20);
            if (! measure (l, r).finite) finite = false;
        }
        std::printf ("stress: 3000 rounds of random parameters  %s\n", finite ? "ok" : "FAIL (non-finite output)");
        if (! finite) ++failures;
    }

    std::printf (failures ? "RESULT: %d FAILED\n" : "RESULT: all checks passed\n", failures);
    return failures ? 1 : 0;
}
