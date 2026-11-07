// Offline tests for the ResonanceSuppressor DSP core.
// Exit code 0 = all tests passed.

#include <juce_dsp/juce_dsp.h>

#include "ResonanceSuppressor.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace
{
    constexpr double sr = 48000.0;
    int failures = 0;

    void check (bool ok, const char* what, double value)
    {
        std::printf ("  [%s] %-58s %10.4f\n", ok ? "PASS" : "FAIL", what, value);
        if (! ok) ++failures;
    }

    using Signal = std::vector<std::vector<float>>;   // [channel][sample]

    Signal makeNoise (int channels, int length, float gain, unsigned seed)
    {
        juce::Random rng ((juce::int64) seed);
        Signal s ((size_t) channels, std::vector<float> ((size_t) length));
        for (auto& ch : s)
            for (auto& x : ch)
                x = gain * (rng.nextFloat() * 2.0f - 1.0f);
        return s;
    }

    void addSine (Signal& s, double hz, float amp)
    {
        for (auto& ch : s)
            for (size_t i = 0; i < ch.size(); ++i)
                ch[i] += amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / sr);
    }

    Signal run (const Signal& input, const ResonanceSuppressor::Settings& settings, int blockSize = 480,
                const Signal* sidechain = nullptr)
    {
        ResonanceSuppressor sup;
        sup.setSettings (settings);
        sup.prepare (sr, (int) input.size());

        Signal out = input;
        const int length = (int) out[0].size();
        std::vector<float*> ptrs (out.size());
        std::vector<const float*> scPtrs (sidechain != nullptr ? sidechain->size() : 0);

        for (int pos = 0; pos < length; pos += blockSize)
        {
            const int n = juce::jmin (blockSize, length - pos);
            for (size_t ch = 0; ch < out.size(); ++ch)
                ptrs[ch] = out[ch].data() + pos;
            for (size_t ch = 0; ch < scPtrs.size(); ++ch)
                scPtrs[ch] = (*sidechain)[ch].data() + pos;

            sup.process (ptrs.data(), (int) out.size(),
                         scPtrs.empty() ? nullptr : scPtrs.data(), (int) scPtrs.size(), n);
        }
        return out;
    }

    /** Averaged power spectrum (Hann, 8192 points) of channel 0 from 'start' on, in dB. */
    std::vector<double> powerSpectrumDb (const std::vector<float>& x, int start)
    {
        constexpr int order = 13, N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf (2 * N);
        std::vector<double> acc (N / 2 + 1, 0.0);
        int frames = 0;

        for (int pos = start; pos + N <= (int) x.size(); pos += N / 2, ++frames)
        {
            for (int n = 0; n < N; ++n)
                buf[(size_t) n] = x[(size_t) (pos + n)] * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / N));
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (int k = 0; k <= N / 2; ++k)
                acc[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
        }

        for (auto& v : acc)
            v = 10.0 * std::log10 (v / juce::jmax (1, frames) + 1.0e-30);
        return acc;
    }

    double binOf (double hz) { return hz * 8192.0 / sr; }

    double peakDb (const std::vector<double>& spec, double hz)
    {
        const int k = (int) std::round (binOf (hz));
        double m = -1000.0;
        for (int i = k - 2; i <= k + 2; ++i) m = juce::jmax (m, spec[(size_t) i]);
        return m;
    }

    double bandDb (const std::vector<double>& spec, double lo, double hi)
    {
        double sum = 0.0; int n = 0;
        for (int k = (int) binOf (lo); k <= (int) binOf (hi); ++k, ++n)
            sum += std::pow (10.0, spec[(size_t) k] / 10.0);
        return 10.0 * std::log10 (sum / juce::jmax (1, n));
    }

    double maxDelayedError (const Signal& in, const Signal& out, int latency, double scale = 1.0)
    {
        double err = 0.0;
        for (size_t ch = 0; ch < in.size(); ++ch)
            for (size_t i = (size_t) latency + 4096; i < in[ch].size(); ++i)   // skip the first frames (start-up)
                err = juce::jmax (err, std::abs ((double) out[ch][i] - scale * in[ch][i - (size_t) latency]));
        return err;
    }
}

