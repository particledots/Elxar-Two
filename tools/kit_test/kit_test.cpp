// ELXAR-Two kit test: no plugin, no Logic. Checks the .SND kit file reader and the Random kit maker against the real
// engine code. Run by GitHub Actions on every build.
//   kit_test [number of random kits per wild setting]   (default 150)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>
#include "KitFile.h"
#include "KitRandom.h"

static int failures = 0;
static void check (bool ok, const std::string& what)
{
    std::printf("%-78s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    std::fflush (stdout);
    if (! ok) ++failures;
}

// A kit file the way the firmware writes it: 8 name bytes, then the 247 parameters.
static std::vector<uint8_t> makeFile (const char* name8, const elxar::kitfile::Params& p)
{
    std::vector<uint8_t> f (elxar::kitfile::kFileBytes, 0);
    for (int i = 0; i < 8 && name8[i]; ++i) f[(size_t) i] = (uint8_t) name8[i];
    for (size_t i = 0; i < p.size(); ++i) f[8 + i] = p[i];
    return f;
}

static elxar::kitfile::Params initKit()
{
    elxar::kitfile::Params p {};
    for (int i = 0; i < ELXAR_NUM_KIT_PARAMS; ++i) p[(size_t) i] = elxar_init_kit_value (i);
    return p;
}

// Plays one track of the loaded kit alone and returns its peak level over about half a second.
// Returns -1 if anything was not a number.
static float playTrack (int track)
{
    const int voiceOfTrack = track > 5 ? 5 : track;
    for (int v = 0; v < ELXAR_NUM_VOICES; ++v) elxar_set_fader ((uint8_t) v, v == voiceOfTrack ? 1.0f : 0.0f);
    float l[ELXAR_BLOCK], r[ELXAR_BLOCK];
    for (int b = 0; b < 40; ++b) elxar_render_block (l, r);      // lets the faders settle and earlier tails die away in the others
    elxar_trigger ((uint8_t) track, 110, ELXAR_DEFAULT_NOTE);
    float peak = 0;
    for (int b = 0; b < 330; ++b)
    {
        elxar_render_block (l, r);
        for (int i = 0; i < ELXAR_BLOCK; ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) return -1.0f;
            peak = std::fmax (peak, std::fmax (std::fabs (l[i]), std::fabs (r[i])));
        }
    }
    return peak;
}

static void loadIntoEngine (const elxar::kitfile::Params& k)
{
    for (int i = 0; i < ELXAR_NUM_KIT_PARAMS; ++i) elxar_set_param ((uint16_t) i, k[(size_t) i]);
    elxar_silence_fx();
}

