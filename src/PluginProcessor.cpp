#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <limits>

extern "C" {
#include "Parameters.h"
#include "Preset/InitKit.h"
}

// What the screen shows, and how it maps onto the engine.
const std::array<const char*, ELXARTwoProcessor::kNumStrips> ELXARTwoProcessor::kStripNames = { "Drum 1", "Drum 2", "Drum 3", "Snare", "Hi-hat", "Cymbal" };
const std::array<int, ELXARTwoProcessor::kNumStrips> ELXARTwoProcessor::kStripToVoice = { 0, 1, 2, 3, 5, 4 };
const std::array<const char*, ELXARTwoProcessor::kNumRows> ELXARTwoProcessor::kRowNames = { "Drum 1", "Drum 2", "Drum 3", "Snare", "Hat closed", "Hat open", "Cymbal" };
const std::array<int, ELXARTwoProcessor::kNumRows> ELXARTwoProcessor::kRowToTrack = { 0, 1, 2, 3, 5, 6, 4 };
const std::array<int, ELXARTwoProcessor::kNumRows> ELXARTwoProcessor::kRowToStrip = { 0, 1, 2, 3, 4, 4, 5 };
const std::array<int, ELXARTwoProcessor::kNumRows> ELXARTwoProcessor::kRowNotes = { 36, 38, 40, 41, 43, 45, 47 };   // C1 D1 E1 F1 G1 A1 B1 (Logic naming)

namespace
{
int panParam (int voice) { return voice < 3 ? PAR_PAN1 + voice : PAR_PAN4 + (voice - 3); }

// The value the firmware's built-in "init kit" has for a parameter, so the sliders start where the engine starts.
float initKitValue (int par)
{
    const int i = par + 8;   // the kit data starts after an 8-character name
    return i < INIT_KIT_SIZE ? (float) initKit[i] : 0.0f;
}

juce::String sid (int strip, const char* what) { return "v" + juce::String (strip + 1) + "_" + what; }
}

juce::AudioProcessorValueTreeState::ParameterLayout ELXARTwoProcessor::createLayout()
{
    using FloatP = juce::AudioParameterFloat;
    using BoolP = juce::AudioParameterBool;
    using ChoiceP = juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const juce::NormalisableRange<float> r127 (0.0f, 127.0f, 1.0f);

    for (int s = 0; s < kNumStrips; ++s)
    {
        const int v = kStripToVoice[(size_t) s];
        const juce::String n (kStripNames[(size_t) s]);
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "pitch"), 1), n + " Pitch", r127, initKitValue (PAR_COARSE1 + 2 * v)));
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "decay"), 1), n + " Decay", r127, initKitValue (PAR_VELOD1 + 2 * v)));
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "tone"), 1), n + " Tone", r127, initKitValue (PAR_FILTER_FREQ_1 + v)));
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "level"), 1), n + " Level", r127, initKitValue (PAR_VOL1 + v)));
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "pan"), 1), n + " Pan", r127, initKitValue (panParam (v))));
        layout.add (std::make_unique<BoolP> (juce::ParameterID (sid (s, "mute"), 1), n + " Mute", false));
        layout.add (std::make_unique<BoolP> (juce::ParameterID (sid (s, "solo"), 1), n + " Solo", false));
        layout.add (std::make_unique<BoolP> (juce::ParameterID (sid (s, "fx"), 1), n + " To effect", false));
    }
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("hat_open_decay", 1), "Hi-hat Open decay", r127, initKitValue (PAR_VELOD6_OPEN)));

    layout.add (std::make_unique<ChoiceP> (juce::ParameterID ("fx_type", 1), "Effect type",
                                           juce::StringArray { "Off", "Drive", "Ring mod", "Compressor", "Delay", "Reverb" }, 0));
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("fx_mix", 1), "Effect dry/wet", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f));
    for (int i = 1; i <= 4; ++i)
        layout.add (std::make_unique<FloatP> (juce::ParameterID ("fx_p" + juce::String (i), 1), "Effect knob " + juce::String (i), r127, 64.0f));

    layout.add (std::make_unique<FloatP> (juce::ParameterID ("master", 1), "Master level", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.7f));
    layout.add (std::make_unique<ChoiceP> (juce::ParameterID ("run_mode", 1), "Step grid runs",
                                           juce::StringArray { "With Logic transport", "Always" }, 0));
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("gen_density", 1), "Generator density", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f));
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("gen_variation", 1), "Generator variation", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f));
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("gen_euclid", 1), "Generator Euclid hits", juce::NormalisableRange<float> (1.0f, 16.0f, 1.0f), 4.0f));
    return layout;
}

