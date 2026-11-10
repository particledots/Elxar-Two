#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SoundParams.h"
#include <algorithm>
#include <cmath>
#include <mutex>

extern "C" {
#include "Parameters.h"
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
constexpr int kFxRouteValue = 6;   // the engine's "audio out" value that sends a voice through the effect

juce::String sid (int strip, const char* what) { return "v" + juce::String (strip + 1) + "_" + what; }

// The firmware's Euclidean generator keeps scratch memory of its own, so only one caller at a time.
std::mutex& euclidMutex() { static std::mutex m; return m; }

uint64_t euclidBits (int length, int hits, int rotation)
{
    length = juce::jlimit (1, 16, length);
    hits = juce::jlimit (0, length, hits);
    if (hits == 0) return 0;
    const std::lock_guard<std::mutex> guard (euclidMutex());
    return elxar_euclid ((uint8_t) length, (uint8_t) hits, (uint8_t) (((rotation % length) + length) % length));
}
}

juce::AudioProcessorValueTreeState::ParameterLayout ELXARTwoProcessor::createLayout()
{
    using FloatP = juce::AudioParameterFloat;
    using BoolP = juce::AudioParameterBool;
    using ChoiceP = juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const juce::NormalisableRange<float> r127 (0.0f, 127.0f, 1.0f);
    auto init = [] (int par) { return (float) elxar_init_kit_value (par); };

    // The main knobs are host parameters (Logic can automate them). The IDs are the same as in Milestone 1.
    for (int s = 0; s < kNumStrips; ++s)
    {
        const int v = kStripToVoice[(size_t) s];
        const juce::String n (kStripNames[(size_t) s]);
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "pitch"), 1), n + " Pitch", r127, init (PAR_COARSE1 + 2 * v)));
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "decay"), 1), n + " Decay", r127, init (PAR_VELOD1 + 2 * v)));
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "tone"), 1), n + " Tone", r127, init (PAR_FILTER_FREQ_1 + v)));
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "level"), 1), n + " Level", r127, init (PAR_VOL1 + v)));
        layout.add (std::make_unique<FloatP> (juce::ParameterID (sid (s, "pan"), 1), n + " Pan", r127, init (elxar::panParam (v))));
        layout.add (std::make_unique<BoolP> (juce::ParameterID (sid (s, "mute"), 1), n + " Mute", false));
        layout.add (std::make_unique<BoolP> (juce::ParameterID (sid (s, "solo"), 1), n + " Solo", false));
        layout.add (std::make_unique<BoolP> (juce::ParameterID (sid (s, "fx"), 1), n + " To effect", false));
    }
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("hat_open_decay", 1), "Hi-hat Open decay", r127, init (PAR_VELOD6_OPEN)));

    layout.add (std::make_unique<ChoiceP> (juce::ParameterID ("fx_type", 1), "Effect type",
                                           juce::StringArray { "Off", "Drive", "Ring mod", "Compressor", "Delay", "Reverb" }, 0));
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("fx_mix", 1), "Effect dry/wet", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f));
    for (int i = 1; i <= 4; ++i)
        layout.add (std::make_unique<FloatP> (juce::ParameterID ("fx_p" + juce::String (i), 1), "Effect knob " + juce::String (i), r127, 64.0f));

    layout.add (std::make_unique<FloatP> (juce::ParameterID ("master", 1), "Master level", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.7f));
    layout.add (std::make_unique<ChoiceP> (juce::ParameterID ("run_mode", 1), "Sequencer runs",
                                           juce::StringArray { "With Logic transport", "Always" }, 0));
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("gen_density", 1), "Generator density", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f));
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("gen_variation", 1), "Generator variation", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f));
    layout.add (std::make_unique<FloatP> (juce::ParameterID ("gen_euclid", 1), "Generator kick hits", juce::NormalisableRange<float> (1.0f, 16.0f, 1.0f), 4.0f));
    return layout;
}

