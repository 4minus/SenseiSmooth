#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::NormalisableRange<float> skewedRange (float min, float max, float centre, float step = 0.0f)
    {
        juce::NormalisableRange<float> r (min, max, step);
        r.setSkewForCentre (centre);
        return r;
    }

    auto percentAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; });
    }

    auto msAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; });
    }

    auto hzAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz"
                                    : juce::String (juce::roundToInt (v)) + " Hz";
            });
    }

    auto dbAttributes (const char* suffix = " dB")
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([suffix] (float v, int)
            {
                return (v > 0.04f ? "+" : "") + juce::String (v, 1) + suffix;
            });
    }

    const juce::StringArray detectChoices      { "Input", "Sidechain" };
    const juce::StringArray channelModeChoices { "Stereo Link", "L / R", "Mid / Side", "Mid only", "Side only" };
}

SenseiSmoothAudioProcessor::SenseiSmoothAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state (*this, nullptr, "SenseiSmoothState", createLayout())
{
    params.depth       = state.getRawParameterValue (ParamIDs::depth);
    params.sharpness   = state.getRawParameterValue (ParamIDs::sharpness);
    params.selectivity = state.getRawParameterValue (ParamIDs::selectivity);
    params.attack      = state.getRawParameterValue (ParamIDs::attack);
    params.release     = state.getRawParameterValue (ParamIDs::release);
    params.lowFreq     = state.getRawParameterValue (ParamIDs::lowFreq);
    params.highFreq    = state.getRawParameterValue (ParamIDs::highFreq);
    params.tilt        = state.getRawParameterValue (ParamIDs::tilt);
    for (int i = 0; i < ResonanceSuppressor::numBands; ++i)
    {
        auto& b = params.bands[(size_t) i];
        b.on    = state.getRawParameterValue (ParamIDs::band (i, ParamIDs::bandOn));
        b.shape = state.getRawParameterValue (ParamIDs::band (i, ParamIDs::bandShape));
        b.freq  = state.getRawParameterValue (ParamIDs::band (i, ParamIDs::bandFreq));
        b.gain  = state.getRawParameterValue (ParamIDs::band (i, ParamIDs::bandGain));
        b.q     = state.getRawParameterValue (ParamIDs::band (i, ParamIDs::bandQ));
    }
    params.mix         = state.getRawParameterValue (ParamIDs::mix);
    params.output      = state.getRawParameterValue (ParamIDs::output);
    params.delta       = state.getRawParameterValue (ParamIDs::delta);
    params.detect      = state.getRawParameterValue (ParamIDs::detect);
    params.channelMode = state.getRawParameterValue (ParamIDs::channelMode);
}

juce::AudioProcessorValueTreeState::ParameterLayout SenseiSmoothAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout] (const char* id, const char* name, NormalisableRange<float> range,
                               float def, AudioParameterFloatAttributes attr)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, std::move (attr)));
    };

    addFloat (ParamIDs::depth,       "Amount",        NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f,    dbAttributes());
    addFloat (ParamIDs::sharpness,   "Precision",     NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,  percentAttributes());
    addFloat (ParamIDs::selectivity, "Threshold",     NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,  percentAttributes());
    addFloat (ParamIDs::attack,      "Attack",        skewedRange (0.5f, 100.0f, 8.0f, 0.1f), 5.0f,          msAttributes());
    addFloat (ParamIDs::release,     "Release",       skewedRange (5.0f, 1000.0f, 100.0f, 1.0f), 80.0f,      msAttributes());
    addFloat (ParamIDs::lowFreq,     "Low",           skewedRange (20.0f, 20000.0f, 640.0f, 1.0f), 80.0f,    hzAttributes());
    addFloat (ParamIDs::highFreq,    "High",          skewedRange (20.0f, 20000.0f, 640.0f, 1.0f), 18000.0f, hzAttributes());
    addFloat (ParamIDs::tilt,        "Tilt",          NormalisableRange<float> (-3.0f, 3.0f, 0.1f), 0.0f,    dbAttributes (" dB/oct"));
    // Detection bands (all off by default, spread across the spectrum)
    const float defaultHz[ResonanceSuppressor::numBands] = { 150.0f, 400.0f, 1000.0f, 2500.0f, 5000.0f, 10000.0f };
    for (int i = 0; i < ResonanceSuppressor::numBands; ++i)
    {
        const String prefix = "Band " + String (i + 1) + " ";
        auto id = [i] (const char* field) { return ParameterID { ParamIDs::band (i, field), 1 }; };

        layout.add (std::make_unique<AudioParameterBool>   (id (ParamIDs::bandOn),    prefix + "On", false));
        layout.add (std::make_unique<AudioParameterChoice> (id (ParamIDs::bandShape), prefix + "Shape", bandShapeNames(), 0));
        layout.add (std::make_unique<AudioParameterFloat>  (id (ParamIDs::bandFreq),  prefix + "Freq",
                                                            skewedRange (20.0f, 20000.0f, 640.0f, 1.0f), defaultHz[i], hzAttributes()));
        layout.add (std::make_unique<AudioParameterFloat>  (id (ParamIDs::bandGain),  prefix + "Gain",
                                                            NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 6.0f, dbAttributes()));
        layout.add (std::make_unique<AudioParameterFloat>  (id (ParamIDs::bandQ),     prefix + "Q",
                                                            skewedRange (0.3f, 8.0f, 1.2f, 0.01f), 1.0f,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction (
                                                                [] (float v, int) { return String (v, 2); })));
    }

    addFloat (ParamIDs::mix,        "Mix",           NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percentAttributes());
    addFloat (ParamIDs::output,      "Output",        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,  dbAttributes());

    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ParamIDs::delta, 1 },       "Delta", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::detect, 1 },      "Detect",   detectChoices, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::channelMode, 1 }, "Channels", channelModeChoices, 0));

    return layout;
}

