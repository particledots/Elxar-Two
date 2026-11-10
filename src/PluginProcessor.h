#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "engine/ElxarEngine.h"

// ELXAR-Two, Milestone 2: the LXR-02 sound engine and the firmware's own step sequencer inside the plugin.
//
// Words used in this file:
//   "row"   = one line of the step grid, in the order shown on screen (7 rows: the hi-hat has a closed and an open row)
//   "strip" = one of the six voice panels on screen
//   "track" / "voice" = the engine's own numbering (see kRowToTrack / kStripToVoice)
//   "kit"   = one complete sound: the 247 sound parameters of the firmware
//
// Who owns what:
//   - The plugin keeps its own copy of the patterns and of the kit ("the model"). The window edits the model, and the
//     model is what is saved with the Logic project.
//   - The audio thread copies every change from the model into the firmware engine at the start of the next audio
//     block. Only the audio thread ever touches the engine.
class ELXARTwoProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumStrips = 6;
    static constexpr int kNumRows = 7;
    static constexpr int kNumSteps = ELXAR_NUM_STEPS;      // 64 steps per row (4 bars of 16)
    static constexpr int kStepsPerBar = 16;
    static constexpr int kNumBars = kNumSteps / kStepsPerBar;
    static constexpr int kNumPatterns = 16;                // the firmware holds 64; the plugin offers the first 16
    static constexpr int kNumKitSlots = 8;
    static constexpr int kNumKitParams = ELXAR_NUM_KIT_PARAMS;

    static const std::array<const char*, kNumStrips> kStripNames;   // Drum 1, Drum 2, Drum 3, Snare, Hi-hat, Cymbal
    static const std::array<int, kNumStrips> kStripToVoice;         // strip -> engine voice 0..5
    static const std::array<const char*, kNumRows> kRowNames;
    static const std::array<int, kNumRows> kRowToTrack;             // row -> engine track 0..6
    static const std::array<int, kNumRows> kRowToStrip;             // row -> strip (for mute / solo)
    static const std::array<int, kNumRows> kRowNotes;               // MIDI note that plays each row (C1 upwards, white keys)

    using Kit = std::array<uint8_t, (size_t) kNumKitParams>;

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

    // =====================================================================================================
    // Used by the window. Everything below is safe to call from the window's thread.
    // =====================================================================================================
    juce::AudioProcessorValueTreeState apvts;

    bool engineAvailable() const { return engine.ownsEngine(); }

    // ---- patterns ----
    static ElxarStep defaultStep();
    ElxarStep getStep (int pattern, int row, int step) const;
    void setStep (int pattern, int row, int step, const ElxarStep&);
    void setStepOn (int pattern, int row, int step, bool on);
    int  getTrackLength (int pattern, int row) const;
    void setTrackLength (int pattern, int row, int length);
    int  getShuffle (int pattern) const;
    void setShuffle (int pattern, int shuffle);
    void clearPattern (int pattern);
    void copyPattern (int pattern);
    bool pastePattern (int pattern);                    // false = nothing copied yet
    bool hasCopiedPattern() const { return clipboardValid.load(); }

    int  getCurrentPattern() const { return currentPattern.load(); }      // the pattern shown, edited and asked to play
    void setCurrentPattern (int p) { currentPattern.store (juce::jlimit (0, kNumPatterns - 1, p)); }
    int  getPlayingPattern() const { return playingPattern.load(); }      // the pattern the sequencer is on right now
    int  getRowPosition (int row) const { return row >= 0 && row < kNumRows ? rowPosition[(size_t) row].load() : -1; }   // step 0..63, -1 = stopped
    uint32_t getModelVersion() const { return modelVersion.load(); }      // changes whenever patterns or kit change behind the window's back

    // ---- pattern generator (built on the firmware's Euclidean generator) ----
    void euclidFillRow (int pattern, int row, int length, int hits, int rotation);
    void generatePattern (int pattern, float density01, float variation01, int kickHits);

    // ---- MIDI file of a pattern ----
    int  autoBarsForPattern (int pattern) const;                          // bars needed for the longest row that has notes
    juce::File writePatternMidiFile (int pattern, int bars) const;        // bars 0 = automatic; returns a non-existent file on failure

    // ---- kit (the complete sound) ----
    int  getKitParam (int par) const;
    void setKitParam (int par, int value);
    Kit  currentKit() const;
    void applyKit (const Kit&);
    void loadInitKit();
    void storeKitSlot (int slot);
    bool recallKitSlot (int slot);                                        // false = empty slot
    bool isKitSlotUsed (int slot) const;

    void auditionRow (int row) { pendingHits.fetch_or (1u << (unsigned) row); }

    float getDisplayBpm() const { return displayBpm.load(); }
    bool  isHostPlaying() const { return hostPlaying.load(); }
    bool  hasHostTempo() const { return hostTempoKnown.load(); }
    bool  isSequencerRunning() const { return sequencerRunning.load(); }

    // window size (1.0 = normal), remembered with the project
    float getUiScale() const { return uiScale.load(); }
    void  setUiScale (float s) { uiScale.store (juce::jlimit (0.5f, 2.5f, s)); }

    // The modulation targets the firmware offers, with names (for the LFO, velocity and step automation menus).
    struct TargetItem { int index; int voice; int par; juce::String name; };   // index = position in the firmware's list; voice 1..6 or 0
    static const std::vector<TargetItem>& modTargets();
    static int targetIndexForParam (int par);   // 0 if the parameter is not a target

