#include "ThereminEngine.h"

namespace
{
    struct ScaleDef { const char* name; std::initializer_list<int> steps; };

    const ScaleDef scaleDefs[] =
    {
        { "Chromatic",        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "Major",            { 0, 2, 4, 5, 7, 9, 11 } },
        { "Natural minor",    { 0, 2, 3, 5, 7, 8, 10 } },
        { "Harmonic minor",   { 0, 2, 3, 5, 7, 8, 11 } },
        { "Melodic minor",    { 0, 2, 3, 5, 7, 9, 11 } },
        { "Dorian",           { 0, 2, 3, 5, 7, 9, 10 } },
        { "Mixolydian",       { 0, 2, 4, 5, 7, 9, 10 } },
        { "Major pentatonic", { 0, 2, 4, 7, 9 } },
        { "Minor pentatonic", { 0, 3, 5, 7, 10 } },
        { "Blues",            { 0, 3, 5, 6, 7, 10 } },
        { "Whole tone",       { 0, 2, 4, 6, 8, 10 } },
    };

    constexpr int numScales = (int) (sizeof (scaleDefs) / sizeof (scaleDefs[0]));

    int scaleMask (int index)
    {
        int mask = 0;
        for (int step : scaleDefs[juce::jlimit (0, numScales - 1, index)].steps)
            mask |= 1 << step;
        return mask;
    }

    float onePoleCoeff (float ms, float dt)
    {
        return ms <= 0.0f ? 1.0f : 1.0f - std::exp (-dt / (ms * 0.001f));
    }
}

const juce::StringArray& ThereminEngine::getScaleNames()
{
    static const juce::StringArray names = []
    {
        juce::StringArray n;
        for (auto& s : scaleDefs)
            n.add (s.name);
        return n;
    }();
    return names;
}

void ThereminEngine::prepare (double sr)
{
    sampleRate = sr;
    detector.prepare (sr, 25.0f);
    hopSize = juce::jmax (32, juce::roundToInt (sr * 0.0025)); // ~2.5 ms update rate
    hopSeconds = (float) (hopSize / sr);
    hopCounter = 0;

    encoder.reset();
    lastMinHz = lastMaxHz = -1.0f;
    firstBlock = true;
    setupPending = true;
    gateOpen = active = false;
    voicedHops = 0;
    historyCount = 0;
    unvoicedTime = 0.0f;
    volumeSmoothed = 0.0f;
}

void ThereminEngine::allNotesOff (juce::MidiBuffer& out, int pos)
{
    encoder.noteEnd (out, pos);
    active = false;
    display.noteActive = false;
}

float ThereminEngine::median3 (float x) noexcept
{
    history[0] = history[1];
    history[1] = history[2];
    history[2] = x;
    historyCount = juce::jmin (3, historyCount + 1);

    if (historyCount < 3)
        return x;

    const float a = history[0], b = history[1], c = history[2];
    return juce::jmax (juce::jmin (a, b), juce::jmin (juce::jmax (a, b), c));
}

float ThereminEngine::snapToScale (float pitch, const Settings& s)
{
    const int mask = scaleMask (s.scale);
    auto inScale = [&] (int note) { return ((mask >> (((note - s.key) % 12 + 12) % 12)) & 1) != 0; };

    const int base = juce::roundToInt (pitch);
    int nearest = base;
    float bestDist = 1.0e9f;
    for (int d = -12; d <= 12; ++d)
    {
        const int n = base + d;
        const float dist = std::abs (pitch - (float) n);
        if (inScale (n) && dist < bestDist)
        {
            bestDist = dist;
            nearest = n;
        }
    }

    // Hysteresis: only leave the current note once the new one is clearly closer.
    const float hyst = s.hysteresisCents * 0.01f;
    if (snappedNote > -1000 && inScale (snappedNote)
        && std::abs (pitch - (float) snappedNote) <= bestDist + hyst)
        return (float) snappedNote;

    snappedNote = nearest;
    return (float) nearest;
}

