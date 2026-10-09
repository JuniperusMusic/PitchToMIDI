#include "PitchDetector.h"

void PitchDetector::prepare (double sr, float lowestPossibleHz)
{
    sampleRate = sr;
    lowestAllocatedHz = juce::jmax (10.0f, lowestPossibleHz);
    maxTau = (int) std::ceil (sampleRate / lowestAllocatedHz) + 2;

    const int maxFft = juce::nextPowerOfTwo (2 * maxTau);
    const int maxOrder = juce::roundToInt (std::log2 ((double) maxFft));

    ffts.clear();
    for (int order = 0; order <= maxOrder; ++order)
        ffts.push_back (order >= 4 ? std::make_unique<juce::dsp::FFT> (order) : nullptr);

    ringSize = 2 * maxTau;
    ring.assign ((size_t) ringSize, 0.0f);
    frame.assign ((size_t) ringSize, 0.0f);
    prefix.assign ((size_t) ringSize + 1, 0.0);
    bufA.assign ((size_t) maxFft * 2, 0.0f);
    bufB.assign ((size_t) maxFft * 2, 0.0f);
    cmnd.assign ((size_t) maxTau + 1, 1.0f);
    writePos = 0;

    setRange (60.0f, 2000.0f);
}

void PitchDetector::setRange (float minHz, float maxHz)
{
    minHz = juce::jmax (minHz, lowestAllocatedHz);
    maxHz = juce::jlimit (minHz * 2.0f, (float) sampleRate * 0.25f, maxHz);

    tauMax = juce::jmin (maxTau, (int) std::ceil (sampleRate / minHz) + 2);
    tauMin = juce::jmax (2, (int) std::floor (sampleRate / maxHz));
    tauMin = juce::jmin (tauMin, tauMax - 4);

    integration = tauMax;
    frameLength = integration + tauMax;
    fftSize = juce::nextPowerOfTwo (frameLength);
    fftOrder = juce::roundToInt (std::log2 ((double) fftSize));
}

void PitchDetector::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writePos = 0;
}

PitchDetector::Result PitchDetector::analyse()
{
    Result result;
    const int L = frameLength;
    const int N = integration;

    // Unwrap the most recent L samples from the ring buffer.
    int start = writePos - L;
    if (start < 0)
        start += ringSize;

    for (int i = 0; i < L; ++i)
    {
        int idx = start + i;
        if (idx >= ringSize)
            idx -= ringSize;
        frame[(size_t) i] = ring[(size_t) idx];
    }

    prefix[0] = 0.0;
    for (int i = 0; i < L; ++i)
        prefix[(size_t) i + 1] = prefix[(size_t) i] + (double) frame[(size_t) i] * frame[(size_t) i];

    // Level from the most recent N samples (more responsive than the whole frame).
    const double recentEnergy = (prefix[(size_t) L] - prefix[(size_t) (L - N)]) / (double) N;
    result.rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (recentEnergy), -120.0f);

    if (prefix[(size_t) N] < 1.0e-12)
        return result;

    // Cross-correlation c(tau) = sum_{j<N} x[j] * x[j+tau] via FFT.
    auto& fft = *ffts[(size_t) fftOrder];
    const int M = fftSize;

    std::fill (bufA.begin(), bufA.begin() + 2 * M, 0.0f);
    std::fill (bufB.begin(), bufB.begin() + 2 * M, 0.0f);
    std::copy (frame.begin(), frame.begin() + N, bufA.begin());
    std::copy (frame.begin(), frame.begin() + L, bufB.begin());

    fft.performRealOnlyForwardTransform (bufA.data(), true);
    fft.performRealOnlyForwardTransform (bufB.data(), true);

    for (int k = 0; k <= M / 2; ++k)
    {
        const float ar = bufA[(size_t) (2 * k)], ai = bufA[(size_t) (2 * k + 1)];
        const float br = bufB[(size_t) (2 * k)], bi = bufB[(size_t) (2 * k + 1)];
        bufA[(size_t) (2 * k)]     = ar * br + ai * bi; // conj(A) * B
        bufA[(size_t) (2 * k + 1)] = ar * bi - ai * br;
    }

    fft.performRealOnlyInverseTransform (bufA.data());

    // Normalise using c(0), which must equal the energy of the first N samples.
    // This makes the result independent of the FFT engine's scaling convention.
    const double energyA = prefix[(size_t) N];
    const double c0 = bufA[0];
    if (std::abs (c0) < 1.0e-20)
        return result;
    const double scale = energyA / c0;

    // Cumulative mean normalised difference function.
    cmnd[0] = 1.0f;
    double runningSum = 0.0;
    for (int tau = 1; tau < tauMax; ++tau)
    {
        const double energyB = prefix[(size_t) (tau + N)] - prefix[(size_t) tau];
        const double d = juce::jmax (0.0, energyA + energyB - 2.0 * scale * bufA[(size_t) tau]);
        runningSum += d;
        cmnd[(size_t) tau] = runningSum > 0.0 ? (float) (d * tau / runningSum) : 1.0f;
    }

    // Absolute threshold: first dip below threshold, then walk down to its local minimum.
    int best = -1;
    for (int tau = tauMin; tau < tauMax - 1; ++tau)
    {
        if (cmnd[(size_t) tau] < threshold)
        {
            while (tau + 1 < tauMax - 1 && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        // Nothing under the threshold: report the global minimum as an unpitched estimate.
        best = tauMin;
        for (int tau = tauMin + 1; tau < tauMax - 1; ++tau)
            if (cmnd[(size_t) tau] < cmnd[(size_t) best])
                best = tau;
    }
    else
    {
        result.pitched = true;
    }

    // Parabolic interpolation around the minimum.
    double refined = best;
    if (best > 1 && best < tauMax - 1)
    {
        const double y0 = cmnd[(size_t) best - 1], y1 = cmnd[(size_t) best], y2 = cmnd[(size_t) best + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs (denom) > 1.0e-12)
            refined += juce::jlimit (-1.0, 1.0, 0.5 * (y0 - y2) / denom);
    }

    result.frequency = (float) (sampleRate / refined);
    result.confidence = juce::jlimit (0.0f, 1.0f, 1.0f - cmnd[(size_t) best]);
    return result;
}
