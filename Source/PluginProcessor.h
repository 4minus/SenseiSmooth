#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ResonanceSuppressor.h"

namespace ParamIDs
{
    // IDs are stable identifiers for automation/state; display names may differ.
    inline constexpr auto depth       = "depth";        // "Amount"
    inline constexpr auto sharpness   = "sharpness";    // "Precision"
    inline constexpr auto selectivity = "selectivity";  // "Threshold"
    inline constexpr auto attack      = "attack";
    inline constexpr auto release     = "release";
    inline constexpr auto lowFreq     = "lowFreq";
    inline constexpr auto highFreq    = "highFreq";
    inline constexpr auto tilt        = "tilt";
    inline constexpr auto mix         = "mix";
    inline constexpr auto output      = "output";
    inline constexpr auto delta       = "delta";
    inline constexpr auto detect      = "detect";
    inline constexpr auto channelMode = "channelMode";

    // Detection bands: "band1On", "band1Shape", "band1Freq", "band1Gain", "band1Q", ... (index is 0-based)
    inline constexpr auto bandOn    = "On";
    inline constexpr auto bandShape = "Shape";
    inline constexpr auto bandFreq  = "Freq";
    inline constexpr auto bandGain  = "Gain";
    inline constexpr auto bandQ     = "Q";
    inline juce::String band (int index, const char* field) { return "band" + juce::String (index + 1) + field; }
}

/** Display names of the band shapes, in the order of ResonanceSuppressor::BandShape. */
inline const juce::StringArray& bandShapeNames()
{
    static const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "Band Pass" };
    return names;
}

class SenseiSmoothAudioProcessor final : public juce::AudioProcessor
{
public:
    SenseiSmoothAudioProcessor();
    ~SenseiSmoothAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sensei Smooth"; }

    bool   acceptsMidi() const override  { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override                             { return 1; }
    int  getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept       { return state; }
    const ResonanceSuppressor& getSuppressor() const noexcept    { return suppressor; }

    /** Current settings as read from the parameters (also used by the display). */
    ResonanceSuppressor::Settings readSettings() const noexcept;

    /** True while the host feeds a sidechain bus with at least one channel. */
    bool isSidechainConnected() const noexcept { return sidechainConnected.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState state;
    ResonanceSuppressor suppressor;
    std::atomic<bool> sidechainConnected { false };

    struct Params
    {
        std::atomic<float>* depth = nullptr;
        std::atomic<float>* sharpness = nullptr;
        std::atomic<float>* selectivity = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* lowFreq = nullptr;
        std::atomic<float>* highFreq = nullptr;
        std::atomic<float>* tilt = nullptr;
        struct BandParams
        {
            std::atomic<float>* on = nullptr;
            std::atomic<float>* shape = nullptr;
            std::atomic<float>* freq = nullptr;
            std::atomic<float>* gain = nullptr;
            std::atomic<float>* q = nullptr;
        };
        std::array<BandParams, ResonanceSuppressor::numBands> bands {};
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* delta = nullptr;
        std::atomic<float>* detect = nullptr;
        std::atomic<float>* channelMode = nullptr;
    } params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SenseiSmoothAudioProcessor)
};