int main()
{
    const int length = (int) sr * 4;

    ResonanceSuppressor probe;
    probe.prepare (sr, 2);
    const int latency = probe.getLatencySamples();
    std::printf ("latency: %d samples\n\n", latency);

    // 1) Transparency: depth 0 must reproduce the input exactly (delayed by the latency).
    std::printf ("Transparency\n");
    {
        auto in = makeNoise (2, length, 0.5f, 1);
        ResonanceSuppressor::Settings s;
        s.depthDb = 0.0f;
        check (maxDelayedError (in, run (in, s), latency) < 1.0e-4, "depth 0 -> output == delayed input (max err)", maxDelayedError (in, run (in, s), latency));

        s.depthDb = 24.0f; s.mix = 0.0f;
        check (maxDelayedError (in, run (in, s), latency) < 1.0e-4, "mix 0 -> output == delayed input (max err)", maxDelayedError (in, run (in, s), latency));

        s.depthDb = 0.0f; s.mix = 1.0f; s.outputGainDb = -6.0f;
        const double g = juce::Decibels::decibelsToGain (-6.0);
        check (maxDelayedError (in, run (in, s), latency, g) < 1.0e-4, "output -6 dB -> scaled delayed input (max err)", maxDelayedError (in, run (in, s), latency, g));

        // odd block sizes must not matter
        s.outputGainDb = 0.0f;
        check (maxDelayedError (in, run (in, s, 37), latency) < 1.0e-4, "block size 37 (max err)", maxDelayedError (in, run (in, s, 37), latency));
    }

    // 2) Resonance suppression: a strong 3 kHz tone over noise gets reduced, the noise much less.
    std::printf ("\nResonance suppression (3 kHz tone over noise)\n");
    {
        auto in = makeNoise (2, length, 0.05f, 2);
        addSine (in, 3000.0, 0.3f);

        ResonanceSuppressor::Settings s;
        s.depthDb = 12.0f;
        const auto out = run (in, s);

        const int settle = (int) sr;   // ignore the first second
        const auto specIn  = powerSpectrumDb (in[0],  settle - latency);
        const auto specOut = powerSpectrumDb (out[0], settle);

        const double toneDrop  = peakDb (specIn, 3000.0) - peakDb (specOut, 3000.0);
        const double noiseDrop = bandDb (specIn, 500.0, 1500.0) - bandDb (specOut, 500.0, 1500.0);
        const double highDrop  = bandDb (specIn, 8000.0, 12000.0) - bandDb (specOut, 8000.0, 12000.0);

        check (toneDrop > 8.0,  "tone reduction (dB, expect > 8)", toneDrop);
        check (toneDrop < 12.5, "tone reduction bounded by depth (dB, expect < 12.5)", toneDrop);
        check (std::abs (noiseDrop) < 0.3, "noise 0.5-1.5 kHz change (dB, expect |x| < 0.3)", noiseDrop);
        check (std::abs (highDrop)  < 0.3, "noise 8-12 kHz change (dB, expect |x| < 0.3)", highDrop);

        // 3) Delta: processed + delta == dry
        s.delta = true;
        const auto deltaOut = run (in, s);
        double err = 0.0;
        for (size_t ch = 0; ch < in.size(); ++ch)
            for (size_t i = (size_t) latency + 4096; i < in[ch].size(); ++i)
                err = juce::jmax (err, std::abs ((double) out[ch][i] + deltaOut[ch][i] - in[ch][i - (size_t) latency]));
        check (err < 1.0e-4, "processed + delta == delayed input (max err)", err);

        // 4) Range: tone outside the processing range is left alone.
        s.delta = false;
        s.highHz = 1000.0f;
        const auto outRange = run (in, s);
        const double rangeDrop = peakDb (specIn, 3000.0) - peakDb (powerSpectrumDb (outRange[0], settle), 3000.0);
        check (std::abs (rangeDrop) < 0.5, "tone above High Freq untouched (dB, expect |x| < 0.5)", rangeDrop);

        // 5) Threshold: a moderate peak is reduced less at 100 % than at 0 %.
        auto weak = makeNoise (1, length, 0.05f, 3);
        addSine (weak, 2000.0, 0.01f);
        s.highHz = 18000.0f;
        const auto specWeak = powerSpectrumDb (weak[0], settle - latency);
        auto weakDrop = [&] { return peakDb (specWeak, 2000.0) - peakDb (powerSpectrumDb (run (weak, s)[0], settle), 2000.0); };

        s.threshold = 0.0f;
        const double dropLowThr = weakDrop();
        s.threshold = 1.0f;
        const double dropHighThr = weakDrop();
        std::printf ("    weak tone drop: threshold 0 %% = %.2f dB, 100 %% = %.2f dB\n", dropLowThr, dropHighThr);
        check (dropLowThr > dropHighThr + 1.0, "threshold 0 % reduces weak tone more than 100 %", dropLowThr - dropHighThr);

        // 6) Detection EQ shifts the sensitivity (still at threshold 100 %).
        std::printf ("\nDetection EQ (weak 2 kHz tone)\n");
        s.emphasisHz = 2000.0f;
        s.emphasisDb = 12.0f;
        const double dropEmph = weakDrop();
        check (dropEmph > dropHighThr + 2.0, "emphasis +12 dB @ 2 kHz -> more reduction (dB gained)", dropEmph - dropHighThr);

        s.emphasisDb = 0.0f;
        s.tiltDbPerOct = -3.0f;
        const double dropTilt = weakDrop();
        check (dropTilt < dropHighThr - 1.0, "tilt -3 dB/oct -> less reduction at 2 kHz (dB lost)", dropHighThr - dropTilt);
    }

    // 7) Sidechain ("make room"): main = loud noise, sidechain = 3 kHz tone.
    std::printf ("\nSidechain\n");
    {
        const auto mainSig = makeNoise (1, length, 0.5f, 4);
        auto sc = makeNoise (1, length, 0.0f, 5);
        addSine (sc, 3000.0, 0.02f);

        ResonanceSuppressor::Settings s;
        s.depthDb = 12.0f;
        s.detect = ResonanceSuppressor::DetectSource::sidechain;

        const int settle = (int) sr;
        const auto specIn  = powerSpectrumDb (mainSig[0], settle - latency);
        const auto specOut = powerSpectrumDb (run (mainSig, s, 480, &sc)[0], settle);

        const double bandDrop = bandDb (specIn, 2900.0, 3100.0) - bandDb (specOut, 2900.0, 3100.0);
        const double farDrop  = bandDb (specIn, 500.0, 1500.0)  - bandDb (specOut, 500.0, 1500.0);
        check (bandDrop > 6.0, "main reduced around the sidechain tone (dB, expect > 6)", bandDrop);
        check (std::abs (farDrop) < 0.3, "main unchanged away from it (dB, expect |x| < 0.3)", farDrop);

        const auto silent = makeNoise (1, length, 0.0f, 6);
        const double errSilent = maxDelayedError (mainSig, run (mainSig, s, 480, &silent), latency);
        check (errSilent < 1.0e-4, "silent sidechain -> transparent (max err)", errSilent);

        const double errNone = maxDelayedError (mainSig, run (mainSig, s), latency);
        check (errNone < 1.0e-4, "no sidechain connected -> transparent (max err)", errNone);

        s.threshold = 1.0f;   // sidechain threshold -30 dB: the tone's band (about -45 dB) stays below
        const double dropHighThr = bandDb (specIn, 2900.0, 3100.0)
                                 - bandDb (powerSpectrumDb (run (mainSig, s, 480, &sc)[0], settle), 2900.0, 3100.0);
        check (std::abs (dropHighThr) < 1.0, "sidechain below threshold -> no reduction (dB)", dropHighThr);
    }

    // 8) Channel modes.
    std::printf ("\nChannel modes\n");
    {
        using CM = ResonanceSuppressor::ChannelMode;
        const int settle = (int) sr;

        // M/S round trip is exact.
        const auto stereo = makeNoise (2, length, 0.5f, 7);
        ResonanceSuppressor::Settings s;
        s.depthDb = 0.0f;
        s.channelMode = CM::midSide;
        const double errMs = maxDelayedError (stereo, run (stereo, s), latency);
        check (errMs < 1.0e-4, "Mid/Side, amount 0 -> transparent (max err)", errMs);

        // Mono-compatible signal (L == R) with a resonance: it lives in the mid channel only.
        auto dual = makeNoise (1, length, 0.05f, 8);
        addSine (dual, 3000.0, 0.3f);
        dual.push_back (dual[0]);
        const auto specIn = powerSpectrumDb (dual[0], settle - latency);

        s.depthDb = 12.0f;
        s.channelMode = CM::sideOnly;
        const double errSide = maxDelayedError (dual, run (dual, s), latency);
        check (errSide < 1.0e-4, "Side only on L == R signal -> untouched (max err)", errSide);

        s.channelMode = CM::midOnly;
        const auto outMid = run (dual, s);
        const double dropMid = peakDb (specIn, 3000.0) - peakDb (powerSpectrumDb (outMid[0], settle), 3000.0);
        check (dropMid > 8.0, "Mid only reduces the centred resonance (dB, expect > 8)", dropMid);

        double lrDiff = 0.0;
        for (size_t i = 0; i < outMid[0].size(); ++i)
            lrDiff = juce::jmax (lrDiff, (double) std::abs (outMid[0][i] - outMid[1][i]));
        check (lrDiff < 1.0e-5, "Mid only keeps L == R (max |L-R|)", lrDiff);

        s.channelMode = CM::independent;
        const double dropLr = peakDb (specIn, 3000.0) - peakDb (powerSpectrumDb (run (dual, s)[0], settle), 3000.0);
        check (dropLr > 8.0, "L / R independent reduces the resonance (dB, expect > 8)", dropLr);
    }

    // 9) Higher sample rates pick a larger FFT.
    std::printf ("\nSample rates\n");
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        ResonanceSuppressor r;
        r.prepare (rate, 2);
        std::printf ("    %6.0f Hz -> FFT %d, latency %d samples (%.1f ms)\n",
                     rate, r.getFftSize(), r.getLatencySamples(), 1000.0 * r.getLatencySamples() / rate);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