ELXARTwoProcessor::ELXARTwoProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ELXARTwo", createLayout())
{
    // ---- the kit: start from the firmware's init kit ----
    for (int p = 0; p < kNumKitParams; ++p)
        kitValues[(size_t) p].store ((int) elxar_init_kit_value (p));
    kitSent.fill (-1);

    for (int s = 0; s < kNumStrips; ++s)
    {
        const int v = kStripToVoice[(size_t) s];
        linkKitParam (PAR_COARSE1 + 2 * v, sid (s, "pitch"));
        linkKitParam (PAR_VELOD1 + 2 * v, sid (s, "decay"));
        linkKitParam (PAR_FILTER_FREQ_1 + v, sid (s, "tone"));
        linkKitParam (PAR_VOL1 + v, sid (s, "level"));
        linkKitParam (elxar::panParam (v), sid (s, "pan"));
        linkKitParam (PAR_AUDIO_OUT1 + v, sid (s, "fx"), true);
        muteParams[(size_t) s] = apvts.getRawParameterValue (sid (s, "mute"));
        soloParams[(size_t) s] = apvts.getRawParameterValue (sid (s, "solo"));
    }
    linkKitParam (PAR_VELOD6_OPEN, "hat_open_decay");
    linkKitParam (PAR_FX_TYPE, "fx_type");
    linkKitParam (PAR_FX_DRY_WET, "fx_mix");
    linkKitParam (PAR_FX_P1, "fx_p1");
    linkKitParam (PAR_FX_P2, "fx_p2");
    linkKitParam (PAR_FX_P3, "fx_p3");
    linkKitParam (PAR_FX_P4, "fx_p4");
    masterParam = apvts.getRawParameterValue ("master");
    runModeParam = apvts.getRawParameterValue ("run_mode");

    // The order in which changes are sent to the engine: modulation targets first, then the firmware's own order
    // (the same as elxar_engine.c uses when it loads a kit).
    {
        std::array<bool, (size_t) kNumKitParams> seen {};
        int n = 0;
        auto add = [&] (int par) { if (par >= 0 && par < kNumKitParams && ! seen[(size_t) par]) { seen[(size_t) par] = true; kitScanOrder[(size_t) n++] = par; } };
        for (int i = 0; i < 6; ++i) { add (PAR_VEL_DEST_1 + i); add (PAR_TARGET_LFO1 + i); }
        for (int i = 0; i < kNumKitParams; ++i) add (elxar_param_order (i));
        for (int i = 0; i < kNumKitParams; ++i) add (i);
    }

    // ---- patterns: all empty, plus a simple starting beat in pattern 1 so there is something to hear straight away ----
    for (auto& p : patterns) resetPattern (p);
    resetPattern (clipboard);
    for (auto& r : rowPosition) r.store (-1);
    for (int step : { 0, 4, 8, 12 }) setStepOn (0, 0, step, true);                    // Drum 1 on every beat
    for (int step : { 4, 12 }) setStepOn (0, 3, step, true);                          // Snare on 2 and 4
    for (int step = 0; step < kStepsPerBar; step += 2) setStepOn (0, 4, step, true);  // closed hat on 8th notes
    setStepOn (0, 5, 14, true);                                                       // one open hat
    markAllDirty();
}

void ELXARTwoProcessor::linkKitParam (int par, const juce::String& paramId, bool isFxRoute)
{
    if (par < 0 || par >= kNumKitParams) return;
    kitLinks[(size_t) par] = { apvts.getRawParameterValue (paramId), apvts.getParameter (paramId), isFxRoute };
    jassert (kitLinks[(size_t) par].raw != nullptr && kitLinks[(size_t) par].param != nullptr);
}

void ELXARTwoProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 44100.0;
    engine.prepare (sampleRateHz);
    kitSent.fill (-1);          // send every setting to the engine again
    markAllDirty();             // and every pattern
    lastRequestedPattern = -1;
    lastBpmSentToEngine = 0.0;
    clock.reset();
    engine.seqStop();
    freeRunSteps = 0.0;
    lastMaster = masterParam->load();
}

bool ELXARTwoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// =========================================================================================================
// Audio thread
// =========================================================================================================
int ELXARTwoProcessor::wantedKitValue (int par) const
{
    const auto& l = kitLinks[(size_t) par];
    if (l.raw != nullptr)
    {
        const float v = l.raw->load();
        return l.isFxRoute ? (v > 0.5f ? kFxRouteValue : 0) : juce::roundToInt (v);
    }
    return kitValues[(size_t) par].load();
}

void ELXARTwoProcessor::syncKitToEngine()
{
    for (int i = 0; i < kNumKitParams; ++i)
    {
        const int par = kitScanOrder[(size_t) i];
        const int v = wantedKitValue (par);
        if (v != kitSent[(size_t) par])
        {
            engine.setParam (par, v);
            kitSent[(size_t) par] = v;
        }
    }
}

void ELXARTwoProcessor::applyMutes()
{
    bool anySolo = false;
    for (int s = 0; s < kNumStrips; ++s)
        anySolo = anySolo || soloParams[(size_t) s]->load() > 0.5f;
    for (int s = 0; s < kNumStrips; ++s)
    {
        const bool muted = muteParams[(size_t) s]->load() > 0.5f;
        const bool solo = soloParams[(size_t) s]->load() > 0.5f;
        engine.setFader (kStripToVoice[(size_t) s], (muted || (anySolo && ! solo)) ? 0.0f : 1.0f);   // the engine fades this smoothly itself
    }
}

