#include "ResonanceSuppressor.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float silenceDb        = -96.0f;  // main-signal bins below this are never reduced
    constexpr float kneeDb           = 3.0f;
    constexpr float envWidthOct      = 1.0f;    // width of the reference envelope (input mode)
    constexpr int   envMinHalf       = 6;       // minimum half width of the envelope in bins
    constexpr int   maxDilation      = 32;      // cap for the peak-widening (max) filter in bins
    constexpr double detectorMs      = 25.0;    // time constant of the detector power average
    constexpr float sidechainLeadDb  = 6.0f;    // sidechain mode: cut until the sidechain leads by this much

    /** Bandwidth in octaves for a Q value (classic EQ relation). Q 1 ~ 1.4 oct, Q 4 ~ 0.36 oct. */
    inline float bandwidthOct (float q) noexcept
    {
        q = juce::jmax (0.05f, q);
        return 2.0f * std::asinh (1.0f / (2.0f * q)) / std::log (2.0f);
    }

    inline float smoothstep (float t) noexcept
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return t * t * (3.0f - 2.0f * t);
    }

    inline float softKnee (float x) noexcept
    {
        constexpr float h = kneeDb * 0.5f;
        if (x <= -h) return 0.0f;
        if (x <   h) return (x + h) * (x + h) / (2.0f * kneeDb);
        return x;
    }
}

ResonanceSuppressor::ResonanceSuppressor()
    : displayLevelDb     (new std::atomic<float>[maxDisplayBins]),
      displaySidechainDb (new std::atomic<float>[maxDisplayBins]),
      displayReductionDb (new std::atomic<float>[maxDisplayBins]),
      displayOutputDb    (new std::atomic<float>[maxDisplayBins])
{
    for (int i = 0; i < maxDisplayBins; ++i)
    {
        displayLevelDb[(size_t) i].store (-200.0f);
        displayOutputDb[(size_t) i].store (-200.0f);
        displaySidechainDb[(size_t) i].store (-200.0f);
        displayReductionDb[(size_t) i].store (0.0f);
    }
}

float ResonanceSuppressor::bandCurveDb (const Band& b, float hz) noexcept
{
    const float d  = std::log2 (juce::jmax (1.0f, hz) / juce::jmax (10.0f, b.hz));   // distance in octaves
    const float bw = bandwidthOct (b.q);

    switch (b.shape)
    {
        case BandShape::lowShelf:  return b.gainDb / (1.0f + std::exp (d / (bw * 0.25f)));
        case BandShape::highShelf: return b.gainDb / (1.0f + std::exp (-d / (bw * 0.25f)));
        case BandShape::bell:
        case BandShape::bandPass:
        default:
        {
            const float sigma = bw / 2.3548f;          // bandwidth = full width at half maximum
            return b.gainDb * std::exp (-0.5f * (d / sigma) * (d / sigma));
        }
    }
}

float ResonanceSuppressor::sensitivityDb (const Settings& s, float hz) noexcept
{
    hz = juce::jmax (20.0f, hz);
    float db = s.tiltDbPerOct * std::log2 (hz / 1000.0f);
    for (const auto& b : s.bands)
        if (b.on)
            db += bandCurveDb (b, hz);
    return db;
}

float ResonanceSuppressor::focusWeight (const Settings& s, float hz) noexcept
{
    bool anyBandPass = false;
    float w = 0.0f;

    for (const auto& b : s.bands)
    {
        if (! b.on || b.shape != BandShape::bandPass)
            continue;

        anyBandPass = true;
        // flat-topped window: 1 inside, 0.5 at the band edges, quickly 0 outside
        const float half = bandwidthOct (b.q) * 0.5f;
        const float d = std::abs (std::log2 (juce::jmax (1.0f, hz) / juce::jmax (10.0f, b.hz))) / half;
        w = juce::jmax (w, std::exp (-0.6931f * d * d * d * d));
    }

    return anyBandPass ? w : 1.0f;
}

