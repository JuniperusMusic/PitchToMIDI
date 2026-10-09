#pragma once

#include "PitchDetector.h"
#include "MidiPitchEncoder.h"
#include <array>
#include <atomic>

/**
    Audio in -> pitch detection -> pitch shaping (smoothing, scale snapping, glide) -> MIDI out.
    Independent of the plugin wrapper so it can be unit-tested.
*/
class ThereminEngine
{
public:
    enum class PitchMode { continuous = 0, noteGlide };
    enum class VelocityMode { fixed = 0, fromLevel };

    struct Settings
    {
        // Detection
        float minHz = 60.0f, maxHz = 2000.0f;
        float threshold = 0.15f;
        float gateDb = -45.0f;
        float releaseMs = 40.0f;
        float tuningA4 = 440.0f;
        float transpose = 0.0f;

        // Pitch shaping
        PitchMode pitchMode = PitchMode::continuous;
        float correction = 1.0f;     // 0..1, only in noteGlide mode
        float glideMs = 60.0f;
        float smoothingMs = 8.0f;
        int scale = 0;               // index into getScaleNames()
        int key = 0;                 // 0 = C
        float hysteresisCents = 15.0f;

        // Output
        MidiPitchEncoder::Config midi;
        bool sendRpn = true;
        bool holdNote = false;
        VelocityMode velocityMode = VelocityMode::fixed;
        int velocity = 100;
        float levelMaxDb = -6.0f;
    };

    /** Values the editor polls for display. */
    struct Display
    {
        std::atomic<float> detectedHz { 0.0f }, confidence { 0.0f }, levelDb { -120.0f };
        std::atomic<float> inputPitch { 0.0f }, outputPitch { 0.0f };
        std::atomic<bool> voiced { false }, noteActive { false };
        std::atomic<int> midiNote { -1 }, bend { 8192 }, volume { 0 };
    };

    static const juce::StringArray& getScaleNames();

    void prepare (double sampleRate);
    void process (const float* input, int numSamples, juce::MidiBuffer& out, const Settings& settings);

    /** Ends any sounding note (e.g. on bypass). */
    void allNotesOff (juce::MidiBuffer& out, int pos);

    int getHopSize() const noexcept { return hopSize; }
    int getFrameLength() const noexcept { return detector.getFrameLength(); }

    Display display;

private:
    void analyseHop (juce::MidiBuffer& out, int pos, const Settings& s);
    float snapToScale (float pitch, const Settings& s);
    float median3 (float x) noexcept;

    PitchDetector detector;
    MidiPitchEncoder encoder;

    double sampleRate = 44100.0;
    int hopSize = 128, hopCounter = 0;
    float hopSeconds = 0.0f;

    MidiPitchEncoder::Config lastConfig;
    bool setupPending = true, firstBlock = true;
    float lastMinHz = -1.0f, lastMaxHz = -1.0f;

    // Tracking state
    bool gateOpen = false, active = false;
    int voicedHops = 0;
    float unvoicedTime = 0.0f;
    std::array<float, 3> history {};
    int historyCount = 0;

    float smoothedPitch = 0.0f;
    int snappedNote = -1000;
    float glideFrom = 0.0f, glideTo = 0.0f, glideValue = 0.0f, glidePos = 1.0f;
    float lastOutput = 60.0f;
    float volumeSmoothed = 0.0f;
};