// Copies the rows that changed from the model into the firmware's pattern memory. If the window is in the middle of
// an edit (it holds the lock for a few microseconds), this simply waits for the next audio block: no audio is lost.
void ELXARTwoProcessor::syncPatternsToEngine()
{
    if (! anyDirty.load()) return;
    const juce::ScopedTryLock lock (modelLock);
    if (! lock.isLocked()) return;
    anyDirty.store (false);
    for (int p = 0; p < kNumPatterns; ++p)
    {
        const uint32_t bits = dirtyRows[(size_t) p].exchange (0);
        if (bits == 0) continue;
        const auto& pat = patterns[(size_t) p];
        for (int row = 0; row < kNumRows; ++row)
            if (bits & (1u << (unsigned) row))
                engine.seqWriteTrack (p, kRowToTrack[(size_t) row], pat.rows[(size_t) row].steps.data(), pat.rows[(size_t) row].length);
        if (bits & 0x80u)
            engine.seqWriteShuffle (p, pat.shuffle);
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

    // ---- bring the engine up to date with the window: sound, mutes, patterns, which pattern to play ----
    syncKitToEngine();
    applyMutes();
    syncPatternsToEngine();
    const int wantedPattern = currentPattern.load();
    if (wantedPattern != lastRequestedPattern)
    {
        engine.seqSelectPattern (wantedPattern);   // stopped: at once. running: when the longest row of the playing pattern ends
        lastRequestedPattern = wantedPattern;
    }

    // ---- the sequencer clock: the firmware sequencer gets 24 pulses per step, placed from the song position ----
    const bool always = runModeParam->load() > 0.5f;
    const double stepsPerSample = bpm * 4.0 / (60.0 * sampleRateHz);   // one step is a 16th note
    bool run = false;
    double posSteps = 0.0;
    if (always)
    {
        run = true;
        posSteps = freeRunSteps;
        freeRunSteps += stepsPerSample * numSamples;
        if (freeRunSteps > 3.0e9) freeRunSteps = 0.0;
    }
    else
    {
        run = playing && ppqKnown;
        posSteps = hostPpq * 4.0;
        freeRunSteps = 0.0;
    }
    const int numPulses = clock.advance (engine, run, posSteps, stepsPerSample, numSamples, pulseOffsets.data());

    // ---- sounds played by hand: "Hit" buttons in the window and notes on the track ----
    numEvents = 0;
    const uint32_t hits = pendingHits.exchange (0);
    for (int row = 0; row < kNumRows; ++row)
        if (hits & (1u << (unsigned) row))
            addEvent (0, row, 110);

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (! msg.isNoteOn()) continue;
        for (int row = 0; row < kNumRows; ++row)
            if (msg.getNoteNumber() == kRowNotes[(size_t) row])
                addEvent (juce::jlimit (0, numSamples - 1, metadata.samplePosition), row, juce::jlimit (1, 127, (int) msg.getVelocity()));
    }
    midi.clear();

    // ---- render, giving the engine each note and each clock pulse at its exact place in the block ----
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
    int ei = 0, pi = 0;   // both lists are already in time order
    while (ei < numEvents || pi < numPulses)
    {
        const bool noteFirst = pi >= numPulses || (ei < numEvents && events[(size_t) ei].offset <= pulseOffsets[(size_t) pi]);
        if (noteFirst)
        {
            const auto& e = events[(size_t) ei++];
            renderTo (e.offset);
            engine.trigger (kRowToTrack[(size_t) e.row], e.velocity);
        }
        else
        {
            renderTo (pulseOffsets[(size_t) pi++]);
            engine.seqPulse();
        }
    }
    renderTo (numSamples);

    // ---- what the window shows ----
    const bool seqRunning = engine.seqRunning();
    sequencerRunning.store (seqRunning);
    playingPattern.store (engine.ready() ? engine.seqActivePattern() : wantedPattern);
    for (int row = 0; row < kNumRows; ++row)
        rowPosition[(size_t) row].store (seqRunning ? engine.seqPosition (kRowToTrack[(size_t) row]) : -1);

    const float master = masterParam->load();
    buffer.applyGainRamp (0, numSamples, lastMaster, master);
    lastMaster = master;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)   // safety limit: never send more than full scale to Logic
        juce::FloatVectorOperations::clip (buffer.getWritePointer (ch), buffer.getReadPointer (ch), -1.0f, 1.0f, numSamples);
}

// =========================================================================================================
// Patterns (the model)
// =========================================================================================================
ElxarStep ELXARTwoProcessor::defaultStep()
{
    ElxarStep s {};
    s.on = 0; s.velocity = 110; s.probability = 100; s.note = ELXAR_DEFAULT_NOTE; s.flam = 0; s.shift = ELXAR_SHIFT_NONE;
    s.autoTarget[0] = s.autoTarget[1] = 0; s.autoValue[0] = s.autoValue[1] = 0;
    return s;
}

void ELXARTwoProcessor::resetPattern (PatternData& p)
{
    for (auto& row : p.rows)
    {
        row.steps.fill (defaultStep());
        row.length = kStepsPerBar;
    }
    p.shuffle = 0;
}

void ELXARTwoProcessor::markRowDirty (int pattern, int row)
{
    dirtyRows[(size_t) pattern].fetch_or (row < 0 ? 0x80u : (1u << (unsigned) row));
    anyDirty.store (true);
}

void ELXARTwoProcessor::markAllDirty()
{
    for (auto& d : dirtyRows) d.store (0xFFu);
    anyDirty.store (true);
}

#define ELXAR_CHECK_CELL(pattern, row, step) (pattern >= 0 && pattern < kNumPatterns && row >= 0 && row < kNumRows && step >= 0 && step < kNumSteps)

