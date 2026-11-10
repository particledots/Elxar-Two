// ELXAR-Two headless engine test: no plugin, no Logic. Renders drum hits through the real engine code and
// writes WAV files, then checks the result for obvious faults. Run by GitHub Actions on every build.
//   engine_test [output folder] [sample rate]
#include <algorithm>
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

// ---- helpers for the sequencer checks ----
static std::vector<ElxarStep> emptyTrack()
{
    std::vector<ElxarStep> tr ((size_t) ELXAR_NUM_STEPS);
    for (auto& s : tr) { s.on = 0; s.velocity = 110; s.probability = 100; s.note = ELXAR_DEFAULT_NOTE; s.flam = 0; s.shift = ELXAR_SHIFT_NONE;
                         s.autoTarget[0] = s.autoTarget[1] = 0; s.autoValue[0] = s.autoValue[1] = 0; }
    return tr;
}

static void clearAllTracks (elxar::Engine& e, int pattern)
{
    const std::vector<ElxarStep> tr = emptyTrack();
    for (int t = 0; t < 7; ++t) e.seqWriteTrack (pattern, t, tr.data(), 16);
    e.seqWriteShuffle (pattern, 0);
}

// Runs `steps` steps from song step `from`, and returns for one track the list of pulse numbers (step x 24 + pulse)
// at which the sequencer played it.
static std::vector<int> runAndLog (elxar::Engine& e, int track, uint32_t from, int steps, bool start = true)
{
    std::vector<int> hits;
    if (start) e.seqStart (from);
    for (int i = 0; i < steps * ELXAR_PULSES_PER_STEP; ++i)
    {
        const uint32_t before = e.seqTriggerCount (track);
        e.seqPulse();
        for (uint32_t k = e.seqTriggerCount (track); k > before; --k) hits.push_back (i);
    }
    return hits;
}

static bool expect (const char* what, const std::vector<int>& got, const std::vector<int>& want)
{
    const bool ok = got == want;
    std::printf ("seq %-34s %s", what, ok ? "ok\n" : "FAIL  got:");
    if (! ok) { for (int v : got) std::printf (" %d", v); std::printf ("  want:"); for (int v : want) std::printf (" %d", v); std::printf ("\n"); }
    return ok;
}

