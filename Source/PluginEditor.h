#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

//==============================================================================
/** Spectrum + reduction display with an editable detection curve.

    Points on the gold curve:
      - Low / High pills: processing range (double-click resets)
      - Tilt point:       tilt of the sensitivity (y, double-click resets)
      - Band points 1..6: frequency (x) and gain (y); mouse wheel = width (Q);
                          right-click = shape menu; double-click = remove
    Double-click an empty spot to add a band there.
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
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Selected band (-1 = none). The editor shows its controls below the display. */
    int  getSelectedBand() const noexcept { return selectedBand; }
    void setSelectedBand (int index);
    std::function<void (int)> onBandSelected;

    static juce::Colour bandColour (int index);

private:
    struct Handle
    {
        enum Type { none, low, high, tilt, band };
        Type type = none;
        int  index = -1;

        bool operator== (const Handle& o) const noexcept { return type == o.type && index == o.index; }
        bool operator!= (const Handle& o) const noexcept { return ! (*this == o); }
    };

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
    void setParam (const juce::String& id, float value);
    void beginGestures (Handle);
    void endGestures (Handle);
    void addBandAt (juce::Point<float>);
    void removeBand (int index);
    void showBandMenu (int index);

    SenseiSmoothAudioProcessor& processor;
    Handle hovered, dragging;
    int selectedBand = -1;

    static constexpr int numPoints = 256;
    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    static constexpr float minDb = -96.0f, maxDb = 0.0f, maxRedDb = 24.0f;
    static constexpr float sensRangeDb = 15.0f;
    static constexpr float tiltHandleHz = 12000.0f;

    std::array<float, numPoints> levelDb {}, sidechainDb {}, reductionDb {}, outputDb {}, pointHz {};
};

//==============================================================================
/** Controls of the selected detection band, shown below the display. */
class BandStrip final : public juce::Component
{
public:
    explicit BandStrip (SenseiSmoothAudioProcessor&);

    /** -1 shows a short hint instead of controls. */
    void setBand (int index);
    std::function<void()> onRemove;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SenseiSmoothAudioProcessor& processor;
    int band = -1;

    juce::ToggleButton onButton { "On" };
    juce::ComboBox shapeBox;
    juce::Slider freq, gain, q;
    juce::Label freqLabel, gainLabel, qLabel, hint;
    juce::TextButton removeButton { "Remove" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttachment, gainAttachment, qAttachment;
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
    BandStrip bandStrip;

    Control amount, precision, threshold, attack, release;   // side panel
    Control mix, output;                                     // bottom bar
    Choice detect, channels;
    juce::ToggleButton deltaButton { "Delta" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttachment;

    std::unique_ptr<juce::Drawable> logo;
    juce::Rectangle<int> panelArea, bottomBar, logoArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SenseiSmoothAudioProcessorEditor)
};