ElxarStep ELXARTwoProcessor::getStep (int pattern, int row, int step) const
{
    if (! ELXAR_CHECK_CELL (pattern, row, step)) return defaultStep();
    const juce::ScopedLock lock (modelLock);
    return patterns[(size_t) pattern].rows[(size_t) row].steps[(size_t) step];
}

void ELXARTwoProcessor::setStep (int pattern, int row, int step, const ElxarStep& s)
{
    if (! ELXAR_CHECK_CELL (pattern, row, step)) return;
    ElxarStep c = s;
    c.on = c.on ? 1 : 0;
    c.velocity = (uint8_t) juce::jlimit (1, 127, (int) c.velocity);
    c.probability = (uint8_t) juce::jlimit (0, 100, (int) c.probability);
    c.note = (uint8_t) juce::jlimit (0, 127, (int) c.note);
    c.flam = (uint8_t) juce::jlimit (0, 5, (int) c.flam);
    c.shift = (uint8_t) juce::jlimit (0, 14, (int) c.shift);
    for (int i = 0; i < 2; ++i)
    {
        if ((int) c.autoTarget[i] >= elxar_num_mod_targets()) c.autoTarget[i] = 0;
        c.autoValue[i] = (uint8_t) juce::jlimit (0, 127, (int) c.autoValue[i]);
    }
    {
        const juce::ScopedLock lock (modelLock);
        patterns[(size_t) pattern].rows[(size_t) row].steps[(size_t) step] = c;
    }
    markRowDirty (pattern, row);
}

void ELXARTwoProcessor::setStepOn (int pattern, int row, int step, bool on)
{
    if (! ELXAR_CHECK_CELL (pattern, row, step)) return;
    {
        const juce::ScopedLock lock (modelLock);
        patterns[(size_t) pattern].rows[(size_t) row].steps[(size_t) step].on = on ? 1 : 0;
    }
    markRowDirty (pattern, row);
}

int ELXARTwoProcessor::getTrackLength (int pattern, int row) const
{
    if (! ELXAR_CHECK_CELL (pattern, row, 0)) return kStepsPerBar;
    const juce::ScopedLock lock (modelLock);
    return patterns[(size_t) pattern].rows[(size_t) row].length;
}

void ELXARTwoProcessor::setTrackLength (int pattern, int row, int length)
{
    if (! ELXAR_CHECK_CELL (pattern, row, 0)) return;
    {
        const juce::ScopedLock lock (modelLock);
        patterns[(size_t) pattern].rows[(size_t) row].length = juce::jlimit (1, kNumSteps, length);
    }
    markRowDirty (pattern, row);
}

int ELXARTwoProcessor::getShuffle (int pattern) const
{
    if (pattern < 0 || pattern >= kNumPatterns) return 0;
    const juce::ScopedLock lock (modelLock);
    return patterns[(size_t) pattern].shuffle;
}

void ELXARTwoProcessor::setShuffle (int pattern, int shuffle)
{
    if (pattern < 0 || pattern >= kNumPatterns) return;
    {
        const juce::ScopedLock lock (modelLock);
        patterns[(size_t) pattern].shuffle = juce::jlimit (0, 127, shuffle);
    }
    markRowDirty (pattern, -1);
}

void ELXARTwoProcessor::clearPattern (int pattern)
{
    if (pattern < 0 || pattern >= kNumPatterns) return;
    {
        const juce::ScopedLock lock (modelLock);
        resetPattern (patterns[(size_t) pattern]);
    }
    dirtyRows[(size_t) pattern].store (0xFFu);
    anyDirty.store (true);
}

void ELXARTwoProcessor::copyPattern (int pattern)
{
    if (pattern < 0 || pattern >= kNumPatterns) return;
    const juce::ScopedLock lock (modelLock);
    clipboard = patterns[(size_t) pattern];
    clipboardValid.store (true);
}

bool ELXARTwoProcessor::pastePattern (int pattern)
{
    if (pattern < 0 || pattern >= kNumPatterns || ! clipboardValid.load()) return false;
    {
        const juce::ScopedLock lock (modelLock);
        patterns[(size_t) pattern] = clipboard;
    }
    dirtyRows[(size_t) pattern].store (0xFFu);
    anyDirty.store (true);
    return true;
}

// =========================================================================================================
// Pattern generator. Every rhythm comes from the firmware's own Euclidean generator (EuklidGenerator.c):
// "hits" spread as evenly as possible over "length" steps, then rotated.
// =========================================================================================================
void ELXARTwoProcessor::euclidFillRow (int pattern, int row, int length, int hits, int rotation)
{
    if (! ELXAR_CHECK_CELL (pattern, row, 0)) return;
    length = juce::jlimit (1, 16, length);   // 16 is the limit of the firmware's generator
    const uint64_t bits = euclidBits (length, hits, rotation);
    {
        const juce::ScopedLock lock (modelLock);
        auto& r = patterns[(size_t) pattern].rows[(size_t) row];
        r.length = length;
        for (int i = 0; i < kNumSteps; ++i)
            r.steps[(size_t) i].on = (i < length && (bits & (1ull << i)) != 0) ? 1 : 0;
    }
    markRowDirty (pattern, row);
}

