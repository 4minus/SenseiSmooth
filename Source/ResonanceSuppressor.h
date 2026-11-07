#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>
#include <vector>

/**
    Dynamic resonance suppressor.

    Works on an STFT (sqrt-Hann analysis + synthesis window, 75 % overlap) and
    applies a real, smoothly varying gain per bin. Two detection modes:

    - Input: bins that stick out of a smoothed reference ("envelope") of the
      signal's own spectrum by more than a threshold are attenuated.
    - Sidechain ("make room"): the main signal is attenuated in the bands where
      the sidechain signal is present (above an absolute threshold) and where
      the main signal competes with it. The cut is limited so that the
      sidechain leads the main signal by a fixed margin, never more.

    A detection EQ (tilt + one bell band) shifts the sensitivity per frequency.
    Stereo can be processed linked, independent L/R, or as Mid/Side (both, or
    only one of them).

    Mix and delta listening are applied in the spectral domain, so the dry path
    is sample-aligned with the wet path by construction. Latency = FFT size.
*/
class ResonanceSuppressor
{
public:
    enum class DetectSource { input, sidechain };
    enum class ChannelMode  { linked, independent, midSide, midOnly, sideOnly };

    struct Settings
    {
        float depthDb      = 6.0f;     // maximum reduction, 0 .. 24 dB
        float sharpness    = 0.5f;     // 0 = broad, 1 = narrow notches
        float threshold    = 0.5f;     // 0 = very sensitive, 1 = only strong peaks / loud sidechain
        float attackMs     = 5.0f;
        float releaseMs    = 80.0f;
        float lowHz        = 80.0f;    // processing range
        float highHz       = 18000.0f;
        float tiltDbPerOct = 0.0f;     // detection EQ: + = more sensitive towards highs (pivot 1 kHz)
        float emphasisHz   = 3000.0f;  // detection EQ: bell band
        float emphasisDb   = 0.0f;     //   + = more sensitive around emphasisHz
        float mix          = 1.0f;     // 0 .. 1
        float outputGainDb = 0.0f;
        bool  delta        = false;    // listen to what gets removed
        DetectSource detect = DetectSource::input;
        ChannelMode  channelMode = ChannelMode::linked;
    };

    static constexpr int maxSidechainChannels = 2;

    ResonanceSuppressor();

    void prepare (double sampleRate, int numChannels);
    void reset();

    void setSettings (const Settings& s) noexcept { settings = s; }
    const Settings& getSettings() const noexcept  { return settings; }

    /** In-place processing of the main signal. The sidechain is analysis-only and may be null. */
    void process (float* const* channels, int numChannels,
                  const float* const* sidechain, int numSidechainChannels,
                  int numSamples) noexcept;

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        process (channels, numChannels, nullptr, 0, numSamples);
    }

    int    getLatencySamples() const noexcept { return fftSize; }
    int    getFftSize() const noexcept        { return fftSize; }
    double getSampleRate() const noexcept     { return sampleRate; }

    /** Absolute sidechain threshold (display dB units) for a given threshold setting. */
    static float sidechainThresholdDb (float threshold) noexcept { return -90.0f + 60.0f * threshold; }

    /** Detection-EQ offset in dB at a frequency (used by DSP and display). */
    static float sensitivityDb (const Settings&, float hz) noexcept;

    //==============================================================================
    // Lock-free display data (written by the audio thread once per frame).
    static constexpr int maxDisplayBins = 8192 / 2 + 1;

    int   getNumDisplayBins() const noexcept             { return numDisplayBins.load (std::memory_order_acquire); }
    float getDisplayLevelDb (int bin) const noexcept      { return displayLevelDb[(size_t) bin].load (std::memory_order_relaxed); }
    float getDisplaySidechainDb (int bin) const noexcept  { return displaySidechainDb[(size_t) bin].load (std::memory_order_relaxed); }
    float getDisplayReductionDb (int bin) const noexcept  { return displayReductionDb[(size_t) bin].load (std::memory_order_relaxed); }

    /** Spectrum of what actually leaves the plugin (in delta mode: the removed signal). */
    float getDisplayOutputDb (int bin) const noexcept     { return displayOutputDb[(size_t) bin].load (std::memory_order_relaxed); }

private:
    void processFrame (int numChannels, int numSidechainChannels) noexcept;
    void updateTables() noexcept;
    void computeTargetReduction (const float* levelDb, const float* sidechainDb, float* targetDb) noexcept;
    bool isChannelActive (int ch) const noexcept;
    static bool isMidSide (ChannelMode m) noexcept
    {
        return m == ChannelMode::midSide || m == ChannelMode::midOnly || m == ChannelMode::sideOnly;
    }
    static void movingAverage (const float* in, float* out, int n,
                               const std::vector<int>& lo, const std::vector<int>& hi,
                               std::vector<double>& prefix) noexcept;

    Settings settings;

    double sampleRate = 44100.0;
    int fftOrder = 11, fftSize = 2048, hopSize = 512, numBins = 1025;
    int numChannelsPrepared = 0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;           // sqrt-Hann, used for analysis and synthesis
    float olaScale = 1.0f;               // overlap-add normalisation
    float levelNorm = 1.0f;              // magnitude -> dBFS of a full-scale sine

    // Streaming buffers (ring buffers of fftSize, shared position)
    std::vector<std::vector<float>> inRing, outRing, scRing;
    int ringPos = 0, hopCounter = 0;

    // Per-frame work buffers
    std::vector<std::vector<float>> spectra;   // 2 * fftSize each (JUCE real FFT layout)
    std::vector<float> scSpectrum;
    std::vector<float> powerBuf, levelDbBuf, scPowerBuf, scLevelDbBuf;
    std::vector<float> envDbBuf, scBandBuf, targetBuf, smoothBuf;
    std::vector<double> prefixBuf;

    // Per-channel smoothed reduction state in dB (index 0 used when linked)
    std::vector<std::vector<float>> reductionState;

    // Time-smoothed detector power. Averaging a few frames keeps random noise
    // peaks from being mistaken for resonances.
    std::vector<float> detectorLinked, detectorSidechain, detectorOutput;
    std::vector<float> outLevelDbBuf;
    std::vector<std::vector<float>> detectorPerChannel;

    // Frequency tables (bin index ranges, weights), rebuilt when settings change
    std::vector<int> envLo, envHi, redLo, redHi;
    std::vector<float> rangeWeight, sensitivity;
    float tSharpness = -1.0f, tLow = -1.0f, tHigh = -1.0f, tTilt = -100.0f, tEmphHz = -1.0f, tEmphDb = -100.0f;

    std::unique_ptr<std::atomic<float>[]> displayLevelDb, displaySidechainDb, displayReductionDb, displayOutputDb;
    std::atomic<int> numDisplayBins { 0 };
};
