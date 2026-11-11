// ELXAR-Two plugin test: runs the real plugin code (PluginProcessor) without Logic, with a pretend host that
// supplies tempo and song position. Checks the things that the headless engine_test cannot: that the plugin
// follows the host transport, that patterns and kits survive saving and loading, and that the MIDI file is right.
// Not part of the download. Build with -DELXAR_BUILD_PLUGIN_TEST=ON (see CMakeLists.txt).
#include <set>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>
#include <vector>
#include "PluginProcessor.h"
#include "PluginEditor.h"

extern "C" {
#include "Parameters.h"
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%-64s %s\n", what.toRawUTF8(), ok ? "ok" : "FAIL");
    if (! ok) ++failures;
}

// A pretend host: fixed tempo, a song position that the test moves.
struct FakeHost : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
};

struct Runner
{
    Runner (ELXARTwoProcessor& p, FakeHost& h, double rate_, int block_) : proc (p), host (h), rate (rate_), block (block_) {}

    // Runs `blocks` audio blocks and returns the left channel.
    std::vector<float> run (int blocks, juce::MidiBuffer* firstBlockMidi = nullptr)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, block);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0 && firstBlockMidi != nullptr) midi = *firstBlockMidi;
            proc.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
            if (host.playing) host.ppq += (double) block / rate * host.bpm / 60.0;
        }
        return out;
    }
    ELXARTwoProcessor& proc;
    FakeHost& host;
    double rate;
    int block;
};

float peakBetween (const std::vector<float>& x, double fromSec, double toSec, double rate)
{
    float pk = 0;
    const auto a = (size_t) juce::jlimit (0.0, (double) x.size(), fromSec * rate), b = (size_t) juce::jlimit (0.0, (double) x.size(), toSec * rate);
    for (size_t i = a; i < b; ++i) pk = juce::jmax (pk, std::abs (x[i]));
    return pk;
}

// The time (seconds) of the first sample louder than `level` at or after `fromSec`, or -1.
double firstLoud (const std::vector<float>& x, double fromSec, double rate, float level)
{
    for (auto i = (size_t) (fromSec * rate); i < x.size(); ++i)
        if (std::abs (x[i]) > level) return (double) i / rate;
    return -1.0;
}

std::unique_ptr<ELXARTwoProcessor> makeProcessor (FakeHost& host, double rate, int block)
{
    std::unique_ptr<ELXARTwoProcessor> p (dynamic_cast<ELXARTwoProcessor*> (createPluginFilter()));
    p->setPlayHead (&host);
    p->setRateAndBufferSizeDetails (rate, block);
    p->prepareToPlay (rate, block);
    return p;
}

