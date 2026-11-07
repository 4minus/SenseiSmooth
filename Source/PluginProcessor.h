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
    inline constexpr auto emphFreq    = "emphFreq";
    inline constexpr auto emphGain    = "emphGain";
    inline constexpr auto mix         = "mix";
    inline constexpr auto output      = "output";
    inline constexpr auto delta       = "delta";
    inline constexpr auto detect      = "detect";
    inline constexpr auto channelMode = "channelMode";
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
        std::atomic<float>* emphFreq = nullptr;
        std::atomic<float>* emphGain = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* delta = nullptr;
        std::atomic<float>* detect = nullptr;
        std::atomic<float>* channelMode = nullptr;
    } params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SenseiSmoothAudioProcessor)
};