ELXARTwoProcessor::ELXARTwoProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ELXARTwo", createLayout())
{
    auto link = [this] (const juce::String& id, int enginePar, float scale = 1.0f)
    {
        links.push_back ({ apvts.getRawParameterValue (id), enginePar, scale, -1 });
    };
    for (int s = 0; s < kNumStrips; ++s)
    {
        const int v = kStripToVoice[(size_t) s];
        link (sid (s, "pitch"), PAR_COARSE1 + 2 * v);
        link (sid (s, "decay"), PAR_VELOD1 + 2 * v);
        link (sid (s, "tone"), PAR_FILTER_FREQ_1 + v);
        link (sid (s, "level"), PAR_VOL1 + v);
        link (sid (s, "pan"), panParam (v));
        muteParams[(size_t) s] = apvts.getRawParameterValue (sid (s, "mute"));
        soloParams[(size_t) s] = apvts.getRawParameterValue (sid (s, "solo"));
        fxParams[(size_t) s] = apvts.getRawParameterValue (sid (s, "fx"));
    }
    link ("hat_open_decay", PAR_VELOD6_OPEN);
    link ("fx_type", PAR_FX_TYPE);
    link ("fx_mix", PAR_FX_DRY_WET);
    link ("fx_p1", PAR_FX_P1);
    link ("fx_p2", PAR_FX_P2);
    link ("fx_p3", PAR_FX_P3);
    link ("fx_p4", PAR_FX_P4);
    masterParam = apvts.getRawParameterValue ("master");
    runModeParam = apvts.getRawParameterValue ("run_mode");
    lastFxRoute.fill (-1);

    // A simple starting beat in pattern 1 so there is something to hear straight away.
    for (int step : { 0, 4, 8, 12 }) setStep (0, 0, step, true);          // Drum 1 on every beat
    for (int step : { 4, 12 }) setStep (0, 3, step, true);                // Snare on 2 and 4
    for (int step = 0; step < kNumSteps; step += 2) setStep (0, 4, step, true);   // closed hat on 8th notes
    setStep (0, 5, 14, true);                                             // one open hat
}

void ELXARTwoProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 44100.0;
    engine.prepare (sampleRateHz);
    for (auto& l : links) l.lastSent = -1;   // send every setting to the engine again
    lastFxRoute.fill (-1);
    lastBpmSentToEngine = 0.0;
    expectedHostSteps = -1.0e9;
    lastTriggeredStep = -1;
    lastMaster = masterParam->load();
}

bool ELXARTwoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ELXARTwoProcessor::pushParametersToEngine()
{
    for (auto& l : links)
    {
        const int v = juce::roundToInt (l.value->load() * l.scale);
        if (v != l.lastSent)
        {
            engine.setParam (l.enginePar, v);
            l.lastSent = v;
        }
    }

    bool anySolo = false;
    for (int s = 0; s < kNumStrips; ++s)
        anySolo = anySolo || soloParams[(size_t) s]->load() > 0.5f;

    for (int s = 0; s < kNumStrips; ++s)
    {
        const int v = kStripToVoice[(size_t) s];
        const bool muted = muteParams[(size_t) s]->load() > 0.5f;
        const bool solo = soloParams[(size_t) s]->load() > 0.5f;
        engine.setFader (v, (muted || (anySolo && ! solo)) ? 0.0f : 1.0f);   // the engine fades this smoothly itself

        // "To effect" on: the whole voice goes through the effect (engine output 6). Off: straight to the main output (0).
        const int route = fxParams[(size_t) s]->load() > 0.5f ? 6 : 0;
        if (route != lastFxRoute[(size_t) s])
        {
            engine.setParam (PAR_AUDIO_OUT1 + v, route);
            lastFxRoute[(size_t) s] = route;
        }
    }
}

