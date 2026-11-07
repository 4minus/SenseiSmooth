// Renders the plugin editor offscreen (after feeding a test signal) to a PNG.
// Usage: SenseiSmoothSnapshot <output.png> [input|bands|sc|drag|delta] [scale]
//   input: default view (one bell band + tilt set)
//   bands: two band-pass bands, band 1 selected (shows the band strip)
//   sc:    sidechain mode - the sidechain gets a vocal-like harmonic signal
//   drag:  drags band 1 with synthetic mouse events
//   delta: delta view
//   scale: render scale, e.g. 2 for high-DPI images (default 1)

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    void setParam (SenseiSmoothAudioProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.getState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    SpectrumView* findSpectrum (juce::Component& editor)
    {
        for (auto* c : editor.getChildren())
            if (auto* view = dynamic_cast<SpectrumView*> (c))
                return view;
        return nullptr;
    }

    /** Drags band 1 to 800 Hz / +9 dB with synthetic mouse events and
        leaves the pointer hovering there (so the read-out is visible). */
    void simulateBandDrag (SenseiSmoothAudioProcessor& processor, juce::Component& editor)
    {
        auto* view = findSpectrum (editor);
        if (view == nullptr) { std::printf ("SpectrumView not found\n"); return; }

        const float w = (float) view->getWidth(), h = (float) view->getHeight();
        auto xFor = [w] (float hz) { return w * std::log (hz / 20.0f) / std::log (1000.0f); };
        auto yFor = [h] (float db) { return h * (0.5f - 0.35f * db / 15.0f); };

        const auto s = processor.readSettings();
        const float bandHz = s.bands[0].hz;
        const juce::Point<float> from (xFor (bandHz), yFor (ResonanceSuppressor::sensitivityDb (s, bandHz)));
        const juce::Point<float> to (xFor (800.0f), yFor (9.0f));

        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        auto event = [&] (juce::Point<float> p, bool dragged)
        {
            return juce::MouseEvent (src, p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, view, view, now, from, now, 1, dragged);
        };

        view->mouseMove (event (from, false));
        view->mouseDown (event (from, false));
        view->mouseDrag (event (from + (to - from) * 0.5f, true));
        view->mouseDrag (event (to, true));
        view->mouseUp   (event (to, true));
        view->mouseMove (event (to, false));

        const auto after = processor.readSettings();
        std::printf ("band 1 after drag: %.0f Hz, %+.2f dB (expected ~800 Hz, ~+8.7 dB), selected band %d\n",
                     after.bands[0].hz, after.bands[0].gainDb, view->getSelectedBand());

        // double-click an empty spot -> a second band appears there and gets selected
        const juce::Point<float> empty (xFor (5000.0f), yFor (-6.0f));
        view->mouseDoubleClick (event (empty, false));
        auto s2 = processor.readSettings();
        std::printf ("after double-click at 5 kHz: band 2 on=%d, %.0f Hz, %+.1f dB, selected band %d\n",
                     (int) s2.bands[1].on, s2.bands[1].hz, s2.bands[1].gainDb, view->getSelectedBand());

        // mouse wheel on band 1 -> narrower
        juce::MouseWheelDetails wheel { 0.0f, 0.2f, false, false, false };
        view->mouseWheelMove (event (to, false), wheel);
        std::printf ("after wheel on band 1: Q %.2f (was 1.00, expect > 1)\n", processor.readSettings().bands[0].q);

        // double-click band 2 -> removed, selection cleared
        const auto p2 = juce::Point<float> (xFor (s2.bands[1].hz), yFor (ResonanceSuppressor::sensitivityDb (s2, s2.bands[1].hz)));
        view->mouseDoubleClick (event (p2, false));
        std::printf ("after double-click on band 2: on=%d, selected band %d\n",
                     (int) processor.readSettings().bands[1].on, view->getSelectedBand());

        view->mouseMove (event (to, false));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::File outFile (argc > 1 ? juce::String (argv[1]) : juce::String ("senseismooth_snapshot.png"));
    const bool sidechainMode = argc > 2 && juce::String (argv[2]) == "sc";
    const bool dragMode      = argc > 2 && juce::String (argv[2]) == "drag";
    const bool deltaMode     = argc > 2 && juce::String (argv[2]) == "delta";
    const bool bandsMode     = argc > 2 && juce::String (argv[2]) == "bands";
    const float renderScale  = argc > 3 ? juce::jlimit (0.5f, 4.0f, juce::String (argv[3]).getFloatValue()) : 1.0f;

    SenseiSmoothAudioProcessor processor;
    setParam (processor, ParamIDs::depth, 12.0f);
    if (deltaMode)
        setParam (processor, ParamIDs::delta, 1.0f);

    if (sidechainMode)
    {
        processor.enableAllBuses();
        setParam (processor, ParamIDs::detect, 1.0f);
    }
    else if (bandsMode)
    {
        // two band-pass bands: only the 350 Hz and 2.8 kHz regions are processed
        setParam (processor, ParamIDs::band (0, ParamIDs::bandOn), 1.0f);
        setParam (processor, ParamIDs::band (0, ParamIDs::bandShape), 3.0f);
        setParam (processor, ParamIDs::band (0, ParamIDs::bandFreq), 2800.0f);
        setParam (processor, ParamIDs::band (0, ParamIDs::bandGain), 6.0f);
        setParam (processor, ParamIDs::band (0, ParamIDs::bandQ), 1.4f);
        setParam (processor, ParamIDs::band (1, ParamIDs::bandOn), 1.0f);
        setParam (processor, ParamIDs::band (1, ParamIDs::bandShape), 3.0f);
        setParam (processor, ParamIDs::band (1, ParamIDs::bandFreq), 350.0f);
        setParam (processor, ParamIDs::band (1, ParamIDs::bandGain), 3.0f);
        setParam (processor, ParamIDs::band (1, ParamIDs::bandQ), 1.8f);
    }
    else
    {
        setParam (processor, ParamIDs::band (0, ParamIDs::bandOn), 1.0f);
        setParam (processor, ParamIDs::band (0, ParamIDs::bandFreq), 3000.0f);
        setParam (processor, ParamIDs::band (0, ParamIDs::bandGain), 6.0f);
        setParam (processor, ParamIDs::tilt, -1.0f);
    }

    constexpr double sr = 48000.0;
    constexpr int block = 512;
    processor.setRateAndBufferSizeDetails (sr, block);
    processor.prepareToPlay (sr, block);

    const int numChannels = processor.getTotalNumInputChannels();   // 2, or 4 with the sidechain bus
    juce::AudioBuffer<float> buffer (numChannels, block);
    juce::MidiBuffer midi;
    juce::Random rng (42);
    float lp = 0.0f;
    double phase[3] {}, scPhase = 0.0;
    const double freqs[3] { 350.0, 2800.0, 7200.0 };
    const float amps[3] { 0.05f, 0.08f, 0.04f };

    for (int b = 0; b < (int) (sr * 2.0) / block; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            // main: pink-ish noise with a few resonances
            lp = 0.97f * lp + 0.03f * (rng.nextFloat() * 2.0f - 1.0f);
            float x = 0.6f * lp + 0.01f * (rng.nextFloat() * 2.0f - 1.0f);
            for (int t = 0; t < 3; ++t)
            {
                phase[t] += freqs[t] / sr;
                x += amps[t] * (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase[t]);
            }
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);

            // sidechain: 220 Hz harmonic series with a formant bump around 1 kHz
            if (numChannels >= 4)
            {
                scPhase += 220.0 / sr;
                float v = 0.0f;
                for (int h = 1; h <= 20; ++h)
                {
                    const float f = 220.0f * (float) h;
                    const float formant = std::exp (-0.5f * std::pow (std::log2 (f / 1000.0f) / 0.6f, 2.0f));
                    v += 0.03f * (0.3f + formant) / (float) h * (float) std::sin (2.0 * juce::MathConstants<double>::pi * scPhase * h);
                }
                buffer.setSample (2, i, v);
                buffer.setSample (3, i, v);
            }
        }
        processor.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    // Let the display timer pick up the analysis data for a moment, then capture.
    bool ok = false;
    juce::Timer::callAfterDelay (800, [&]
    {
        if (dragMode)
            simulateBandDrag (processor, *editor);
        if (bandsMode)
            if (auto* view = findSpectrum (*editor))
                view->setSelectedBand (0);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, renderScale);

        outFile.deleteFile();
        juce::FileOutputStream stream (outFile);
        juce::PNGImageFormat png;
        ok = stream.openedOk() && png.writeImageToStream (image, stream);
        std::printf ("%s %s\n", ok ? "wrote" : "FAILED to write", outFile.getFullPathName().toRawUTF8());

        juce::MessageManager::getInstance()->stopDispatchLoop();
    });

    juce::MessageManager::getInstance()->runDispatchLoop();

    editor.reset();
    return ok ? 0 : 1;
}