//   kickHits  : how many Drum 1 hits per bar
//   density   : how busy the hi-hat and the extra drums are
//   variation : how much chance is added (extra hits, quieter "ghost" hits, steps that only play sometimes)
void ELXARTwoProcessor::generatePattern (int pattern, float density, float variation, int kickHits)
{
    if (pattern < 0 || pattern >= kNumPatterns) return;
    density = juce::jlimit (0.0f, 1.0f, density);
    variation = juce::jlimit (0.0f, 1.0f, variation);
    kickHits = juce::jlimit (1, 16, kickHits);
    juce::Random rng (juce::Time::currentTimeMillis());
    auto chance = [&rng] (float p) { return rng.nextFloat() < p; };

    std::array<uint64_t, kNumRows> rows {};
    rows[0] = euclidBits (16, kickHits, 0);                                              // Drum 1
    rows[3] = chance (variation * 0.35f) ? euclidBits (16, 3, 4 + 2 * rng.nextInt (3))   // Snare: now and then three hits
                                         : euclidBits (16, 2, 4);                        //        normally beats 2 and 4
    const int hatHits = juce::jlimit (2, 16, juce::roundToInt (2.0f + density * 14.0f));
    rows[4] = euclidBits (16, hatHits, 0);                                               // closed hat
    if (chance (0.25f + variation * 0.6f))                                               // open hat on off-beats, instead of the closed one
    {
        rows[5] = euclidBits (16, chance (variation) ? 2 : 1, 2 + 4 * rng.nextInt (4));
        rows[4] &= ~rows[5];
    }
    const int extra = juce::roundToInt (density * (1.0f + 3.0f * variation));            // Drum 2 and Drum 3: sparse fills
    if (extra > 0) rows[1] = euclidBits (16, juce::jlimit (1, 6, extra), 1 + rng.nextInt (15)) & ~rows[0];
    if (extra > 1) rows[2] = euclidBits (16, juce::jlimit (1, 5, extra - 1), 1 + rng.nextInt (15)) & ~rows[3];
    if (chance (0.3f + variation * 0.4f)) rows[6] = 1;                                   // Cymbal on the first step

    {
        const juce::ScopedLock lock (modelLock);
        auto& pat = patterns[(size_t) pattern];
        const int shuffle = pat.shuffle;
        resetPattern (pat);
        pat.shuffle = shuffle;
        for (int r = 0; r < kNumRows; ++r)
            for (int i = 0; i < kStepsPerBar; ++i)
            {
                auto& st = pat.rows[(size_t) r].steps[(size_t) i];
                st.on = (rows[(size_t) r] & (1ull << i)) != 0 ? 1 : 0;
                if (! st.on) continue;
                const bool onBeat = (i % 4) == 0;
                if (r == 4) st.velocity = (uint8_t) (onBeat ? 110 : (chance (variation) ? 60 + rng.nextInt (30) : 95));     // hat accents
                else if ((r == 1 || r == 2) && chance (variation * 0.6f)) st.probability = (uint8_t) (50 + 25 * rng.nextInt (2));  // fills that come and go
                else if (r == 0 && ! onBeat && chance (variation * 0.5f)) st.velocity = 80;                                // softer extra kicks
            }
    }
    dirtyRows[(size_t) pattern].store (0xFFu);
    anyDirty.store (true);
}

// =========================================================================================================
// MIDI file of a pattern
// =========================================================================================================
int ELXARTwoProcessor::autoBarsForPattern (int pattern) const
{
    if (pattern < 0 || pattern >= kNumPatterns) return 1;
    const juce::ScopedLock lock (modelLock);
    int longest = 1;
    for (const auto& row : patterns[(size_t) pattern].rows)
    {
        bool hasNotes = false;
        for (int i = 0; i < row.length; ++i) hasNotes = hasNotes || row.steps[(size_t) i].on != 0;
        if (hasNotes) longest = juce::jmax (longest, row.length);
    }
    return juce::jlimit (1, kNumBars, (longest + kStepsPerBar - 1) / kStepsPerBar);
}