void ThereminEngine::process (const float* input, int numSamples, juce::MidiBuffer& out, const Settings& s)
{
    if (! juce::exactlyEqual (s.minHz, lastMinHz) || ! juce::exactlyEqual (s.maxHz, lastMaxHz))
    {
        detector.setRange (s.minHz, s.maxHz);
        lastMinHz = s.minHz;
        lastMaxHz = s.maxHz;
    }
    detector.setThreshold (s.threshold);

    if (firstBlock || s.midi != lastConfig)
    {
        if (encoder.isNoteOn())
            encoder.noteEnd (out, 0);
        active = false;
        encoder.setConfig (s.midi);
        lastConfig = s.midi;
        setupPending = true;
        firstBlock = false;
    }

    if (setupPending && s.sendRpn)
    {
        encoder.sendSetup (out, 0);
        setupPending = false;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        detector.push (input[i]);
        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;
            analyseHop (out, i, s);
        }
    }
}

void ThereminEngine::analyseHop (juce::MidiBuffer& out, int pos, const Settings& s)
{
    const auto r = detector.analyse();
    const float dt = hopSeconds;

    // Level gate with 3 dB hysteresis.
    gateOpen = gateOpen ? r.rmsDb > s.gateDb - 3.0f : r.rmsDb > s.gateDb;
    const bool voiced = gateOpen && r.pitched && r.frequency > 0.0f;

    float rawPitch = display.inputPitch.load();
    if (voiced)
    {
        rawPitch = 69.0f + 12.0f * std::log2 (r.frequency / s.tuningA4) + s.transpose;
        rawPitch = median3 (rawPitch);
        ++voicedHops;
        unvoicedTime = 0.0f;
    }
    else
    {
        voicedHops = 0;
        historyCount = 0;
        unvoicedTime += dt;
    }

    // Volume (expression) from input level.
    const float levelRange = juce::jmax (1.0f, s.levelMaxDb - s.gateDb);
    const float levelNorm = gateOpen ? juce::jlimit (0.0f, 1.0f, (r.rmsDb - s.gateDb) / levelRange) : 0.0f;
    volumeSmoothed += (levelNorm - volumeSmoothed) * onePoleCoeff (10.0f, dt);
    const int volume = juce::roundToInt (volumeSmoothed * 127.0f);
    encoder.setVolume (volume, out, pos);

    const float correction = s.pitchMode == PitchMode::noteGlide ? juce::jlimit (0.0f, 1.0f, s.correction) : 0.0f;

    auto shapePitch = [&] (float raw)
    {
        smoothedPitch += (raw - smoothedPitch) * onePoleCoeff (s.smoothingMs, dt);

        const float target = snapToScale (raw, s);
        if (! juce::exactlyEqual (target, glideTo))
        {
            glideFrom = glideValue;
            glideTo = target;
            glidePos = 0.0f;
        }
        glidePos = s.glideMs > 0.0f ? juce::jmin (1.0f, glidePos + dt / (s.glideMs * 0.001f)) : 1.0f;
        const float t = glidePos * glidePos * (3.0f - 2.0f * glidePos); // smoothstep
        glideValue = glideFrom + (glideTo - glideFrom) * t;

        return smoothedPitch + (glideValue - smoothedPitch) * correction;
    };

    if (! active)
    {
        if (voiced && voicedHops >= 2)
        {
            smoothedPitch = rawPitch;
            snappedNote = -1000;
            glideValue = glideFrom = glideTo = snapToScale (rawPitch, s);
            glidePos = 1.0f;
            lastOutput = shapePitch (rawPitch);

            const int velocity = s.velocityMode == VelocityMode::fromLevel
                                   ? juce::jlimit (1, 127, 1 + juce::roundToInt (levelNorm * 126.0f))
                                   : juce::jlimit (1, 127, s.velocity);

            if (s.sendRpn && ! s.midi.mpe)
                encoder.sendSetup (out, pos); // cheap insurance in case the synth missed it

            encoder.noteStart (lastOutput, velocity, out, pos);
            active = true;
        }
    }
    else
    {
        if (! voiced && ! s.holdNote && unvoicedTime * 1000.0f >= s.releaseMs)
        {
            encoder.noteEnd (out, pos);
            active = false;
        }
        else
        {
            // While briefly unvoiced the last pitch is held, but glides still complete.
            lastOutput = shapePitch (rawPitch);
            encoder.pitchUpdate (lastOutput, out, pos);
        }
    }

    display.detectedHz = r.pitched ? r.frequency : 0.0f;
    display.confidence = r.confidence;
    display.levelDb = r.rmsDb;
    display.inputPitch = rawPitch;
    display.outputPitch = lastOutput;
    display.voiced = voiced;
    display.noteActive = active;
    display.midiNote = encoder.isNoteOn() ? encoder.getCurrentNote() : -1;
    display.bend = encoder.getLastBend();
    display.volume = volume;
}