void ResonanceSuppressor::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    numChannelsPrepared = juce::jmax (1, numChannels);

    // Keep the frequency resolution roughly constant across sample rates.
    fftOrder = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
    fftSize  = 1 << fftOrder;
    hopSize  = fftSize / 4;
    numBins  = fftSize / 2 + 1;

    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize ((size_t) fftSize);
    double windowSum = 0.0;
    for (int n = 0; n < fftSize; ++n)
    {
        const double hann = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / fftSize);
        window[(size_t) n] = (float) std::sqrt (hann);
        windowSum += window[(size_t) n];
    }

    // Periodic Hann (= sqrt-Hann squared) at 75 % overlap sums to exactly 2.
    olaScale  = 0.5f;
    levelNorm = (float) (2.0 / windowSum);

    auto alloc2d = [] (std::vector<std::vector<float>>& v, int rows, int size)
    {
        v.assign ((size_t) rows, std::vector<float> ((size_t) size, 0.0f));
    };

    alloc2d (inRing,             numChannelsPrepared,  fftSize);
    alloc2d (outRing,            numChannelsPrepared,  fftSize);
    alloc2d (scRing,             maxSidechainChannels, fftSize);
    alloc2d (spectra,            numChannelsPrepared,  2 * fftSize);
    alloc2d (reductionState,     numChannelsPrepared,  numBins);
    alloc2d (detectorPerChannel, numChannelsPrepared,  numBins);
    scSpectrum.assign ((size_t) (2 * fftSize), 0.0f);

    for (auto* v : { &powerBuf, &levelDbBuf, &scPowerBuf, &scLevelDbBuf, &envDbBuf, &scBandBuf,
                     &targetBuf, &smoothBuf, &rangeWeight, &sensitivity, &detectorLinked, &detectorSidechain,
                     &detectorOutput, &outLevelDbBuf })
        v->assign ((size_t) numBins, 0.0f);

    prefixBuf.assign ((size_t) numBins + 1, 0.0);

    for (auto* v : { &envLo, &envHi, &redLo, &redHi })
        v->assign ((size_t) numBins, 0);

    // The envelope tables only depend on the FFT size, not on the settings.
    const float envRatio = std::pow (2.0f, envWidthOct * 0.5f);
    for (int k = 0; k < numBins; ++k)
    {
        envLo[(size_t) k] = juce::jmax (0,           juce::jmin (k - envMinHalf, (int) std::floor ((float) k / envRatio)));
        envHi[(size_t) k] = juce::jmin (numBins - 1, juce::jmax (k + envMinHalf, (int) std::ceil  ((float) k * envRatio)));
    }

    tablesValid = false;
    updateTables();

    reset();
    numDisplayBins.store (juce::jmin (numBins, maxDisplayBins), std::memory_order_release);
}

void ResonanceSuppressor::reset()
{
    for (auto* vv : { &inRing, &outRing, &scRing, &reductionState, &detectorPerChannel })
        for (auto& v : *vv)
            std::fill (v.begin(), v.end(), 0.0f);

    std::fill (detectorLinked.begin(),    detectorLinked.end(),    0.0f);
    std::fill (detectorSidechain.begin(), detectorSidechain.end(), 0.0f);
    std::fill (detectorOutput.begin(),    detectorOutput.end(),    0.0f);
    ringPos = 0;
    hopCounter = 0;
}

void ResonanceSuppressor::updateTables() noexcept
{
    const auto& s = settings;

    auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };

    if (tablesValid && same (s.sharpness, tSharpness) && same (s.lowHz, tLow) && same (s.highHz, tHigh)
        && same (s.tiltDbPerOct, tTilt) && s.bands == tBands)
        return;

    tablesValid = true;
    tSharpness  = s.sharpness;
    tLow        = s.lowHz;
    tHigh       = s.highHz;
    tTilt       = s.tiltDbPerOct;
    tBands      = s.bands;

    // Width of the reduction smoothing: 1/3 octave (soft) .. 1/24 octave (surgical)
    const float widthOct = juce::jmap (juce::jlimit (0.0f, 1.0f, s.sharpness), 1.0f / 3.0f, 1.0f / 24.0f);
    const float ratio    = std::pow (2.0f, widthOct * 0.5f);
    const float binHz    = (float) (sampleRate / fftSize);

    const float lo = juce::jmax (1.0f, juce::jmin (s.lowHz, s.highHz));
    const float hi = juce::jmax (s.lowHz, s.highHz);

    for (int k = 0; k < numBins; ++k)
    {
        redLo[(size_t) k] = juce::jmax (0,           juce::jmin (k - 1, (int) std::floor ((float) k / ratio)));
        redHi[(size_t) k] = juce::jmin (numBins - 1, juce::jmax (k + 1, (int) std::ceil  ((float) k * ratio)));

        const float f = juce::jmax (1.0e-3f, (float) k * binHz);
        const float tLowFade  = 1.0f + std::log2 (f / lo) / 0.5f;   // half-octave fade below lowHz
        const float tHighFade = 1.0f - std::log2 (f / hi) / 0.5f;   // half-octave fade above highHz
        rangeWeight[(size_t) k] = k == 0 ? 0.0f : smoothstep (tLowFade) * smoothstep (tHighFade) * focusWeight (s, f);
        sensitivity[(size_t) k] = sensitivityDb (s, f);
    }
}