ResonanceSuppressor::Settings SenseiSmoothAudioProcessor::readSettings() const noexcept
{
    using RS = ResonanceSuppressor;

    RS::Settings s;
    s.depthDb      = params.depth->load();
    s.sharpness    = params.sharpness->load() * 0.01f;
    s.threshold    = params.selectivity->load() * 0.01f;
    s.attackMs     = params.attack->load();
    s.releaseMs    = params.release->load();
    s.lowHz        = params.lowFreq->load();
    s.highHz       = params.highFreq->load();
    s.tiltDbPerOct = params.tilt->load();
    for (int i = 0; i < RS::numBands; ++i)
    {
        const auto& p = params.bands[(size_t) i];
        auto& b = s.bands[(size_t) i];
        b.on     = p.on->load() > 0.5f;
        b.shape  = (RS::BandShape) juce::jlimit (0, 3, juce::roundToInt (p.shape->load()));
        b.hz     = p.freq->load();
        b.gainDb = p.gain->load();
        b.q      = p.q->load();
    }
    s.mix         = params.mix->load() * 0.01f;
    s.outputGainDb = params.output->load();
    s.delta        = params.delta->load() > 0.5f;
    s.detect       = params.detect->load() > 0.5f ? RS::DetectSource::sidechain : RS::DetectSource::input;
    s.channelMode  = (RS::ChannelMode) juce::jlimit (0, 4, juce::roundToInt (params.channelMode->load()));
    return s;
}

void SenseiSmoothAudioProcessor::prepareToPlay (double sampleRate, int)
{
    suppressor.setSettings (readSettings());
    suppressor.prepare (sampleRate, juce::jmax (1, getMainBusNumInputChannels(), getMainBusNumOutputChannels()));
    setLatencySamples (suppressor.getLatencySamples());
}

void SenseiSmoothAudioProcessor::reset()
{
    suppressor.reset();
}

bool SenseiSmoothAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (out != layouts.getMainInputChannelSet())
        return false;

    // Sidechain: optional, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void SenseiSmoothAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainIn  = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);

    // Main output shares its channels with the main input; clear outputs that have no input.
    for (int ch = mainIn.getNumChannels(); ch < mainOut.getNumChannels(); ++ch)
        mainOut.clear (ch, 0, mainOut.getNumSamples());

    const float* const* scPtrs = nullptr;
    int numSc = 0;

    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        scPtrs = sc.getArrayOfReadPointers();
        numSc  = sc.getNumChannels();
    }

    sidechainConnected.store (numSc > 0);

    suppressor.setSettings (readSettings());
    suppressor.process (mainOut.getArrayOfWritePointers(), juce::jmin (mainIn.getNumChannels(), mainOut.getNumChannels()),
                        scPtrs, numSc, buffer.getNumSamples());
}

juce::AudioProcessorEditor* SenseiSmoothAudioProcessor::createEditor()
{
    return new SenseiSmoothAudioProcessorEditor (*this);
}

void SenseiSmoothAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SenseiSmoothAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SenseiSmoothAudioProcessor();
}