// Writes the pattern as a standard MIDI file, using the same notes that play the voices (C1 upwards), so the file
// dropped on this instrument's track plays the same beat. The timing follows the firmware sequencer: each row loops
// at its own length, flams become several notes, shift and shuffle move the notes, and a step with a probability
// below 100 % is rolled again for every bar. (Per-step pitch and automation cannot be put into such a file.)
juce::File ELXARTwoProcessor::writePatternMidiFile (int pattern, int bars) const
{
    if (pattern < 0 || pattern >= kNumPatterns) return {};
    if (bars <= 0) bars = autoBarsForPattern (pattern);
    bars = juce::jlimit (1, 16, bars);
    PatternData pat;
    {
        const juce::ScopedLock lock (modelLock);
        pat = patterns[(size_t) pattern];
    }
    const int ticksPerQuarter = 480, ticksPerStep = ticksPerQuarter / 4;
    const int totalSteps = bars * kStepsPerBar;
    const long totalPulses = (long) totalSteps * ELXAR_PULSES_PER_STEP;
    const double shuffleDelay = (double) pat.shuffle / 127.0 * 16.0 / (double) ELXAR_PULSES_PER_STEP;   // in steps, as in elxar::SeqClock
    juce::Random rng (juce::Time::currentTimeMillis());

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (int row = 0; row < kNumRows; ++row)
    {
        const auto& r = pat.rows[(size_t) row];
        std::vector<std::pair<int, int>> hits;   // tick, velocity
        for (int k = 0; k < totalSteps; ++k)
        {
            const auto& st = r.steps[(size_t) (k % juce::jmax (1, r.length))];
            if (! st.on) continue;
            if (st.probability < 100 && rng.nextInt (100) + 1 > (int) st.probability) continue;
            const uint32_t bits = elxar_flam_bits (st.flam);
            const int shift = elxar_shift_pulses (st.shift);
            for (int p = 0; p < ELXAR_PULSES_PER_STEP; ++p)
            {
                if ((bits & (1u << (unsigned) (23 - p))) == 0) continue;
                long q = (long) k * ELXAR_PULSES_PER_STEP + p + shift;
                q = ((q % totalPulses) + totalPulses) % totalPulses;   // an early first step wraps to the end of the file
                const long step = q / ELXAR_PULSES_PER_STEP;
                double t = (double) step + (double) (q % ELXAR_PULSES_PER_STEP) / (double) ELXAR_PULSES_PER_STEP;
                if ((step & 1) != 0 && shuffleDelay > 0.0) t = juce::jmax (t, (double) step + shuffleDelay);
                hits.emplace_back (juce::roundToInt (t * ticksPerStep), juce::jlimit (1, 127, (int) st.velocity));
            }
        }
        std::sort (hits.begin(), hits.end());
        for (size_t i = 0; i < hits.size(); ++i)
        {
            if (i > 0 && hits[i].first == hits[i - 1].first) continue;   // two hits on the same tick: one note
            int length = ticksPerStep / 2;
            for (size_t j = i + 1; j < hits.size(); ++j)
                if (hits[j].first > hits[i].first) { length = juce::jmin (length, hits[j].first - hits[i].first - 2); break; }
            length = juce::jmax (3, length);
            seq.addEvent (juce::MidiMessage::noteOn (1, kRowNotes[(size_t) row], (juce::uint8) hits[i].second), (double) hits[i].first);
            seq.addEvent (juce::MidiMessage::noteOff (1, kRowNotes[(size_t) row]), (double) (hits[i].first + length));
        }
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), (double) (totalSteps * ticksPerStep));
    seq.sort();
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

// =========================================================================================================
// Kit (the complete sound)
// =========================================================================================================
int ELXARTwoProcessor::getKitParam (int par) const
{
    if (par < 0 || par >= kNumKitParams) return 0;
    return wantedKitValue (par);
}

void ELXARTwoProcessor::setKitParam (int par, int value)
{
    if (par < 0 || par >= kNumKitParams) return;
    value = juce::jlimit (0, 127, value);
    const auto& l = kitLinks[(size_t) par];
    if (l.param != nullptr)   // one of the host parameters: go through the host so Logic and the knobs on screen follow
    {
        const float v = l.isFxRoute ? (value >= kFxRouteValue ? 1.0f : 0.0f) : l.param->convertTo0to1 ((float) value);
        if (std::abs (l.param->getValue() - v) > 1.0e-6f)
            l.param->setValueNotifyingHost (v);
    }
    else
    {
        kitValues[(size_t) par].store (value);
    }
}

ELXARTwoProcessor::Kit ELXARTwoProcessor::currentKit() const
{
    Kit k {};
    for (int p = 0; p < kNumKitParams; ++p)
        k[(size_t) p] = (uint8_t) juce::jlimit (0, 127, wantedKitValue (p));
    return k;
}

void ELXARTwoProcessor::applyKit (const Kit& k)
{
    for (int p = 0; p < kNumKitParams; ++p)
        setKitParam (p, (int) k[(size_t) p]);
    modelVersion.fetch_add (1);
}

void ELXARTwoProcessor::loadInitKit()
{
    Kit k {};
    for (int p = 0; p < kNumKitParams; ++p)
        k[(size_t) p] = elxar_init_kit_value (p);
    applyKit (k);
}

void ELXARTwoProcessor::storeKitSlot (int slot)
{
    if (slot < 0 || slot >= kNumKitSlots) return;
    const Kit k = currentKit();
    const juce::ScopedLock lock (modelLock);
    kitSlots[(size_t) slot] = k;
    kitSlotUsed[(size_t) slot] = true;
}