int main (int argc, char** argv)
{
    const int count = argc > 1 ? std::atoi (argv[1]) : 150;
    elxar_init();
    const int numTargets = elxar_num_mod_targets();
    using namespace elxar;

    // ---------------------------------------------------------------- the file layout
    check (kitfile::kFileBytes == 255 && ELXAR_NUM_KIT_PARAMS == 247, "a kit file is 255 bytes: 8 name bytes + 247 parameters");
    {
        const auto init = initKit();
        auto f = makeFile ("Initkit", init);
        const auto r = kitfile::parse (f.data(), f.size(), numTargets);
        check (r.ok && r.name == "Initkit" && r.params == init && r.note.empty() && r.adjusted == 0, "the init kit as a file reads back unchanged, name Initkit");
    }
    {
        // the firmware's own init kit array is 254 bytes (name + 246 values): the last value is missing and reads as 0
        std::vector<uint8_t> f (254, 0);
        for (int i = 0; i < 8; ++i) f[(size_t) i] = (uint8_t) "Initkit\0"[i];
        for (int i = 0; i < 246; ++i) f[(size_t) (8 + i)] = elxar_init_kit_value (i);
        const auto r = kitfile::parse (f.data(), f.size(), numTargets);
        check (r.ok && ! r.note.empty() && r.params[246] == 0 && r.params[10] == elxar_init_kit_value (10), "a short file loads, the missing values become 0, and it says so");
    }
    {
        std::vector<uint8_t> tiny (8, 'A'), huge (3663, 1), pat (4000, 2);
        check (! kitfile::parse (tiny.data(), tiny.size(), numTargets).ok && ! kitfile::parse (huge.data(), huge.size(), numTargets).ok
               && ! kitfile::parse (pat.data(), pat.size(), numTargets).ok && ! kitfile::parse (tiny.data(), 0, numTargets).ok,
               "files of the wrong size (8, 0, 3663 and 4000 bytes) are refused");
        check (! kitfile::parse (huge.data(), huge.size(), numTargets).error.empty(), "a refusal comes with a plain-words reason");
    }
    {
        const uint8_t raw[8] = { 'K', 'i', 't', 0x01, 0xFF, 'x', 0, 0 };
        const uint8_t spaced[8] = { 'H', 'i', ' ', ' ', ' ', ' ', ' ', ' ' };
        const uint8_t blank[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        const uint8_t full[8] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H' };
        check (kitfile::nameFromBytes (raw) == "Kit__x" && kitfile::nameFromBytes (spaced) == "Hi" && kitfile::nameFromBytes (blank).empty()
               && kitfile::nameFromBytes (full) == "ABCDEFGH", "names: stops at a zero byte, odd bytes become _, trailing spaces cut, 8 letters fit");
    }
    {
        auto p = initKit();
        for (auto& b : p) b = 255;
        auto f = makeFile ("Wild", p);
        const auto r = kitfile::parse (f.data(), f.size(), numTargets);
        bool inRange = r.ok && r.adjusted > 100;
        for (auto b : r.params) inRange = inRange && b <= 127;
        for (int v = 0; v < 6; ++v)
        {
            inRange = inRange && r.params[(size_t) (PAR_VEL_DEST_1 + v)] == 0 && r.params[(size_t) (PAR_TARGET_LFO1 + v)] == 0   // 255 is past the end of the target list
                      && r.params[(size_t) (PAR_AUDIO_OUT1 + v)] == 0;
            for (const auto& i : voiceParams (v))
                if (i.kind == ParamKind::choice) inRange = inRange && r.params[(size_t) i.par] == i.numChoices - 1;
                else if (i.kind == ParamKind::onOff) inRange = inRange && r.params[(size_t) i.par] == 1;
        }
        inRange = inRange && r.params[PAR_FX_TYPE] == 5;
        check (inRange, "values past the end of a list, 7 bits or the target list are brought back into range, and counted");
        loadIntoEngine (r.params);
        check (playTrack (0) >= 0.0f && playTrack (3) >= 0.0f, "the engine plays such a kit without a problem (finite output)");
    }
    {
        auto p = initKit();
        p[PAR_VEL_DEST_1] = (uint8_t) (numTargets - 1);   // 228: the last target; above 127, so it must NOT be cut to 127
        p[PAR_TARGET_LFO3] = (uint8_t) numTargets;
        p[PAR_TARGET_LFO4] = 200;
        p[PAR_AUDIO_OUT2] = 6;
        p[PAR_AUDIO_OUT3] = 7;
        auto f = makeFile ("Targets", p);
        const auto r = kitfile::parse (f.data(), f.size(), numTargets);
        check (r.params[PAR_VEL_DEST_1] == numTargets - 1 && r.params[PAR_TARGET_LFO4] == 200 && r.params[PAR_TARGET_LFO3] == 0 && r.params[PAR_AUDIO_OUT2] == 6 && r.params[PAR_AUDIO_OUT3] == 0,
              "targets above 127 stay (up to 228); one past the end becomes 0; routing 6 stays, 7 becomes 0");
    }

    // ---------------------------------------------------------------- Random kit
    {
        const auto init = initKit();
        const auto a = randomkit::make (1234, 0.0f, numTargets);
        check (a.params == init, "Random kit with wild = 0 gives the init kit");
        const auto b1 = randomkit::make (99, 0.6f, numTargets), b2 = randomkit::make (99, 0.6f, numTargets), b3 = randomkit::make (100, 0.6f, numTargets);
        check (b1.params == b2.params && b1.name == b2.name && b1.params != b3.params, "the same seed gives the same kit and name, another seed another kit");
        auto nameOk = [] (const std::string& n) { if (n.size() < 6 || n.size() > 8) return false; for (char c : n) if (c < 'A' || c > 'Z') return false; return true; };
        std::set<std::string> names;
        bool allNames = true, allInRange = true, keptFixed = true;
        for (uint32_t s = 1; s <= 2000; ++s)
        {
            const auto k = randomkit::make (s, (float) (s % 11) / 10.0f, numTargets);
            allNames = allNames && nameOk (k.name);
            names.insert (k.name);
            auto copy = k.params;
            allInRange = allInRange && kitfile::sanitise (copy, numTargets) == 0;
            for (int v = 0; v < 6; ++v)   // volume, pan, routing, LFO and velocity targets, effect: from the init kit
                keptFixed = keptFixed && k.params[(size_t) (PAR_VOL1 + v)] == init[(size_t) (PAR_VOL1 + v)] && k.params[(size_t) (PAR_AUDIO_OUT1 + v)] == 0
                            && k.params[(size_t) (PAR_TARGET_LFO1 + v)] == 0 && k.params[(size_t) (PAR_AMOUNT_LFO1 + v)] == 0 && k.params[(size_t) (PAR_VEL_DEST_1 + v)] == 0
                            && k.params[(size_t) elxar::panParam (v)] == init[(size_t) elxar::panParam (v)];
            keptFixed = keptFixed && k.params[PAR_FX_TYPE] == 0;
        }
        check (allNames, "2000 random kits: every name is 6 to 8 capital letters");
        check (names.size() >= 100, "names vary (" + std::to_string (names.size()) + " different names in 2000 kits)");
        check (allInRange, "2000 random kits: every value is already in range");
        check (keptFixed, "volume, pan, routing, effect, LFOs and velocity targets are left as in the init kit");
    }

    // The sound: render every voice of many random kits.
    const float wilds[] = { 0.3f, 0.6f, 1.0f };
    float initPeak[ELXAR_NUM_TRACKS] = {};
    loadIntoEngine (initKit());
    for (int t = 0; t < ELXAR_NUM_TRACKS; ++t) initPeak[t] = playTrack (t);
    std::printf ("init kit peaks per track:");
    for (int t = 0; t < ELXAR_NUM_TRACKS; ++t) std::printf (" %.2f", initPeak[t]);
    std::printf ("\n");
    for (float wild : wilds)
    {
        int rendered = 0, notFinite = 0, silent = 0, clipping = 0, quiet = 0;
        float lowest = 9, highest = 0;
        for (int i = 0; i < count; ++i)
        {
            const auto k = randomkit::make ((uint32_t) (7000 + i), wild, numTargets);
            loadIntoEngine (k.params);
            for (int t = 0; t < ELXAR_NUM_TRACKS; ++t)
            {
                const float p = playTrack (t);
                ++rendered;
                if (p < 0) { ++notFinite; continue; }
                if (p < 0.01f) ++silent;
                else if (p < 0.05f) ++quiet;
                if (p > 1.0f) ++clipping;
                lowest = std::fmin (lowest, p);
                highest = std::fmax (highest, p);
            }
        }
        std::printf ("wild %.1f: %d voice renders, not finite %d, silent (peak < 0.01) %d, quiet (< 0.05) %d, clipping (> 1.0) %d, lowest peak %.3f, highest %.3f\n",
                     (double) wild, rendered, notFinite, silent, quiet, clipping, (double) lowest, (double) highest);
        char label[160];
        std::snprintf (label, sizeof label, "wild %.1f: nothing broken, at most 0.5%% of voices silent, none clipping", (double) wild);
        check (notFinite == 0 && silent * 200 <= rendered && clipping == 0, label);
    }

    std::printf (failures == 0 ? "RESULT: all kit checks passed\n" : "RESULT: %d kit check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