static int sequencerChecks (elxar::Engine& e)
{
    int failures = 0;
    const int P = ELXAR_PULSES_PER_STEP;
    for (int p = 0; p < 4; ++p) clearAllTracks (e, p);
    e.seqSelectPattern (0);

    // four on the floor: steps 1, 5, 9, 13, exactly on the step
    std::vector<ElxarStep> tr = emptyTrack();
    for (int i : { 0, 4, 8, 12 }) tr[(size_t) i].on = 1;
    e.seqWriteTrack (0, 0, tr.data(), 16);
    if (! expect ("steps on the grid", runAndLog (e, 0, 0, 16), { 0, 4 * P, 8 * P, 12 * P })) ++failures;
    e.seqStop();

    // a track with its own length (3 steps) against the 16-step tracks
    tr = emptyTrack(); tr[0].on = 1;
    e.seqWriteTrack (0, 1, tr.data(), 3);
    if (! expect ("track length 3", runAndLog (e, 1, 0, 12), { 0, 3 * P, 6 * P, 9 * P })) ++failures;
    e.seqStop();

    // start in the middle of the song: song step 37 is step 6 of a 16-step track and step 2 of a 3-step track
    if (! expect ("song position, 16 steps", runAndLog (e, 0, 37, 8), { 3 * P, 7 * P })) ++failures;   // steps 8 and 12 come after 3 and 7 steps
    e.seqStop();
    if (! expect ("song position, 3 steps", runAndLog (e, 1, 37, 6), { 2 * P, 5 * P })) ++failures;
    e.seqStop();

    // flam x3 = three hits inside one step; shift +2 = 6 pulses late; shift -2 = 6 pulses early (inside the step before)
    tr = emptyTrack(); tr[2].on = 1; tr[2].flam = 2;
    e.seqWriteTrack (0, 2, tr.data(), 16);
    if (! expect ("flam x3", runAndLog (e, 2, 0, 16), { 2 * P, 2 * P + 8, 2 * P + 16 })) ++failures;
    e.seqStop();
    tr = emptyTrack(); tr[2].on = 1; tr[2].shift = 9;
    e.seqWriteTrack (0, 2, tr.data(), 16);
    if (! expect ("shift later", runAndLog (e, 2, 0, 16), { 2 * P + 6 })) ++failures;
    e.seqStop();
    tr = emptyTrack(); tr[2].on = 1; tr[2].shift = 5;
    e.seqWriteTrack (0, 2, tr.data(), 16);
    if (! expect ("shift earlier", runAndLog (e, 2, 0, 16), { 2 * P - 6 })) ++failures;
    e.seqStop();
    // an early (shifted) hit on one track must not swallow hits on the other tracks (firmware review S-B4)
    tr = emptyTrack(); tr[1].on = 1;
    e.seqWriteTrack (0, 3, tr.data(), 16);
    if (! expect ("other track keeps its hit", runAndLog (e, 3, 0, 16), { 1 * P })) ++failures;
    e.seqStop();
    e.seqWriteTrack (0, 2, emptyTrack().data(), 16);
    e.seqWriteTrack (0, 3, emptyTrack().data(), 16);

    // probability: 0 never, 100 always, 50 about half
    {
        tr = emptyTrack(); tr[0].on = 1; tr[0].probability = 0;
        e.seqWriteTrack (0, 4, tr.data(), 1);
        const size_t never = runAndLog (e, 4, 0, 400).size(); e.seqStop();
        tr[0].probability = 100; e.seqWriteTrack (0, 4, tr.data(), 1);
        const size_t always = runAndLog (e, 4, 0, 400).size(); e.seqStop();
        tr[0].probability = 50; e.seqWriteTrack (0, 4, tr.data(), 1);
        const size_t half = runAndLog (e, 4, 0, 400).size(); e.seqStop();
        // (the firmware's random numbers start at 0, so the very first pass after power-up can play a 0 % step once or twice)
        const bool ok = never <= 2 && always == 400 && half > 140 && half < 260;
        std::printf ("seq %-34s %s (0%%: %d, 100%%: %d, 50%%: %d of 400)\n", "probability", ok ? "ok" : "FAIL", (int) never, (int) always, (int) half);
        if (! ok) ++failures;
        e.seqWriteTrack (0, 4, emptyTrack().data(), 16);
    }

    // pattern change: at once while stopped, at the end of the longest track while running
    {
        tr = emptyTrack(); tr[0].on = 1; tr[1].on = 1;
        e.seqWriteTrack (1, 6, tr.data(), 16);          // pattern 2 plays track 6 on steps 1 and 2; pattern 1 never plays it
        e.seqWriteTrack (0, 1, emptyTrack().data(), 16); // all tracks of pattern 1 are 16 long again
        e.seqSelectPattern (1);
        const bool immediate = e.seqActivePattern() == 1;
        e.seqSelectPattern (0);
        e.seqStart (0);
        for (int i = 0; i < 5 * P; ++i) e.seqPulse();
        e.seqSelectPattern (1);                         // asked for during step 5
        const bool notYet = e.seqActivePattern() == 0;
        const std::vector<int> hits = runAndLog (e, 6, 0, 16, false);   // continues from step 5: pattern 2 starts 11 steps later
        const bool ok = immediate && notYet && e.seqActivePattern() == 1 && hits == std::vector<int> ({ 11 * P, 12 * P });
        std::printf ("seq %-34s %s\n", "pattern change", ok ? "ok" : "FAIL");
        if (! ok) { ++failures; for (int v : hits) std::printf (" %d", v); std::printf (" (immediate %d, notYet %d, active %d)\n", (int) immediate, (int) notYet, e.seqActivePattern()); }
        e.seqStop();
        e.seqSelectPattern (0);
        // an edit made to pattern 1 before the change must still be there afterwards (the firmware would undo it)
        if (! expect ("edits survive a pattern change", runAndLog (e, 0, 0, 16), { 0, 4 * P, 8 * P, 12 * P })) ++failures;
        e.seqStop();
    }

    // per-step automation: a step that sets Drum 1's pitch far up must sound different, and stopping must undo it
    {
        int target = 0;
        for (int i = 0; i < elxar_num_mod_targets(); ++i) if (elxar_mod_target_param (i) == PAR_COARSE1) target = i;
        { std::vector<float> dl, dr; render (e, dl, dr, 96000); }   // let the hits of the checks above ring out first
        auto crossings = [&e] (bool automated, int target_)
        {
            std::vector<ElxarStep> t2 = emptyTrack(); t2[0].on = 1;
            if (automated) { t2[0].autoTarget[0] = (uint8_t) target_; t2[0].autoValue[0] = 120; }
            e.seqWriteTrack (0, 0, t2.data(), 16);
            e.seqStart (0); e.seqPulse();
            std::vector<float> l, r; render (e, l, r, 4800);
            e.seqStop();
            std::vector<float> dl, dr; render (e, dl, dr, 48000);
            int n = 0; bool up = false;   // count swings past +-0.02 only, so the quiet noise floor is not counted
            for (float v : l) { if (! up && v > 0.02f) { up = true; ++n; } else if (up && v < -0.02f) up = false; }
            return n;
        };
        const int plain = crossings (false, target), locked = crossings (true, target), plainAgain = crossings (false, target);
        const bool ok = target > 0 && locked > plain * 2 && std::abs (plainAgain - plain) <= 1 + plain / 4;
        std::printf ("seq %-34s %s (cycles in 0.1 s: plain %d, automated %d, plain again %d)\n", "per-step automation", ok ? "ok" : "FAIL", plain, locked, plainAgain);
        if (! ok) ++failures;
    }

    // the firmware's Euclidean generator: the right number of hits, spread as evenly as possible
    {
        int badCount = 0, badSpread = 0, firstBadLength = 0;
        for (int len = 1; len <= 16; ++len)   // 16 is the firmware's limit (its own menu allows 1 to 16)
            for (int hits = 1; hits <= len; ++hits)
            {
                const uint64_t bits = elxar_euclid ((uint8_t) len, (uint8_t) hits, 0);
                int n = 0, gapMin = 999, gapMax = 0, last = -1, first = -1;
                for (int i = 0; i < 64; ++i)
                    if (bits & (1ull << i))
                    {
                        if (i >= len) n += 100;
                        ++n;
                        if (last >= 0) { gapMin = std::min (gapMin, i - last); gapMax = std::max (gapMax, i - last); }
                        else first = i;
                        last = i;
                    }
                if (n == hits && hits > 1) { const int wrap = first + len - last; gapMin = std::min (gapMin, wrap); gapMax = std::max (gapMax, wrap); }
                const bool countOk = n == hits, spreadOk = hits < 2 || gapMax - gapMin <= 1;
                if (! countOk) ++badCount; else if (! spreadOk) ++badSpread;
                if ((! countOk || ! spreadOk) && firstBadLength == 0) firstBadLength = len;
            }
        const uint64_t e38 = elxar_euclid (8, 3, 0), e38r = elxar_euclid (8, 3, 1);
        int n38 = 0; for (int i = 0; i < 8; ++i) if (e38 & (1ull << i)) ++n38;
        const bool rotOk = e38r == (((e38 << 1) | (e38 >> 7)) & 0xFFull);
        std::printf ("seq euclid: lengths 1..16: %d wrong hit counts, %d uneven; E(3,8) = ", badCount, badSpread);
        for (int i = 0; i < 8; ++i) std::printf ("%c", (e38 & (1ull << i)) ? 'x' : '.');
        const bool ok = n38 == 3 && rotOk && firstBadLength == 0;
        std::printf ("  %s\n", ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }
    for (int p = 0; p < 4; ++p) clearAllTracks (e, p);
    return failures;
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

    // 3. the firmware sequencer, clocked pulse by pulse (24 pulses = one step). No sound is needed for these
    //    checks: the test counts when each track is played.
    failures += sequencerChecks (engine);

    // 4. a sequenced beat rendered the way the plugin does it: host position -> clock pulses -> firmware sequencer.
    //    Two bars at 120 BPM in "host blocks" of 480 samples, with a flam, a shifted step and shuffle.
    {
        engine.setParam (PAR_FX_TYPE, 0);
        for (int v = 0; v < 6; ++v) engine.setParam (PAR_AUDIO_OUT1 + v, 0);
        clearAllTracks (engine, 0);
        std::vector<ElxarStep> tr = emptyTrack();
        for (int i : { 0, 4, 8, 12 }) tr[(size_t) i].on = 1;
        engine.seqWriteTrack (0, 0, tr.data(), 16);
        tr = emptyTrack(); tr[4].on = 1; tr[12].on = 1; tr[12].flam = 2;
        engine.seqWriteTrack (0, 3, tr.data(), 16);
        tr = emptyTrack(); for (int i = 0; i < 16; i += 2) { tr[(size_t) i].on = 1; tr[(size_t) i].velocity = (uint8_t) (i % 4 == 0 ? 120 : 70); }
        tr[7].on = 1; tr[7].shift = 9;
        engine.seqWriteTrack (0, 5, tr.data(), 16);
        engine.seqWriteShuffle (0, 40);
        elxar::SeqClock clock;
        clock.reset();
        std::vector<float> l, r;
        std::vector<int> offsets ((size_t) elxar::SeqClock::kMaxPulses);
        const double stepsPerSample = 120.0 * 4.0 / (60.0 * (double) rate);
        const int block = 480, blocks = (int) (32.0 / (stepsPerSample * block)) + 1;
        uint32_t before = 0; for (int t = 0; t < 7; ++t) before += engine.seqTriggerCount (t);
        long long samplePos = 0;
        for (int b = 0; b < blocks; ++b)
        {
            const int n = clock.advance (engine, true, (double) samplePos * stepsPerSample, stepsPerSample, block, offsets.data());
            const size_t at = l.size();
            l.resize (at + (size_t) block); r.resize (at + (size_t) block);
            int pos = 0;
            for (int i = 0; i < n; ++i)
            {
                if (offsets[(size_t) i] > pos) { engine.render (l.data() + at + (size_t) pos, r.data() + at + (size_t) pos, offsets[(size_t) i] - pos); pos = offsets[(size_t) i]; }
                engine.seqPulse();
            }
            if (pos < block) engine.render (l.data() + at + (size_t) pos, r.data() + at + (size_t) pos, block - pos);
            samplePos += block;
        }
        clock.advance (engine, false, 0.0, stepsPerSample, block, offsets.data());   // stop
        render (engine, l, r, rate);
        uint32_t after = 0; for (int t = 0; t < 7; ++t) after += engine.seqTriggerCount (t);
        const Stats st = measure (l, r);
        // two bars: 8 kicks + 2 x (1 snare + 3 flam hits) + 2 x 9 hats = 34, plus the kick and hat that start bar 3
        // (at 48 kHz the last block reaches exactly into bar 3; other rates may stop just before it)
        const uint32_t played = after - before;
        const bool ok = st.finite && st.peak > 0.02f && st.peak <= 2.5f && (played == 36 || played == 34) && ! engine.seqRunning();
        std::printf ("sequenced beat: %u hits, peak %.3f  %s\n", (unsigned) played, (double) st.peak, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
        writeWav (dir + "/elxar_sequencer_beat.wav", l, r, rate);
        engine.seqWriteShuffle (0, 0);
    }

    // 5. stress: random values on every kit parameter while triggering, to catch crashes and bad numbers.
    //    Since Milestone 2 the sequencer runs at the same time with random patterns, automation, jumps and pattern changes.
    {
        uint32_t seed = 12345u;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        std::vector<float> l, r;
        bool finite = true;
        for (int round = 0; round < 3000; ++round)
        {
            for (int k = 0; k < 24; ++k) engine.setParam ((int) (rnd() % ELXAR_NUM_KIT_PARAMS), (int) (rnd() % 128));
            if (round % 7 == 0)   // a random track in a random pattern
            {
                std::vector<ElxarStep> tr ((size_t) ELXAR_NUM_STEPS);
                for (auto& st : tr)
                {
                    st.on = (uint8_t) (rnd() % 3 == 0); st.velocity = (uint8_t) (rnd() % 128); st.probability = (uint8_t) (rnd() % 101);
                    st.note = (uint8_t) (rnd() % 128); st.flam = (uint8_t) (rnd() % 6); st.shift = (uint8_t) (rnd() % 15);
                    st.autoTarget[0] = (uint8_t) (rnd() % 4 ? 0 : rnd() % (uint32_t) elxar_num_mod_targets()); st.autoValue[0] = (uint8_t) (rnd() % 128);
                    st.autoTarget[1] = (uint8_t) (rnd() % 4 ? 0 : rnd() % (uint32_t) elxar_num_mod_targets()); st.autoValue[1] = (uint8_t) (rnd() % 128);
                }
                engine.seqWriteTrack ((int) (rnd() % 16), (int) (rnd() % 7), tr.data(), 1 + (int) (rnd() % 64));
                engine.seqWriteShuffle ((int) (rnd() % 16), (int) (rnd() % 128));
            }
            switch (rnd() % 40)
            {
                case 0: engine.seqStop(); break;
                case 1: case 2: engine.seqStart (rnd() % 100000u); break;
                case 3: engine.seqLocate (rnd() % 100000u); break;
                case 4: case 5: engine.seqSelectPattern ((int) (rnd() % 16)); break;
                default: break;
            }
            for (int k = (int) (rnd() % 60); k > 0; --k) engine.seqPulse();
            for (int t = 0; t < 7; ++t) if (rnd() % 3 == 0) engine.trigger (t, 1 + (int) (rnd() % 127), 36 + (int) (rnd() % 48));
            engine.setTempo (40.0 + (double) (rnd() % 200));
            l.clear(); r.clear();
            render (engine, l, r, rate / 20);
            if (! measure (l, r).finite) finite = false;
        }
        engine.seqStop();
        std::printf ("stress: 3000 rounds of random parameters and sequencer use  %s\n", finite ? "ok" : "FAIL (non-finite output)");
        if (! finite) ++failures;
    }

    std::printf (failures ? "RESULT: %d FAILED\n" : "RESULT: all checks passed\n", failures);
    return failures ? 1 : 0;
}