private:
    struct TrackData { std::array<ElxarStep, (size_t) kNumSteps> steps; int length = kStepsPerBar; };
    struct PatternData { std::array<TrackData, (size_t) kNumRows> rows; int shuffle = 0; };
    struct KitLink { std::atomic<float>* raw = nullptr; juce::RangedAudioParameter* param = nullptr; bool isFxRoute = false; };
    struct Event { int offset; int row; int velocity; };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static void resetPattern (PatternData&);
    void linkKitParam (int par, const juce::String& paramId, bool isFxRoute = false);
    int  wantedKitValue (int par) const;
    void markRowDirty (int pattern, int row);
    void markAllDirty();
    void syncKitToEngine();
    void syncPatternsToEngine();
    void applyMutes();
    void addEvent (int offset, int row, int velocity);
    juce::String encodePatterns() const;
    bool decodePatterns (const juce::String&);

    elxar::Engine engine;
    elxar::SeqClock clock;

    // ---- the model (patterns), guarded by modelLock ----
    juce::CriticalSection modelLock;
    std::array<PatternData, (size_t) kNumPatterns> patterns;
    PatternData clipboard;
    std::atomic<bool> clipboardValid { false };
    std::array<Kit, (size_t) kNumKitSlots> kitSlots {};
    std::array<bool, (size_t) kNumKitSlots> kitSlotUsed {};
    std::array<std::atomic<uint32_t>, (size_t) kNumPatterns> dirtyRows {};   // bit 0..6 = row, bit 7 = shuffle
    std::atomic<bool> anyDirty { false };
    std::atomic<uint32_t> modelVersion { 0 };

    // ---- the kit: every parameter is either one of the host parameters (apvts) or a plain stored value ----
    std::array<KitLink, (size_t) kNumKitParams> kitLinks {};
    std::array<std::atomic<int>, (size_t) kNumKitParams> kitValues {};
    std::array<int, (size_t) kNumKitParams> kitSent {};          // audio thread: what the engine has
    std::array<int, (size_t) kNumKitParams> kitScanOrder {};     // the firmware's own sending order

    std::array<std::atomic<float>*, kNumStrips> muteParams {}, soloParams {};
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* runModeParam = nullptr;
    float lastMaster = 0.7f;

    std::atomic<int> currentPattern { 0 }, playingPattern { 0 };
    int lastRequestedPattern = -1;
    std::array<std::atomic<int>, (size_t) kNumRows> rowPosition {};
    std::atomic<uint32_t> pendingHits { 0 };

    std::array<Event, 256> events {};
    int numEvents = 0;
    std::array<int, (size_t) elxar::SeqClock::kMaxPulses> pulseOffsets {};

    double sampleRateHz = 44100.0;
    double freeRunSteps = 0.0;          // position of the "Always" clock, in 16th notes
    double lastBpmSentToEngine = 0.0;

    std::atomic<float> displayBpm { 120.0f };
    std::atomic<bool> hostPlaying { false }, hostTempoKnown { false }, sequencerRunning { false };
    std::atomic<float> uiScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ELXARTwoProcessor)
};