void ELXARTwoProcessor::addEvent (int offset, int row, int velocity)
{
    if (numEvents < (int) events.size() && row >= 0 && row < kNumRows)
        events[(size_t) numEvents++] = { offset, row, velocity };
}

void ELXARTwoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (numSamples <= 0 || buffer.getNumChannels() < 1)
        return;

    // ---- tempo and transport from the host ----
    double bpm = 120.0, hostPpq = 0.0;
    bool playing = false, tempoKnown = false, ppqKnown = false;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            if (const auto b = pos->getBpm()) { bpm = *b; tempoKnown = true; }
            if (const auto q = pos->getPpqPosition()) { hostPpq = *q; ppqKnown = true; }
            playing = pos->getIsPlaying();
        }
    }
    if (! (bpm > 20.0 && bpm < 999.0)) bpm = 120.0;
    displayBpm.store ((float) bpm);
    hostPlaying.store (playing);
    hostTempoKnown.store (tempoKnown);
    if (bpm != lastBpmSentToEngine) { engine.setTempo (bpm); lastBpmSentToEngine = bpm; }

    pushParametersToEngine();

    // ---- collect everything that should make a sound in this block ----
    numEvents = 0;

    const uint32_t hits = pendingHits.exchange (0);   // "Hit" buttons in the window
    for (int row = 0; row < kNumRows; ++row)
        if (hits & (1u << (unsigned) row))
            addEvent (0, row, 110);

    for (const auto metadata : midi)   // notes played on the track
    {
        const auto msg = metadata.getMessage();
        if (! msg.isNoteOn()) continue;
        for (int row = 0; row < kNumRows; ++row)
            if (msg.getNoteNumber() == kRowNotes[(size_t) row])
                addEvent (juce::jlimit (0, numSamples - 1, metadata.samplePosition), row, juce::jlimit (1, 127, (int) msg.getVelocity()));
    }
    midi.clear();

    // ---- the step grid: 16 steps, one step is a 16th note ----
    const bool always = runModeParam->load() > 0.5f;
    const double stepsPerSample = bpm * 4.0 / (60.0 * sampleRateHz);
    const int pattern = currentPattern.load();
    bool gridRunning = false;
    double startSteps = 0.0;
    if (always)
    {
        gridRunning = true;
        startSteps = freeRunSteps;
        freeRunSteps += stepsPerSample * numSamples;
        if (freeRunSteps > 1.0e9) freeRunSteps = std::fmod (freeRunSteps, 16.0);
    }
    else if (playing && ppqKnown)
    {
        gridRunning = true;
        startSteps = hostPpq * 4.0;
        if (std::abs (startSteps - expectedHostSteps) > 0.02)   // the song position jumped (start, cycle, locate)
            lastTriggeredStep = std::numeric_limits<long long>::min();
        expectedHostSteps = startSteps + stepsPerSample * numSamples;
    }
    else
    {
        expectedHostSteps = -1.0e9;
        freeRunSteps = 0.0;
        lastTriggeredStep = std::numeric_limits<long long>::min();
    }

    if (gridRunning)
    {
        const double endSteps = startSteps + stepsPerSample * numSamples;
        for (auto k = (long long) std::ceil (startSteps - 1.0e-6); (double) k < endSteps - 1.0e-6; ++k)
        {
            if (k == lastTriggeredStep) continue;
            lastTriggeredStep = k;
            const int step = (int) (((k % kNumSteps) + kNumSteps) % kNumSteps);
            const int offset = juce::jlimit (0, numSamples - 1, (int) (((double) k - startSteps) / stepsPerSample));
            for (int row = 0; row < kNumRows; ++row)
                if (patterns[(size_t) pattern][(size_t) row].load() & (1u << (unsigned) step))
                    addEvent (offset, row, 110);
        }
        const double now = std::floor (endSteps - 1.0e-6);
        playingStep.store ((int) ((((long long) now % kNumSteps) + kNumSteps) % kNumSteps));
    }
    else
    {
        playingStep.store (-1);
    }

    // ---- render, starting each sound at its exact place in the block ----
    std::stable_sort (events.begin(), events.begin() + numEvents, [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::array<float, 1024> scratch {};
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end)
        {
            const int n = juce::jmin (end - pos, (int) scratch.size());
            engine.render (left + pos, right != nullptr ? right + pos : scratch.data(), n);
            pos += n;
        }
    };
    for (int i = 0; i < numEvents; ++i)
    {
        renderTo (events[(size_t) i].offset);
        engine.trigger (kRowToTrack[(size_t) events[(size_t) i].row], events[(size_t) i].velocity);
    }
    renderTo (numSamples);

    const float master = masterParam->load();
    buffer.applyGainRamp (0, numSamples, lastMaster, master);
    lastMaster = master;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)   // safety limit: never send more than full scale to Logic
        juce::FloatVectorOperations::clip (buffer.getWritePointer (ch), buffer.getReadPointer (ch), -1.0f, 1.0f, numSamples);
}