bool ELXARTwoProcessor::recallKitSlot (int slot)
{
    if (slot < 0 || slot >= kNumKitSlots) return false;
    Kit k {};
    {
        const juce::ScopedLock lock (modelLock);
        if (! kitSlotUsed[(size_t) slot]) return false;
        k = kitSlots[(size_t) slot];
    }
    applyKit (k);
    return true;
}

bool ELXARTwoProcessor::isKitSlotUsed (int slot) const
{
    if (slot < 0 || slot >= kNumKitSlots) return false;
    const juce::ScopedLock lock (modelLock);
    return kitSlotUsed[(size_t) slot];
}

// The firmware's list of modulation targets, each parameter once, with a readable name.
const std::vector<ELXARTwoProcessor::TargetItem>& ELXARTwoProcessor::modTargets()
{
    static const std::vector<TargetItem> items = []
    {
        std::vector<TargetItem> v;
        std::array<std::vector<elxar::ParamInfo>, 6> voices;
        for (int i = 0; i < 6; ++i) voices[(size_t) i] = elxar::voiceParams (i);
        std::array<bool, (size_t) kNumKitParams> seen {};
        const int n = elxar_num_mod_targets();
        for (int i = 1; i < n; ++i)
        {
            const int par = elxar_mod_target_param (i);
            int voice = elxar_mod_target_voice (i);
            if (par <= 0 || par >= kNumKitParams || seen[(size_t) par]) continue;
            seen[(size_t) par] = true;
            juce::String name;
            if (par >= PAR_FX_P1 && par <= PAR_FX_P4) { name = "Effect: knob " + juce::String (par - PAR_FX_P1 + 1); voice = 0; }
            else if (par == PAR_VOICE_DECIMATION_ALL)  { name = "All voices: Sample rate"; voice = 0; }
            else if (voice >= 1 && voice <= 6)
            {
                if (const auto* info = elxar::findVoiceParam (voices[(size_t) (voice - 1)], par))
                    name = juce::String (elxar::voiceName (voice - 1)) + ": " + info->group + " / " + info->name;
            }
            if (name.isEmpty()) name = "Parameter " + juce::String (par);
            v.push_back ({ i, voice, par, name });
        }
        // the effect knobs and "all voices" first, then voice by voice
        std::stable_sort (v.begin(), v.end(), [] (const TargetItem& a, const TargetItem& b) { return a.voice < b.voice; });
        return v;
    }();
    return items;
}

int ELXARTwoProcessor::targetIndexForParam (int par)
{
    for (const auto& t : modTargets())
        if (t.par == par)
            return t.index;
    return 0;
}

// =========================================================================================================
// Saving with the Logic project: host parameters, the rest of the kit, 8 kit slots, 16 patterns
// =========================================================================================================
namespace
{
constexpr int kStepBytes = 10;

juce::String kitToHex (const ELXARTwoProcessor::Kit& k) { return juce::String::toHexString (k.data(), (int) k.size(), 0); }

bool kitFromHex (const juce::String& hex, ELXARTwoProcessor::Kit& k)
{
    juce::MemoryBlock mb;
    mb.loadFromHexString (hex);
    if (mb.getSize() != k.size()) return false;
    for (size_t i = 0; i < k.size(); ++i) k[i] = (uint8_t) juce::jmin (127, (int) (uint8_t) mb[i]);
    return true;
}
}

// All patterns as one compressed block of text: a small header, then per pattern the shuffle value and per row the
// length followed by 64 steps of 10 bytes each.
juce::String ELXARTwoProcessor::encodePatterns() const
{
    juce::MemoryOutputStream raw;
    raw.write ("ELXP", 4);
    raw.writeByte (1);   // format version
    raw.writeByte ((char) kNumPatterns); raw.writeByte ((char) kNumRows); raw.writeByte ((char) kNumSteps);
    {
        const juce::ScopedLock lock (modelLock);
        for (const auto& pat : patterns)
        {
            raw.writeByte ((char) pat.shuffle);
            for (const auto& row : pat.rows)
            {
                raw.writeByte ((char) row.length);
                for (const auto& s : row.steps)
                {
                    const uint8_t b[kStepBytes] = { s.on, s.velocity, s.probability, s.note, s.flam, s.shift, s.autoTarget[0], s.autoValue[0], s.autoTarget[1], s.autoValue[1] };
                    raw.write (b, kStepBytes);
                }
            }
        }
    }
    juce::MemoryOutputStream packed;
    {
        juce::GZIPCompressorOutputStream gz (packed, 6);
        gz.write (raw.getData(), raw.getDataSize());
    }
    return packed.getMemoryBlock().toBase64Encoding();
}

