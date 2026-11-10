#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "engine/ElxarEngine.h"

// ELXAR-Two, Milestone 1: the LXR-02 sound engine running inside the plugin, with a rough interface.
//
// Words used in this file:
//   "row"   = one line of the step grid, in the order shown on screen (7 rows: the hi-hat has a closed and an open row)
//   "strip" = one of the six voice panels on screen
//   "track" / "voice" = the engine's own numbering (see kRowToTrack / kStripToVoice)
class ELXARTwoProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumStrips = 6;
    static constexpr int kNumRows = 7;
    static constexpr int kNumSteps = 16;
    static constexpr int kNumPatterns = 4;

    static const std::array<const char*, kNumStrips> kStripNames;   // Drum 1, Drum 2, Drum 3, Snare, Hi-hat, Cymbal
    static const std::array<int, kNumStrips> kStripToVoice;         // strip -> engine voice 0..5
    static const std::array<const char*, kNumRows> kRowNames;
    static const std::array<int, kNumRows> kRowToTrack;             // row -> engine track 0..6
    static const std::array<int, kNumRows> kRowToStrip;             // row -> strip (for mute / solo)
    static const std::array<int, kNumRows> kRowNotes;               // MIDI note that plays each row (C1 upwards, white keys)

    ELXARTwoProcessor();
    ~ELXARTwoProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ELXAR-Two"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor (all safe to call from the window's thread) ----
    juce::AudioProcessorValueTreeState apvts;

    bool engineAvailable() const { return engine.ownsEngine(); }
    bool getStep (int pattern, int row, int step) const;
    void setStep (int pattern, int row, int step, bool on);
    void clearPattern (int pattern);
    int  getCurrentPattern() const { return currentPattern.load(); }
    void setCurrentPattern (int p) { currentPattern.store (juce::jlimit (0, kNumPatterns - 1, p)); }
    void generatePattern (int pattern, float density01, float variation01, int euclidHits);
    void auditionRow (int row) { pendingHits.fetch_or (1u << (unsigned) row); }
    juce::File writePatternMidiFile (int pattern) const;   // one bar, 16th notes; returns the file (or a non-existent file on failure)

    float getDisplayBpm() const { return displayBpm.load(); }
    bool  isHostPlaying() const { return hostPlaying.load(); }
    bool  hasHostTempo() const { return hostTempoKnown.load(); }
    int   getPlayingStep() const { return playingStep.load(); }   // -1 when the grid is not running

private:
    struct ParamLink { std::atomic<float>* value = nullptr; int enginePar = 0; float scale = 1.0f; int lastSent = -1; };
    struct Event { int offset; int row; int velocity; };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushParametersToEngine();
    void addEvent (int offset, int row, int velocity);

    elxar::Engine engine;
    std::vector<ParamLink> links;
    std::array<std::atomic<float>*, kNumStrips> muteParams {}, soloParams {}, fxParams {};
    std::array<int, kNumStrips> lastFxRoute {};
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* runModeParam = nullptr;
    float lastMaster = 0.7f;

    std::array<std::array<std::atomic<uint32_t>, kNumRows>, kNumPatterns> patterns {};
    std::atomic<int> currentPattern { 0 };
    std::atomic<uint32_t> pendingHits { 0 };

    std::array<Event, 512> events {};
    int numEvents = 0;

    double sampleRateHz = 44100.0;
    double freeRunSteps = 0.0;          // position of the "Always" clock, in 16th notes
    double expectedHostSteps = -1.0e9;  // where the host should be at the start of the next block
    long long lastTriggeredStep = -1;
    double lastBpmSentToEngine = 0.0;

    std::atomic<float> displayBpm { 120.0f };
    std::atomic<bool> hostPlaying { false }, hostTempoKnown { false };
    std::atomic<int> playingStep { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ELXARTwoProcessor)
};