// ---- step grid data ----
bool ELXARTwoProcessor::getStep (int pattern, int row, int step) const
{
    if (pattern < 0 || pattern >= kNumPatterns || row < 0 || row >= kNumRows || step < 0 || step >= kNumSteps) return false;
    return (patterns[(size_t) pattern][(size_t) row].load() & (1u << (unsigned) step)) != 0;
}

void ELXARTwoProcessor::setStep (int pattern, int row, int step, bool on)
{
    if (pattern < 0 || pattern >= kNumPatterns || row < 0 || row >= kNumRows || step < 0 || step >= kNumSteps) return;
    if (on) patterns[(size_t) pattern][(size_t) row].fetch_or (1u << (unsigned) step);
    else    patterns[(size_t) pattern][(size_t) row].fetch_and (~(1u << (unsigned) step));
}

void ELXARTwoProcessor::clearPattern (int pattern)
{
    if (pattern < 0 || pattern >= kNumPatterns) return;
    for (auto& row : patterns[(size_t) pattern]) row.store (0);
}

// A deliberately simple drum-pattern generator (the firmware's own Euclidean generator is part of its sequencer,
// which is planned for Milestone 2).
//   euclidHits : how many Drum 1 hits are spread as evenly as possible over the 16 steps
//   density    : how busy the hi-hat and the extra drums are
//   variation  : how much randomness is added on top
void ELXARTwoProcessor::generatePattern (int pattern, float density, float variation, int euclidHits)
{
    if (pattern < 0 || pattern >= kNumPatterns) return;
    density = juce::jlimit (0.0f, 1.0f, density);
    variation = juce::jlimit (0.0f, 1.0f, variation);
    euclidHits = juce::jlimit (1, kNumSteps, euclidHits);
    juce::Random rng (juce::Time::currentTimeMillis());
    auto chance = [&rng] (float p) { return rng.nextFloat() < p; };
    auto euclid = [] (int hits, int rotate)
    {
        uint32_t m = 0;
        for (int i = 0; i < kNumSteps; ++i)
            if (((i * hits) % kNumSteps) < hits)
                m |= 1u << (unsigned) ((i + rotate) % kNumSteps);
        return m;
    };
    std::array<uint32_t, kNumRows> rows {};

    rows[0] = euclid (euclidHits, 0);                                             // Drum 1
    for (int i = 0; i < kNumSteps; ++i)
        if ((i % 4) != 0 && chance (variation * 0.12f)) rows[0] |= 1u << (unsigned) i;

    rows[3] = (1u << 4) | (1u << 12);                                             // Snare: beats 2 and 4
    for (int i = 0; i < kNumSteps; ++i)
        if ((i % 4) != 0 && chance (variation * density * 0.25f)) rows[3] |= 1u << (unsigned) i;

    const int hatHits = juce::jlimit (2, kNumSteps, juce::roundToInt (2.0f + density * 14.0f));
    rows[4] = euclid (hatHits, 0);                                                // closed hat
    for (int i = 2; i < kNumSteps; i += 4)                                        // open hat on some off-beats
        if (chance (0.15f + variation * 0.5f)) { rows[5] |= 1u << (unsigned) i; rows[4] &= ~(1u << (unsigned) i); }

    for (int i = 0; i < kNumSteps; ++i)                                           // Drum 2 and Drum 3: sparse fills
    {
        if (chance (density * 0.18f * (0.4f + variation))) rows[1] |= 1u << (unsigned) i;
        if (chance (density * 0.14f * (0.4f + variation))) rows[2] |= 1u << (unsigned) i;
    }
    if (chance (0.3f + variation * 0.4f)) rows[6] |= 1u;                          // Cymbal on the first step

    for (int r = 0; r < kNumRows; ++r)
        patterns[(size_t) pattern][(size_t) r].store (rows[(size_t) r]);
}