bool ELXARTwoProcessor::decodePatterns (const juce::String& text)
{
    juce::MemoryBlock packed;
    if (text.isEmpty() || ! packed.fromBase64Encoding (text)) return false;
    juce::MemoryInputStream in (packed, false);
    juce::GZIPDecompressorInputStream gz (in);
    juce::MemoryBlock raw;
    gz.readIntoMemoryBlock (raw, 4 * 1024 * 1024);
    const auto* d = static_cast<const uint8_t*> (raw.getData());
    const size_t size = raw.getSize();
    if (size < 8 || std::memcmp (d, "ELXP", 4) != 0 || d[4] != 1) return false;
    const int np = d[5], nr = d[6], ns = d[7];
    if (nr != kNumRows || ns != kNumSteps || np < 1) return false;
    const size_t perPattern = 1 + (size_t) nr * (1 + (size_t) ns * kStepBytes);
    if (size < 8 + (size_t) np * perPattern) return false;
    const int count = juce::jmin (np, kNumPatterns);
    const int numTargets = elxar_num_mod_targets();
    size_t at = 8;
    const juce::ScopedLock lock (modelLock);
    for (int p = 0; p < count; ++p)
    {
        size_t q = at + (size_t) p * perPattern;
        auto& pat = patterns[(size_t) p];
        pat.shuffle = juce::jlimit (0, 127, (int) d[q++]);
        for (auto& row : pat.rows)
        {
            row.length = juce::jlimit (1, kNumSteps, (int) d[q++]);
            for (auto& s : row.steps)
            {
                s.on = d[q] ? 1 : 0;
                s.velocity = (uint8_t) juce::jlimit (1, 127, (int) d[q + 1]);
                s.probability = (uint8_t) juce::jlimit (0, 100, (int) d[q + 2]);
                s.note = (uint8_t) juce::jlimit (0, 127, (int) d[q + 3]);
                s.flam = (uint8_t) (d[q + 4] > 5 ? 0 : d[q + 4]);
                s.shift = (uint8_t) (d[q + 5] > 14 ? ELXAR_SHIFT_NONE : d[q + 5]);
                s.autoTarget[0] = (uint8_t) ((int) d[q + 6] < numTargets ? d[q + 6] : 0);
                s.autoValue[0] = (uint8_t) juce::jmin (127, (int) d[q + 7]);
                s.autoTarget[1] = (uint8_t) ((int) d[q + 8] < numTargets ? d[q + 8] : 0);
                s.autoValue[1] = (uint8_t) juce::jmin (127, (int) d[q + 9]);
                q += kStepBytes;
            }
        }
    }
    return true;
}

void ELXARTwoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 2, nullptr);
    state.setProperty ("patternData", encodePatterns(), nullptr);
    state.setProperty ("currentPattern", currentPattern.load(), nullptr);
    state.setProperty ("kit", kitToHex (currentKit()), nullptr);
    {
        const juce::ScopedLock lock (modelLock);
        for (int i = 0; i < kNumKitSlots; ++i)
            state.setProperty ("kitSlot" + juce::String (i + 1), kitSlotUsed[(size_t) i] ? kitToHex (kitSlots[(size_t) i]) : juce::String(), nullptr);
    }
    state.setProperty ("uiScale", (double) uiScale.load(), nullptr);
    state.removeProperty ("patterns", nullptr);   // the Milestone 1 format
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void ELXARTwoProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    const auto state = juce::ValueTree::fromXml (*xml);
    const int version = (int) state.getProperty ("stateVersion", 1);

    if (version >= 2)
    {
        decodePatterns (state.getProperty ("patternData").toString());
    }
    else
    {
        // A project saved with Milestone 1: 4 patterns, 7 rows, one 16-step bar each, stored as hexadecimal words.
        const auto words = juce::StringArray::fromTokens (state.getProperty ("patterns").toString(), " ", "");
        if (words.size() == 4 * kNumRows)
        {
            const juce::ScopedLock lock (modelLock);
            for (auto& p : patterns) resetPattern (p);
            for (int p = 0; p < 4; ++p)
                for (int r = 0; r < kNumRows; ++r)
                {
                    const auto bits = (uint32_t) words[p * kNumRows + r].getHexValue32();
                    for (int i = 0; i < kStepsPerBar; ++i)
                        patterns[(size_t) p].rows[(size_t) r].steps[(size_t) i].on = (bits & (1u << (unsigned) i)) ? 1 : 0;
                }
        }
    }

    // The part of the kit that is not a host parameter (the host parameters come back through apvts below).
    Kit k {};
    if (kitFromHex (state.getProperty ("kit").toString(), k))
        for (int p = 0; p < kNumKitParams; ++p)
            if (kitLinks[(size_t) p].param == nullptr)
                kitValues[(size_t) p].store ((int) k[(size_t) p]);
    {
        const juce::ScopedLock lock (modelLock);
        for (int i = 0; i < kNumKitSlots; ++i)
            kitSlotUsed[(size_t) i] = kitFromHex (state.getProperty ("kitSlot" + juce::String (i + 1)).toString(), kitSlots[(size_t) i]);
    }
    setCurrentPattern ((int) state.getProperty ("currentPattern", 0));
    setUiScale ((float) (double) state.getProperty ("uiScale", 1.0));
    apvts.replaceState (state);
    markAllDirty();
    modelVersion.fetch_add (1);
}

juce::AudioProcessorEditor* ELXARTwoProcessor::createEditor()
{
    return new ELXARTwoEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ELXARTwoProcessor();
}
