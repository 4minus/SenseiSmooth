#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

//==============================================================================
/** Spectrum + reduction display with an editable detection curve.

    Drag points:
      - Low / High pills: processing range
      - Emphasis point:   frequency (x) and sensitivity boost/cut (y)
      - Tilt point:       tilt of the sensitivity (y)
    Double-click a point to reset it.
*/
class SpectrumView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit SpectrumView (SenseiSmoothAudioProcessor&);

    void paint (juce::Graphics&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Handle { none, low, high, emphasis, tilt };

    void timerCallback() override;

    float xForFreq (float hz) const noexcept;
    float freqForX (float x) const noexcept;
    float yForLevel (float db) const noexcept;
    float yForSens (float db) const noexcept;
    float sensForY (float y) const noexcept;

    juce::Point<float> handlePosition (Handle, const ResonanceSuppressor::Settings&) const;
    Handle handleAt (juce::Point<float>) const;
    juce::StringArray paramsFor (Handle) const;
    juce::String describe (Handle, const ResonanceSuppressor::Settings&) const;
    void setParam (const char* id, float value);

    SenseiSmoothAudioProcessor& processor;
    Handle hovered = Handle::none, dragging = Handle::none;

    static constexpr int numPoints = 256;
    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    static constexpr float minDb = -96.0f, maxDb = 0.0f, maxRedDb = 24.0f;
    static constexpr float sensRangeDb = 15.0f;
    static constexpr float tiltHandleHz = 12000.0f;

    std::array<float, numPoints> levelDb {}, sidechainDb {}, reductionDb {}, outputDb {}, pointHz {};
};

//==============================================================================
class SenseiSmoothLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SenseiSmoothLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override;
};

//==============================================================================
class SenseiSmoothAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SenseiSmoothAudioProcessorEditor (SenseiSmoothAudioProcessor&);
    ~SenseiSmoothAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Size { large, medium, small, bar };

    struct Control
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    void addControl (Control&, const char* paramId, const juce::String& name, Size, const juce::String& tooltip);
    void addChoice (Choice&, const char* paramId, const juce::String& name, const juce::String& tooltip);

    SenseiSmoothAudioProcessor& processor;
    SenseiSmoothLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 700 };

    SpectrumView spectrum;

    Control amount, precision, threshold, attack, release;   // side panel
    Control mix, output;                                     // bottom bar
    Choice detect, channels;
    juce::ToggleButton deltaButton { "Delta" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttachment;

    std::unique_ptr<juce::Drawable> logo;
    juce::Rectangle<int> panelArea, bottomBar, logoArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SenseiSmoothAudioProcessorEditor)
};