// Writes the pattern as a small standard MIDI file: one bar of 16th notes, using the same notes that play the
// voices (C1 upwards), so the file dropped on this instrument's track plays the same beat.
juce::File ELXARTwoProcessor::writePatternMidiFile (int pattern) const
{
    if (pattern < 0 || pattern >= kNumPatterns) return {};
    const int ticksPerQuarter = 480, ticksPerStep = ticksPerQuarter / 4;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (int row = 0; row < kNumRows; ++row)
        for (int step = 0; step < kNumSteps; ++step)
            if (getStep (pattern, row, step))
            {
                const double t = (double) (step * ticksPerStep);
                seq.addEvent (juce::MidiMessage::noteOn (1, kRowNotes[(size_t) row], (juce::uint8) 110), t);
                seq.addEvent (juce::MidiMessage::noteOff (1, kRowNotes[(size_t) row]), t + ticksPerStep - 10);
            }
    seq.addEvent (juce::MidiMessage::endOfTrack(), (double) (kNumSteps * ticksPerStep));
    seq.updateMatchedPairs();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (ticksPerQuarter);
    midiFile.addTrack (seq);

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ELXAR-Two");
    folder.createDirectory();
    const auto file = folder.getChildFile ("ELXAR-Two pattern " + juce::String (pattern + 1) + ".mid");
    file.deleteFile();
    {
        juce::FileOutputStream out (file);
        if (! out.openedOk() || ! midiFile.writeTo (out, 1))
            return {};
        out.flush();
    }
    return file;
}

// ---- saving with the Logic project (settings and patterns) ----
void ELXARTwoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::String grid;
    for (int p = 0; p < kNumPatterns; ++p)
        for (int r = 0; r < kNumRows; ++r)
            grid << juce::String::toHexString ((int) patterns[(size_t) p][(size_t) r].load()) << " ";
    state.setProperty ("patterns", grid.trim(), nullptr);
    state.setProperty ("currentPattern", currentPattern.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void ELXARTwoProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    const auto state = juce::ValueTree::fromXml (*xml);
    const auto words = juce::StringArray::fromTokens (state.getProperty ("patterns").toString(), " ", "");
    if (words.size() == kNumPatterns * kNumRows)
        for (int p = 0; p < kNumPatterns; ++p)
            for (int r = 0; r < kNumRows; ++r)
                patterns[(size_t) p][(size_t) r].store ((uint32_t) words[p * kNumRows + r].getHexValue32() & 0xFFFFu);
    setCurrentPattern ((int) state.getProperty ("currentPattern", 0));
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* ELXARTwoProcessor::createEditor()
{
    return new ELXARTwoEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ELXARTwoProcessor();
}