bool ResonanceSuppressor::isChannelActive (int ch) const noexcept
{
    switch (settings.channelMode)
    {
        case ChannelMode::midOnly:  return ch == 0;
        case ChannelMode::sideOnly: return ch == 1;
        case ChannelMode::linked:
        case ChannelMode::independent:
        case ChannelMode::midSide:
        default:                    return true;
    }
}

void ResonanceSuppressor::movingAverage (const float* in, float* out, int n,
                                         const std::vector<int>& lo, const std::vector<int>& hi,
                                         std::vector<double>& prefix) noexcept
{
    prefix[0] = 0.0;
    for (int i = 0; i < n; ++i)
        prefix[(size_t) i + 1] = prefix[(size_t) i] + in[i];

    for (int k = 0; k < n; ++k)
    {
        const int a = lo[(size_t) k], b = hi[(size_t) k];
        out[k] = (float) ((prefix[(size_t) b + 1] - prefix[(size_t) a]) / (double) (b - a + 1));
    }
}

void ResonanceSuppressor::computeTargetReduction (const float* levelDb, const float* sidechainDb, float* targetDb) noexcept
{
    const auto& s = settings;
    const float depth     = juce::jlimit (0.0f, 48.0f, s.depthDb);
    const float threshold = juce::jlimit (0.0f, 1.0f, s.threshold);
    float* raw = smoothBuf.data();

    if (sidechainDb == nullptr)
    {
        // Input mode: how far each bin sticks out of its own neighbourhood.
        const float thresholdDb = 1.5f + 10.5f * threshold;
        float* env = envDbBuf.data();
        movingAverage (levelDb, env, numBins, envLo, envHi, prefixBuf);

        for (int k = 0; k < numBins; ++k)
        {
            const float w = rangeWeight[(size_t) k];
            if (levelDb[k] < silenceDb || w <= 0.0f)
            {
                raw[k] = 0.0f;
                continue;
            }

            const float excess = levelDb[k] - env[k] - thresholdDb + sensitivity[(size_t) k];
            raw[k] = juce::jmin (depth, softKnee (excess)) * w;
        }
    }
    else
    {
        // Sidechain mode: compare band energies (power average over the
        // reduction window) of main and sidechain.
        auto bandLevel = [this] (const float* dbIn, float* dbOut)
        {
            float* p = targetBuf.data();   // scratch, overwritten below anyway
            for (int k = 0; k < numBins; ++k)
                p[k] = std::pow (10.0f, 0.1f * dbIn[k]);
            movingAverage (p, dbOut, numBins, redLo, redHi, prefixBuf);
            for (int k = 0; k < numBins; ++k)
                dbOut[k] = 10.0f * std::log10 (dbOut[k] + 1.0e-30f);
        };

        float* mainBand = envDbBuf.data();
        float* scBand   = scBandBuf.data();
        bandLevel (levelDb, mainBand);
        bandLevel (sidechainDb, scBand);

        const float scThresholdDb = sidechainThresholdDb (threshold);

        for (int k = 0; k < numBins; ++k)
        {
            const float w = rangeWeight[(size_t) k];
            if (levelDb[k] < silenceDb || w <= 0.0f)
            {
                raw[k] = 0.0f;
                continue;
            }

            const float sens     = sensitivity[(size_t) k];
            const float presence = scBand[k] - scThresholdDb;                 // how loud the sidechain is
            const float masking  = mainBand[k] - scBand[k] + sidechainLeadDb; // how much main competes
            raw[k] = juce::jmin (depth, softKnee (juce::jmin (presence, masking) + sens)) * w;
        }
    }

    // Widen peaks (max filter) so narrow resonances keep their depth, then
    // smooth (moving average) so the gain curve has no hard edges.
    for (int k = 0; k < numBins; ++k)
    {
        const int a = juce::jmax (redLo[(size_t) k], k - maxDilation);
        const int b = juce::jmin (redHi[(size_t) k], k + maxDilation);
        float m = 0.0f;
        for (int i = a; i <= b; ++i)
            m = juce::jmax (m, raw[i]);
        targetDb[k] = m;
    }

    std::copy (targetDb, targetDb + numBins, raw);
    movingAverage (raw, targetDb, numBins, redLo, redHi, prefixBuf);
}