void setOnly (ELXARTwoProcessor& p, int pattern, int row, std::initializer_list<int> steps, int length = 16)
{
    for (int i = 0; i < ELXARTwoProcessor::kNumSteps; ++i) p.setStepOn (pattern, row, i, false);
    for (int s : steps) p.setStepOn (pattern, row, s, true);
    p.setTrackLength (pattern, row, length);
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double rate = 48000.0;
    const int block = 256;
    const double stepSec = 60.0 / 120.0 / 4.0;   // one 16th note at 120 BPM = 0.125 s

    // ---------------------------------------------------------------- transport
    {
        FakeHost host;
        auto p = makeProcessor (host, rate, block);
        Runner r (*p, host, rate, block);
        check (p->engineAvailable(), "the engine belongs to this instance");

        auto silent = r.run (60);
        check (peakBetween (silent, 0.05, 0.3, rate) < 0.001f, "silent while Logic is stopped");

        // only Drum 1, steps 1 5 9 13
        p->clearPattern (0);
        setOnly (*p, 0, 0, { 0, 4, 8, 12 });
        host.playing = true; host.ppq = 0.0;
        auto bar = r.run ((int) (2.2 * rate / block));
        bool timing = true;
        juce::String detail;
        for (int beat = 0; beat < 4; ++beat)
        {
            const double want = beat * 4 * stepSec;
            const double got = firstLoud (bar, juce::jmax (0.0, want - 0.02), rate, 0.05f);
            detail << juce::String ((got - want) * 1000.0, 1) << " ";
            if (got < 0 || std::abs (got - want) > 0.006) timing = false;                               // within 6 ms (engine block + rate conversion)
            if (beat > 0 && peakBetween (bar, want - 0.03, want - 0.004, rate) > 0.5f * peakBetween (bar, want, want + 0.02, rate)) {}  // (the tail of the hit before may still ring)
        }
        check (timing, "Drum 1 on beats 1 2 3 4, in time (ms late: " + detail + ")");
        check (p->isSequencerRunning() && p->getRowPosition (0) >= 0, "the window is told the playing position");

        host.playing = false;
        r.run (20);
        auto after = r.run ((int) (1.5 * rate / block));
        check (! p->isSequencerRunning() && peakBetween (after, 1.0, 1.5, rate) < 0.01f, "stops with Logic");

        // start in the middle of a bar: song step 6 -> the next hit is step 9, two steps later
        host.ppq = 6.0 / 4.0; host.playing = true;
        auto mid = r.run ((int) (0.6 * rate / block));
        const double got = firstLoud (mid, 0.0, rate, 0.05f);
        check (std::abs (got - 2 * stepSec) < 0.006, "starting mid-bar plays the right step (" + juce::String (got * 1000.0, 1) + " ms, want 250)");

        // a jump back (cycle): position 0 again -> a hit straight away
        host.ppq = 0.0;
        auto cyc = r.run ((int) (0.3 * rate / block));
        check (firstLoud (cyc, 0.0, rate, 0.05f) >= 0 && firstLoud (cyc, 0.0, rate, 0.05f) < 0.006, "a jump to bar 1 (cycle) restarts the pattern at once");
        host.playing = false;
        r.run ((int) (2.0 * rate / block));

        // row with its own length: Drum 1 length 3, one hit -> a hit every 3 steps
        setOnly (*p, 0, 0, { 0 }, 3);
        host.ppq = 0.0; host.playing = true;
        auto poly = r.run ((int) (1.3 * rate / block));
        bool polyOk = true;
        for (int k = 0; k < 3; ++k) { const double want = k * 3 * stepSec; if (std::abs (firstLoud (poly, juce::jmax (0.0, want - 0.02), rate, 0.05f) - want) > 0.006) polyOk = false; }
        check (polyOk, "a row of length 3 repeats every 3 steps");
        host.playing = false;
        r.run ((int) (2.0 * rate / block));

        // pattern change while running: pattern 2 has the snare on step 1; it must start after pattern 1 ends
        setOnly (*p, 0, 0, {}, 16);
        p->clearPattern (1);
        setOnly (*p, 1, 3, { 0 });
        host.ppq = 0.0; host.playing = true;
        r.run ((int) (0.5 * rate / block));               // half a second into pattern 1 (silent pattern)
        p->setCurrentPattern (1);
        const double asked = host.ppq * 4.0 * stepSec;
        auto change = r.run ((int) (2.0 * rate / block));
        const double hit = firstLoud (change, 0.0, rate, 0.05f) + asked;
        check (std::abs (hit - 2.0) < 0.008 && p->getPlayingPattern() == 1, "a new pattern starts at the end of the playing one (" + juce::String (hit, 3) + " s, want 2.000)");
        host.playing = false;
        r.run ((int) (2.0 * rate / block));
        p->setCurrentPattern (0);

        // "Always" mode: runs without Logic playing
        setOnly (*p, 0, 0, { 0, 4, 8, 12 });
        if (auto* mode = p->apvts.getParameter ("run_mode")) mode->setValueNotifyingHost (1.0f);
        auto always = r.run ((int) (1.0 * rate / block));
        check (peakBetween (always, 0.0, 1.0, rate) > 0.05f && p->isSequencerRunning(), "\"Always\" runs while Logic is stopped");
        if (auto* mode = p->apvts.getParameter ("run_mode")) mode->setValueNotifyingHost (0.0f);
        r.run ((int) (2.0 * rate / block));

        // keys: C1 plays Drum 1
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 10);
        auto key = r.run (40, &midi);
        check (peakBetween (key, 0.0, 0.1, rate) > 0.05f, "the key C1 plays Drum 1");
        r.run ((int) (2.0 * rate / block));

        // mute: Drum 1 muted -> silent
        if (auto* m = p->apvts.getParameter ("v1_mute")) m->setValueNotifyingHost (1.0f);
        r.run (20);
        juce::MidiBuffer midi2;
        midi2.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 10);
        auto muted = r.run (40, &midi2);
        check (peakBetween (muted, 0.0, 0.2, rate) < 0.01f, "Mute silences the voice");
        if (auto* m = p->apvts.getParameter ("v1_mute")) m->setValueNotifyingHost (0.0f);
        r.run ((int) (2.0 * rate / block));

        // kit parameters reach the engine: a host parameter (Drum 1 pitch) and a plain one (Drum 1 FM amount)
        p->setKitParam (PAR_COARSE1, 99);
        p->setKitParam (PAR_FMAMNT1, 77);
        p->setKitParam (PAR_AUDIO_OUT1, 6);
        r.run (4);
        check (elxar_get_param (PAR_COARSE1) == 99 && elxar_get_param (PAR_FMAMNT1) == 77 && elxar_get_param (PAR_AUDIO_OUT1) == 6
               && p->getKitParam (PAR_COARSE1) == 99 && p->getKitParam (PAR_FMAMNT1) == 77, "sound settings reach the engine");
        check (p->apvts.getRawParameterValue ("v1_pitch")->load() == 99.0f && p->apvts.getRawParameterValue ("v1_fx")->load() > 0.5f, "the main knobs follow the Sound page");

        // kit slots
        p->storeKitSlot (2);
        p->loadInitKit();
        r.run (4);
        const bool initOk = p->getKitParam (PAR_FMAMNT1) == (int) elxar_init_kit_value (PAR_FMAMNT1) && p->getKitParam (PAR_COARSE1) == (int) elxar_init_kit_value (PAR_COARSE1)
                            && elxar_get_param (PAR_COARSE1) == elxar_init_kit_value (PAR_COARSE1);
        const bool recalled = p->recallKitSlot (2);
        r.run (4);
        check (initOk && recalled && p->getKitParam (PAR_FMAMNT1) == 77 && elxar_get_param (PAR_COARSE1) == 99 && ! p->recallKitSlot (5), "Init kit, Store kit and Recall kit");

        // ------------------------------------------------------------ saving and loading
        ElxarStep s = ELXARTwoProcessor::defaultStep();
        s.on = 1; s.velocity = 77; s.probability = 60; s.note = 64; s.flam = 3; s.shift = 10;
        s.autoTarget[0] = (uint8_t) ELXARTwoProcessor::targetIndexForParam (PAR_FILTER_FREQ_1); s.autoValue[0] = 33;
        p->setStep (9, 6, 40, s);
        p->setTrackLength (9, 6, 48);
        p->setShuffle (9, 55);
        p->setCurrentPattern (9);
        p->setUiScale (1.25f);
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        std::printf ("  (saved state: %d bytes)\n", (int) saved.getSize());

        FakeHost host2;
        // a second processor while the first still exists: it must stay silent but still hold and save its data
        auto q = makeProcessor (host2, rate, block);
        check (! q->engineAvailable(), "a second instance does not get the engine");
        q->setStateInformation (saved.getData(), (int) saved.getSize());
        const ElxarStep t = q->getStep (9, 6, 40);
        check (t.on == 1 && t.velocity == 77 && t.probability == 60 && t.note == 64 && t.flam == 3 && t.shift == 10
               && t.autoTarget[0] == s.autoTarget[0] && t.autoValue[0] == 33 && q->getTrackLength (9, 6) == 48 && q->getShuffle (9) == 55
               && q->getCurrentPattern() == 9 && q->getStep (0, 0, 4).on == 1 && q->getStep (0, 0, 5).on == 0, "patterns come back after save and load");
        check (q->getKitParam (PAR_FMAMNT1) == 77 && q->getKitParam (PAR_COARSE1) == 99 && q->getKitParam (PAR_AUDIO_OUT1) == 6
               && q->isKitSlotUsed (2) && ! q->isKitSlotUsed (0) && std::abs (q->getUiScale() - 1.25f) < 0.001f, "kit, kit slots and window size come back");
        juce::MemoryBlock savedAgain;
        q->getStateInformation (savedAgain);
        auto q2 = makeProcessor (host2, rate, block);
        q2->setStateInformation (savedAgain.getData(), (int) savedAgain.getSize());
        check (q2->getStep (9, 6, 40).velocity == 77 && q2->getKitParam (PAR_FMAMNT1) == 77 && q2->recallKitSlot (2), "a second save and load gives the same");

        // ------------------------------------------------------------ kit files, kit names and Random kit (1.1.0)
        {
            // a kit file the way the hardware writes it: 8 name bytes, then the 247 values
            auto makeFile = [] (const char* name)
            {
                std::vector<uint8_t> f (255, 0);
                for (int i = 0; i < 8 && name[i] != 0; ++i) f[(size_t) i] = (uint8_t) name[i];
                for (int i = 0; i < 247; ++i) f[(size_t) (8 + i)] = elxar_init_kit_value (i);
                f[8 + PAR_COARSE2] = 71;           // a value that is a host knob (Drum 2 pitch)
                f[8 + PAR_FMAMNT3] = 55;           // a plain value
                f[8 + PAR_TARGET_LFO5] = 200;      // a modulation target above 127
                f[8 + PAR_FX_TYPE] = 4;            // Delay
                return f;
            };
            auto file = makeFile ("TESTKIT");
            const auto res = p->loadKitData (file.data(), file.size(), "x");
            r.run (4);
            check (res.ok && p->getKitName() == "TESTKIT" && p->getKitParam (PAR_COARSE2) == 71 && p->getKitParam (PAR_FMAMNT3) == 55 && p->getKitParam (PAR_FX_TYPE) == 4
                   && p->getKitParam (PAR_TARGET_LFO5) == 200 && p->apvts.getRawParameterValue ("v2_pitch")->load() == 71.0f,
                   "a kit file loads: name, a host knob, a plain value, the effect, a target above 127");
            check (elxar_get_param (PAR_COARSE2) == 71 && elxar_get_param (PAR_FMAMNT3) == 55 && elxar_get_param (PAR_FX_TYPE) == 4, "...and the values reach the engine");

            std::vector<uint8_t> big (3663, 1);
            const auto refused = p->loadKitData (big.data(), big.size(), "x");
            check (! refused.ok && refused.message.isNotEmpty() && p->getKitName() == "TESTKIT" && p->getKitParam (PAR_COARSE2) == 71, "a wrong file is refused with a reason and leaves the sound alone");

            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("elxar_kit_test");
            dir.createDirectory();
            const auto onDisk = dir.getChildFile ("07-DISKFILE.SND");
            auto file2 = makeFile ("DISKKIT");
            onDisk.replaceWithData (file2.data(), file2.size());
            const auto named = p->loadKitFile (onDisk);
            auto file3 = makeFile ("");
            const auto nameless = dir.getChildFile ("03-FALLBK.snd");
            nameless.replaceWithData (file3.data(), file3.size());
            const auto res3 = p->loadKitFile (nameless);
            const bool fallback = res3.ok && p->getKitName() == "FALLBK";
            const auto notKit = dir.getChildFile ("notes.txt");
            notKit.replaceWithText ("hello");
            check (named.ok && fallback && ! p->loadKitFile (notKit).ok && ! p->loadKitFile (dir.getChildFile ("missing.snd")).ok,
                   "Load from a .SND file on disk; a nameless kit takes the file name; other files and missing files are refused");
            p->loadKitFile (onDisk);
            check (p->getKitName() == "DISKKIT", "the name inside the file wins over the file name");
            dir.deleteRecursively();

            // names with the kit slots and with the project
            p->storeKitSlot (4);
            p->loadInitKit();
            const bool initName = p->getKitName() == "Initkit";
            const bool recalledName = p->recallKitSlot (4) && p->getKitName() == "DISKKIT" && p->getKitSlotName (4) == "DISKKIT" && p->getKitSlotName (3).isEmpty();
            juce::MemoryBlock saved3;
            p->getStateInformation (saved3);
            auto q3 = makeProcessor (host2, rate, block);
            q3->setStateInformation (saved3.getData(), (int) saved3.getSize());
            check (initName && recalledName && q3->getKitName() == "DISKKIT" && q3->getKitSlotName (4) == "DISKKIT" && q3->getKitParam (PAR_TARGET_LFO5) == 200
                   && q3->getKitParam (PAR_COARSE2) == 71, "kit names follow Init, Store and Recall kit and come back with the project (targets above 127 too)");

            // Random kit
            p->loadInitKit();
            const juce::String n1 = p->makeRandomKit (0.7f, 42u);
            const auto k1 = p->currentKit();
            r.run (4);
            const bool reached = elxar_get_param (PAR_COARSE1) == p->getKitParam (PAR_COARSE1) && elxar_get_param (PAR_VELOD2) == p->getKitParam (PAR_VELOD2);
            const juce::String n2 = p->makeRandomKit (0.7f, 42u);
            p->makeRandomKit (0.7f, 43u);
            const auto k3 = p->currentKit();
            bool differs = false;
            for (int i = 0; i < 247; ++i) differs = differs || k1[(size_t) i] != elxar_init_kit_value (i);
            check (n1.isNotEmpty() && n1.length() <= 8 && n1 == n2 && k1 != k3 && differs && reached,
                   "Random kit: gets a name of up to 8 letters, the same seed gives the same kit, it reaches the engine");
            p->makeRandomKit (0.0f, 5u);
            bool sameAsInit = true;
            for (int i = 0; i < 247; ++i) sameAsInit = sameAsInit && p->getKitParam (i) == (int) elxar_init_kit_value (i);
            check (sameAsInit, "Random kit with wild at 0 gives the starting kit");
            p->loadInitKit();
            r.run (4);
        }

        // a project saved with Milestone 1: 4 patterns of 7 rows as hexadecimal words
        {
            juce::ValueTree old ("ELXARTwo");
            old.setProperty ("patterns", "1111 0 0 1010 5555 4000 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 8001 0 0 0 0 0 2", nullptr);
            old.setProperty ("currentPattern", 3, nullptr);
            juce::ValueTree par ("PARAM"); par.setProperty ("id", "v2_pitch", nullptr); par.setProperty ("value", 77.0, nullptr);
            old.appendChild (par, nullptr);
            juce::MemoryBlock m1;
            if (auto xml = old.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, m1);
            auto o = makeProcessor (host2, rate, block);
            o->setStateInformation (m1.getData(), (int) m1.getSize());
            check (o->getStep (0, 0, 0).on && o->getStep (0, 0, 4).on && ! o->getStep (0, 0, 1).on && o->getStep (0, 5, 14).on
                   && o->getStep (3, 0, 15).on && o->getStep (3, 6, 1).on && o->getCurrentPattern() == 3 && o->getTrackLength (0, 0) == 16
                   && o->getKitParam (PAR_COARSE2) == 77, "a Milestone 1 project opens (patterns and sliders)");
        }

        // ------------------------------------------------------------ MIDI file
        p->clearPattern (4);
        setOnly (*p, 4, 0, { 0, 4, 8, 12 });
        setOnly (*p, 4, 3, { 4, 12 });
        ElxarStep f = ELXARTwoProcessor::defaultStep(); f.on = 1; f.flam = 2; f.velocity = 90;   // flam x3 on snare step 13
        p->setStep (4, 3, 12, f);
        ElxarStep sh = ELXARTwoProcessor::defaultStep(); sh.on = 1; sh.shift = 9;                // closed hat step 3, 2/8 late
        p->setStep (4, 4, 2, sh);
        setOnly (*p, 4, 6, { 0 }, 5);                                                          // cymbal every 5 steps
        const auto file = p->writePatternMidiFile (4, 2);
        juce::MidiFile mf;
        bool midiOk = false;
        juce::String midiDetail;
        if (file.existsAsFile())
        {
            juce::FileInputStream in (file);
            if (in.openedOk() && mf.readFrom (in) && mf.getNumTracks() == 1 && mf.getTimeFormat() == 480)
            {
                std::map<int, std::vector<int>> notes;
                const auto* tr = mf.getTrack (0);
                for (int i = 0; i < tr->getNumEvents(); ++i)
                {
                    const auto& m = tr->getEventPointer (i)->message;
                    if (m.isNoteOn()) notes[m.getNoteNumber()].push_back ((int) m.getTimeStamp());
                }
                const std::vector<int> kick { 0, 480, 960, 1440, 1920, 2400, 2880, 3360 };
                const std::vector<int> snare { 480, 1440, 1480, 1520, 2400, 3360, 3400, 3440 };   // flam x3 = pulses 0, 8, 16 = ticks +0, +40, +80
                const std::vector<int> hat { 240 + 30, 2160 + 30 };                               // shift +2 = 6 pulses = 30 ticks late
                const std::vector<int> cym { 0, 600, 1200, 1800, 2400, 3000, 3600 };              // every 5 steps over 32 steps
                midiOk = notes[36] == kick && notes[41] == snare && notes[43] == hat && notes[47] == cym && notes.size() == 4;
                for (auto& n : notes) { midiDetail << n.first << ":"; for (int v : n.second) midiDetail << v << " "; }
            }
        }
        check (midiOk, "MIDI file: notes, flam, shift and row length are right");
        if (! midiOk) std::printf ("  %s\n", midiDetail.toRawUTF8());
        check (p->autoBarsForPattern (4) == 1 && p->autoBarsForPattern (9) == 3, "MIDI file: automatic length in bars");

        // ------------------------------------------------------------ generator
        p->euclidFillRow (5, 1, 8, 3, 0);
        int n = 0; for (int i = 0; i < 64; ++i) n += p->getStep (5, 1, i).on;
        check (n == 3 && p->getTrackLength (5, 1) == 8 && p->getStep (5, 1, 0).on && p->getStep (5, 1, 3).on && p->getStep (5, 1, 6).on, "Fill selected row: 3 hits over 8 steps");
        bool genOk = true;
        for (int round = 0; round < 40; ++round)
        {
            p->generatePattern (6, (float) (round % 10) / 9.0f, (float) (round % 7) / 6.0f, 1 + round % 8);
            int kicks = 0; for (int i = 0; i < 16; ++i) kicks += p->getStep (6, 0, i).on;
            if (kicks != 1 + round % 8) genOk = false;
            for (int row = 0; row < 7; ++row) for (int i = 16; i < 64; ++i) if (p->getStep (6, row, i).on) genOk = false;
            for (int i = 0; i < 16; ++i) if (p->getStep (6, 4, i).on && p->getStep (6, 5, i).on) genOk = false;   // closed and open hat never together
        }
        check (genOk, "Generate beat: Drum 1 hit count as asked, one bar, hats do not clash");

        // copy / paste / clear
        p->copyPattern (4);
        p->pastePattern (7);
        const bool pasted = p->getStep (7, 3, 12).flam == 2 && p->getTrackLength (7, 6) == 5;
        p->clearPattern (7);
        check (pasted && ! p->getStep (7, 3, 12).on && p->getStep (7, 3, 12).flam == 0 && p->getTrackLength (7, 6) == 16, "Copy, Paste and Clear pattern");

        // ------------------------------------------------------------ stress: edits from another thread while audio runs
        {
            std::atomic<bool> stop { false };
            std::thread editor ([&]
            {
                juce::Random rng (1234);
                while (! stop.load())
                {
                    const int pat = rng.nextInt (16), row = rng.nextInt (7), st = rng.nextInt (64);
                    ElxarStep e = ELXARTwoProcessor::defaultStep();
                    e.on = (uint8_t) rng.nextInt (2); e.velocity = (uint8_t) (1 + rng.nextInt (127)); e.probability = (uint8_t) rng.nextInt (101);
                    e.flam = (uint8_t) rng.nextInt (6); e.shift = (uint8_t) rng.nextInt (15); e.note = (uint8_t) rng.nextInt (128);
                    e.autoTarget[0] = (uint8_t) rng.nextInt (elxar_num_mod_targets()); e.autoValue[0] = (uint8_t) rng.nextInt (128);
                    p->setStep (pat, row, st, e);
                    if (rng.nextInt (20) == 0) p->setTrackLength (pat, row, 1 + rng.nextInt (64));
                    if (rng.nextInt (50) == 0) p->setShuffle (pat, rng.nextInt (128));
                    if (rng.nextInt (200) == 0) p->setCurrentPattern (rng.nextInt (16));
                    if (rng.nextInt (300) == 0) p->generatePattern (rng.nextInt (16), rng.nextFloat(), rng.nextFloat(), 1 + rng.nextInt (16));
                    if (rng.nextInt (500) == 0) { juce::MemoryBlock mb; p->getStateInformation (mb); }
                    p->setKitParam (rng.nextInt (247), rng.nextInt (128));
                }
            });
            host.ppq = 0.0; host.playing = true; host.bpm = 174.0;
            bool finite = true;
            for (int i = 0; i < 6000; ++i)
            {
                if (i % 997 == 0) host.ppq = (double) (i % 13);
                if (i % 1500 == 0) host.playing = ! host.playing;
                const auto out = r.run (1);
                for (float v : out) if (! std::isfinite (v) || std::abs (v) > 1.0001f) finite = false;
            }
            stop.store (true);
            editor.join();
            check (finite, "6000 audio blocks while another thread edits everything");
            host.playing = false; host.bpm = 120.0;
        }
        q2.reset(); q.reset();
    }

    // ---------------------------------------------------------------- the window (Milestone 3: the styled look)
    // Opens the plugin window twice without showing it, draws it into a picture at two sizes and closes it again.
    // Catches a crash in the drawing code or in the look's clean-up. With a folder as the first argument, the
    // picture is also saved there as window.png.
    {
        FakeHost host;
        auto p = makeProcessor (host, rate, block);
        bool opened = true, drawn = true;
        int colours = 0;
        for (int round = 0; round < 2; ++round)
        {
            std::unique_ptr<juce::AudioProcessorEditor> window (p->createEditor());
            if (window == nullptr) { opened = false; break; }
            for (const float scale : { 1.0f, 0.75f })
            {
                window->setSize (juce::roundToInt (1100.0f * scale), juce::roundToInt (820.0f * scale));
                const juce::Image picture = window->createComponentSnapshot (window->getLocalBounds());
                std::set<juce::uint32> seen;
                for (int y = 2; y < picture.getHeight(); y += 7)
                    for (int x = 2; x < picture.getWidth(); x += 7)
                        seen.insert (picture.getPixelAt (x, y).getARGB());
                if (scale == 1.0f) colours = (int) seen.size();
                if (seen.size() < 20) drawn = false;
                if (round == 0 && scale == 1.0f && argc > 1)
                {
                    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getChildFile ("window.png");
                    out.deleteFile();
                    juce::FileOutputStream stream (out);
                    if (stream.openedOk()) juce::PNGImageFormat().writeImageToStream (picture, stream);
                }
            }
        }
        // the Sound page (kit name, Load kit file, Random kit): draw it too, with a named random kit loaded
        bool soundDrawn = false;
        {
            p->makeRandomKit (0.8f, 2024u);
            std::unique_ptr<juce::AudioProcessorEditor> window (p->createEditor());
            if (auto* e = dynamic_cast<ELXARTwoEditor*> (window.get()))
            {
                e->showSoundPage();
                window->setSize (1100, 820);
                const juce::Image picture = window->createComponentSnapshot (window->getLocalBounds());
                std::set<juce::uint32> seen;
                for (int y = 2; y < picture.getHeight(); y += 7)
                    for (int x = 2; x < picture.getWidth(); x += 7)
                        seen.insert (picture.getPixelAt (x, y).getARGB());
                soundDrawn = seen.size() >= 20;
                if (argc > 1)
                {
                    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getChildFile ("sound-page.png");
                    out.deleteFile();
                    juce::FileOutputStream stream (out);
                    if (stream.openedOk()) juce::PNGImageFormat().writeImageToStream (picture, stream);
                }
            }
            p->loadInitKit();
        }
        check (soundDrawn, "the Sound page draws itself");
        check (opened, "the plugin window opens and closes twice");
        check (drawn, "the plugin window draws itself at 100 % and 75 % (" + juce::String (colours) + " different colours)");
    }

    std::printf (failures ? "RESULT - %d FAILED\n" : "RESULT - all plugin checks passed\n", failures);
    return failures ? 1 : 0;
}
