#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

/**
    Monophonic pitch detector based on the YIN algorithm (de Cheveigné & Kawahara, 2002).

    The difference function is computed with an FFT-based cross-correlation, so the cost per
    analysis is a few FFTs regardless of the lowest detectable frequency. All memory is
    allocated in prepare(); push()/analyse()/setRange() are real-time safe.
*/
class PitchDetector
{
public:
    struct Result
    {
        float frequency  = 0.0f;    // Hz, 0 when nothing was found
        float confidence = 0.0f;    // 1 - CMNDF value at the chosen lag (1 = perfectly periodic)
        float rmsDb      = -120.0f; // level of the analysis frame
        bool  pitched    = false;   // a lag below the threshold was found
    };

    /** Allocates for the lowest frequency that may ever be requested via setRange(). */
    void prepare (double sampleRate, float lowestPossibleHz = 25.0f);

    /** Sets the detection range. Values are clamped to what prepare() allocated. */
    void setRange (float minHz, float maxHz);

    /** YIN absolute threshold (typically 0.1 - 0.2). Higher = more permissive. */
    void setThreshold (float t) noexcept { threshold = t; }

    void reset();

    void push (float sample) noexcept
    {
        ring[(size_t) writePos] = sample;
        if (++writePos == ringSize)
            writePos = 0;
    }

    /** Analyses the most recent getFrameLength() samples. */
    Result analyse();

    int getFrameLength() const noexcept { return frameLength; }
    double getSampleRate() const noexcept { return sampleRate; }

private:
    double sampleRate = 44100.0;
    float lowestAllocatedHz = 25.0f;
    float threshold = 0.15f;

    int maxTau = 0;       // allocated maximum lag
    int tauMin = 2, tauMax = 0;
    int integration = 0;  // YIN integration window (N)
    int frameLength = 0;  // N + tauMax
    int fftOrder = 0, fftSize = 0;

    std::vector<std::unique_ptr<juce::dsp::FFT>> ffts; // indexed by order
    std::vector<float> ring, frame, bufA, bufB, cmnd;
    std::vector<double> prefix;
    int ringSize = 0, writePos = 0;
};