void ResonanceSuppressor::process (float* const* channels, int numChannels,
                                   const float* const* sidechain, int numSidechainChannels,
                                   int numSamples) noexcept
{
    if (fft == nullptr)
        return;

    numChannels = juce::jmin (numChannels, numChannelsPrepared);
    numSidechainChannels = sidechain != nullptr ? juce::jlimit (0, maxSidechainChannels, numSidechainChannels) : 0;

    const int mask = fftSize - 1;
    const bool midSide = numChannels == 2 && isMidSide (settings.channelMode);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto pos = (size_t) ringPos;

        if (midSide)
        {
            const float l = channels[0][i], r = channels[1][i];
            inRing[0][pos] = 0.5f * (l + r);
            inRing[1][pos] = 0.5f * (l - r);

            const float m = outRing[0][pos], s = outRing[1][pos];
            channels[0][i] = m + s;
            channels[1][i] = m - s;
            outRing[0][pos] = outRing[1][pos] = 0.0f;
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                inRing[(size_t) ch][pos] = channels[ch][i];
                channels[ch][i] = outRing[(size_t) ch][pos];
                outRing[(size_t) ch][pos] = 0.0f;
            }
        }

        for (int ch = 0; ch < maxSidechainChannels; ++ch)
            scRing[(size_t) ch][pos] = ch < numSidechainChannels ? sidechain[ch][i] : 0.0f;

        ringPos = (ringPos + 1) & mask;

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;
            processFrame (numChannels, numSidechainChannels);
        }
    }
}

void ResonanceSuppressor::processFrame (int numChannels, int numSidechainChannels) noexcept
{
    updateTables();

    const auto& s = settings;
    const int mask = fftSize - 1;

    auto analyse = [&] (const std::vector<float>& ring, std::vector<float>& spec)
    {
        for (int k = 0; k < fftSize; ++k)
            spec[(size_t) k] = ring[(size_t) ((ringPos + k) & mask)] * window[(size_t) k];

        std::fill (spec.begin() + fftSize, spec.end(), 0.0f);
        fft->performRealOnlyForwardTransform (spec.data(), true);
    };

    auto power = [] (const std::vector<float>& spec, int k)
    {
        const float re = spec[(size_t) (2 * k)], im = spec[(size_t) (2 * k + 1)];
        return re * re + im * im;
    };

    // ---- analysis
    for (int ch = 0; ch < numChannels; ++ch)
        analyse (inRing[(size_t) ch], spectra[(size_t) ch]);

    // ---- detection + gain smoothing
    const double hopSeconds   = hopSize / sampleRate;
    const float attackCoef    = (float) std::exp (-hopSeconds / (juce::jmax (0.1f, s.attackMs)  * 0.001));
    const float releaseCoef   = (float) std::exp (-hopSeconds / (juce::jmax (0.1f, s.releaseMs) * 0.001));
    const float detectorCoef  = (float) std::exp (-hopSeconds / (detectorMs * 0.001));
    const float normDb        = 20.0f * std::log10 (levelNorm);

    auto toLevelDb = [&] (const float* p, std::vector<float>& detector, std::vector<float>& outDb)
    {
        for (int k = 0; k < numBins; ++k)
        {
            auto& d = detector[(size_t) k];
            d = p[k] + detectorCoef * (d - p[k]);
            outDb[(size_t) k] = 10.0f * std::log10 (d + 1.0e-20f) + normDb;
        }
    };

    auto smoothInTime = [&] (std::vector<float>& state, const float* target)
    {
        for (int k = 0; k < numBins; ++k)
        {
            const float coef = target[k] > state[(size_t) k] ? attackCoef : releaseCoef;
            state[(size_t) k] = target[k] + coef * (state[(size_t) k] - target[k]);
        }
    };

    const int numDisplay = juce::jmin (numBins, maxDisplayBins);

    // Sidechain spectrum (max over its channels). Without a connected
    // sidechain it is silent, which means: nothing gets reduced.
    const bool useSidechain = s.detect == DetectSource::sidechain;
    if (useSidechain)
    {
        std::fill (scPowerBuf.begin(), scPowerBuf.end(), 0.0f);
        for (int ch = 0; ch < numSidechainChannels; ++ch)
        {
            analyse (scRing[(size_t) ch], scSpectrum);
            for (int k = 0; k < numBins; ++k)
                scPowerBuf[(size_t) k] = juce::jmax (scPowerBuf[(size_t) k], power (scSpectrum, k));
        }
        toLevelDb (scPowerBuf.data(), detectorSidechain, scLevelDbBuf);
    }

    for (int k = 0; k < numDisplay; ++k)
        displaySidechainDb[(size_t) k].store (useSidechain ? scLevelDbBuf[(size_t) k] : -200.0f, std::memory_order_relaxed);

    const float* scDb = useSidechain ? scLevelDbBuf.data() : nullptr;

    // Linked power (max over channels) is always computed: it feeds the display
    // and, in linked mode, the detector.
    for (int k = 0; k < numBins; ++k)
    {
        float p = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            p = juce::jmax (p, power (spectra[(size_t) ch], k));
        powerBuf[(size_t) k] = p;
    }

    toLevelDb (powerBuf.data(), detectorLinked, levelDbBuf);

    for (int k = 0; k < numDisplay; ++k)
        displayLevelDb[(size_t) k].store (levelDbBuf[(size_t) k], std::memory_order_relaxed);

    const bool linked = s.channelMode == ChannelMode::linked || numChannels == 1;

    if (linked)
    {
        computeTargetReduction (levelDbBuf.data(), scDb, targetBuf.data());
        smoothInTime (reductionState[0], targetBuf.data());
    }
    else
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (isChannelActive (ch))
            {
                for (int k = 0; k < numBins; ++k)
                    powerBuf[(size_t) k] = power (spectra[(size_t) ch], k);

                toLevelDb (powerBuf.data(), detectorPerChannel[(size_t) ch], levelDbBuf);
                computeTargetReduction (levelDbBuf.data(), scDb, targetBuf.data());
            }
            else
            {
                std::fill (targetBuf.begin(), targetBuf.end(), 0.0f);
            }

            smoothInTime (reductionState[(size_t) ch], targetBuf.data());
        }
    }

    for (int k = 0; k < numDisplay; ++k)
    {
        float r = reductionState[0][(size_t) k];
        if (! linked)
            for (int ch = 1; ch < numChannels; ++ch)
                r = juce::jmax (r, reductionState[(size_t) ch][(size_t) k]);
        displayReductionDb[(size_t) k].store (r, std::memory_order_relaxed);
    }

    // ---- apply gains, synthesis, overlap-add
    const float mix     = juce::jlimit (0.0f, 1.0f, s.mix);
    const float outGain = juce::Decibels::decibelsToGain (s.outputGainDb, -200.0f);

    // powerBuf is free again: collect the output spectrum (max over channels) for the display.
    std::fill (powerBuf.begin(), powerBuf.end(), 0.0f);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& spec = spectra[(size_t) ch];
        const auto& state = reductionState[linked ? 0 : (size_t) ch];

        for (int k = 0; k < numBins; ++k)
        {
            const float g = std::pow (10.0f, -0.05f * state[(size_t) k]);
            const float removed = mix * (1.0f - g);
            const float G = (s.delta ? removed : 1.0f - removed) * outGain;
            spec[(size_t) (2 * k)]     *= G;
            spec[(size_t) (2 * k + 1)] *= G;
            powerBuf[(size_t) k] = juce::jmax (powerBuf[(size_t) k], power (spec, k));
        }

        fft->performRealOnlyInverseTransform (spec.data());

        auto& out = outRing[(size_t) ch];
        for (int k = 0; k < fftSize; ++k)
            out[(size_t) ((ringPos + k) & mask)] += spec[(size_t) k] * window[(size_t) k] * olaScale;
    }

    toLevelDb (powerBuf.data(), detectorOutput, outLevelDbBuf);
    for (int k = 0; k < numDisplay; ++k)
        displayOutputDb[(size_t) k].store (outLevelDbBuf[(size_t) k], std::memory_order_relaxed);
}
