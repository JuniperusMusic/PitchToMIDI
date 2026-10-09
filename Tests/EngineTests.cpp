// Headless tests: synthesise theremin-like audio, run the engine, decode the MIDI back into pitch.

#include "ThereminEngine.h"
#include <cstdio>
#include <functional>
#include <map>
#include <random>

namespace
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    int failures = 0;

    void check (bool ok, const char* what, double value = 0.0)
    {
        std::printf ("  [%s] %s (%.3f)\n", ok ? "PASS" : "FAIL", what, value);
        if (! ok)
            ++failures;
    }

    struct Sample { double time; float truePitch; bool noteOn; float midiPitch; };

    struct RunResult
    {
        std::vector<Sample> samples;  // one per block, decoded state at end of block
        int noteOns = 0, noteOffs = 0, rpnMsgs = 0, volumeMsgs = 0;
        std::map<int, int> channelsUsed;
        float maxStep = 0.0f;         // largest jump in decoded output pitch between events
    };

    /** pitchAt(t) returns the true MIDI pitch (<= 0 for silence), ampAt(t) the amplitude. */
    RunResult run (double seconds, std::function<float (double)> pitchAt, std::function<float (double)> ampAt,
                   const ThereminEngine::Settings& s, float harmonic2 = 0.0f, float harmonic3 = 0.0f, float noise = 0.0f)
    {
        ThereminEngine engine;
        engine.prepare (sr);

        RunResult res;
        std::mt19937 rng (1);
        std::normal_distribution<float> gauss (0.0f, 1.0f);
        double phase = 0.0;
        const int totalSamples = (int) (seconds * sr);

        struct ChannelState { int note = -1; int bend = 8192; };
        std::array<ChannelState, 17> ch {};
        int soundingChannel = -1;
        float lastDecoded = -1.0f;

        auto decoded = [&]() -> float
        {
            if (soundingChannel < 0 || ch[(size_t) soundingChannel].note < 0)
                return -1.0f;
            auto& c = ch[(size_t) soundingChannel];
            return (float) c.note + (float) (c.bend - 8192) / 8192.0f * (float) s.midi.bendRange;
        };

        std::vector<float> buffer (blockSize);
        for (int pos = 0; pos < totalSamples; pos += blockSize)
        {
            const int n = std::min (blockSize, totalSamples - pos);
            for (int i = 0; i < n; ++i)
            {
                const double t = (pos + i) / sr;
                const float p = pitchAt (t);
                const double f = p > 0.0f ? 440.0 * std::pow (2.0, (p - 69.0) / 12.0) : 0.0;
                phase += 2.0 * juce::MathConstants<double>::pi * f / sr;
                const float a = p > 0.0f ? ampAt (t) : 0.0f;
                buffer[(size_t) i] = a * (float) (std::sin (phase) + harmonic2 * std::sin (2 * phase) + harmonic3 * std::sin (3 * phase))
                                   + noise * gauss (rng);
            }

            juce::MidiBuffer midi;
            engine.process (buffer.data(), n, midi, s);

            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                const int c = m.getChannel();
                if (m.isNoteOn())
                {
                    ++res.noteOns;
                    ch[(size_t) c].note = m.getNoteNumber();
                    soundingChannel = c;
                    res.channelsUsed[c]++;
                }
                else if (m.isNoteOff())
                {
                    ++res.noteOffs;
                    if (ch[(size_t) c].note == m.getNoteNumber())
                        ch[(size_t) c].note = -1;
                }
                else if (m.isPitchWheel())
                {
                    ch[(size_t) c].bend = m.getPitchWheelValue();
                }
                else if (m.isController() && (m.getControllerNumber() == 101 || m.getControllerNumber() == 100))
                {
                    ++res.rpnMsgs;
                }
                else if (m.isController() || m.isChannelPressure())
                {
                    ++res.volumeMsgs;
                }

                // Only measure continuity after the whole timestamp group is applied.
            }

            const float d = decoded();
            if (d > 0.0f && lastDecoded > 0.0f)
                res.maxStep = std::max (res.maxStep, std::abs (d - lastDecoded));
            lastDecoded = d;

            const double tEnd = (pos + n) / sr;
            res.samples.push_back ({ tEnd, pitchAt (tEnd), d > 0.0f, d });
        }
        return res;
    }

    /** RMS / max error in cents between output and true pitch, with the best latency between 0 and 80 ms. */
    std::pair<double, double> pitchError (const RunResult& r, std::function<float (double)> pitchAt, double fromT, double toT,
                                          double* bestLatencyOut = nullptr)
    {
        double bestRms = 1e9, bestMax = 0, bestLat = 0;
        for (double lat = 0.0; lat <= 0.08; lat += 0.001)
        {
            double sum = 0, mx = 0;
            int count = 0;
            for (auto& s : r.samples)
            {
                if (s.time < fromT || s.time > toT || ! s.noteOn)
                    continue;
                const float truth = pitchAt (s.time - lat);
                if (truth <= 0.0f)
                    continue;
                const double e = (s.midiPitch - truth) * 100.0;
                sum += e * e;
                mx = std::max (mx, std::abs (e));
                ++count;
            }
            if (count == 0)
                continue;
            const double rms = std::sqrt (sum / count);
            if (rms < bestRms) { bestRms = rms; bestMax = mx; bestLat = lat; }
        }
        if (bestLatencyOut) *bestLatencyOut = bestLat;
        return { bestRms, bestMax };
    }

    ThereminEngine::Settings defaults()
    {
        ThereminEngine::Settings s;
        s.minHz = 60.0f;
        s.maxHz = 2500.0f;
        s.gateDb = -50.0f;
        s.smoothingMs = 6.0f;
        s.midi.bendRange = 48;
        return s;
    }

    auto constAmp = [] (double) { return 0.5f; };
}

int main()
{
    {
        std::puts ("Steady A4 (440 Hz):");
        auto pitch = [] (double) { return 69.0f; };
        auto r = run (1.0, pitch, constAmp, defaults());
        auto [rms, mx] = pitchError (r, pitch, 0.2, 1.0);
        check (r.noteOns == 1, "exactly one note-on", r.noteOns);
        check (rms < 1.0, "RMS error < 1 cent", rms);
        check (r.rpnMsgs > 0, "bend range RPN sent", r.rpnMsgs);
    }

    for (float f : { 65.4f, 130.8f, 1046.5f, 2093.0f })
    {
        std::printf ("Steady %.1f Hz with harmonics + noise:\n", f);
        const float p = 69.0f + 12.0f * std::log2 (f / 440.0f);
        auto pitch = [p] (double) { return p; };
        auto r = run (1.0, pitch, constAmp, defaults(), 0.4f, 0.2f, 0.01f);
        auto [rms, mx] = pitchError (r, pitch, 0.25, 1.0);
        check (r.noteOns == 1, "exactly one note-on", r.noteOns);
        check (mx < 5.0, "max error < 5 cents (no octave errors)", mx);
    }

    auto gliss = [] (double t) { return t < 0.1 ? 45.0f : (float) (45.0 + 36.0 * std::min (1.0, (t - 0.1) / 3.0)); };

    {
        std::puts ("3-octave glissando, mono, +/-48 bend:");
        auto r = run (3.5, gliss, constAmp, defaults(), 0.3f, 0.1f, 0.005f);
        double lat = 0;
        auto [rms, mx] = pitchError (r, gliss, 0.3, 3.4, &lat);
        check (r.noteOns == 1, "single note-on (whole slide via bend)", r.noteOns);
        check (rms < 10.0, "RMS tracking error < 10 cents", rms);
        check (r.maxStep < 0.15f, "no jumps > 15 cents between blocks", r.maxStep * 100);
        std::printf ("  estimated latency: %.0f ms\n", lat * 1000);
    }

    {
        std::puts ("3-octave glissando, mono, +/-2 bend (legato retriggers):");
        auto s = defaults();
        s.midi.bendRange = 2;
        auto r = run (3.5, gliss, constAmp, s);
        auto [rms, mx] = pitchError (r, gliss, 0.3, 3.4);
        check (r.noteOns > 10, "retriggers when leaving bend range", r.noteOns);
        check (r.noteOffs == r.noteOns - 1, "each retrigger releases the old note (one still held)", r.noteOffs);
        check (rms < 10.0, "RMS tracking error < 10 cents", rms);
        check (r.maxStep < 0.15f, "continuous across retriggers", r.maxStep * 100);
    }

    {
        std::puts ("Fixed anchor note C4, +/-48:");
        auto s = defaults();
        s.midi.fixedAnchor = true;
        s.midi.anchorNote = 60;
        auto r = run (3.5, gliss, constAmp, s);
        check (r.noteOns == 1, "single note-on", r.noteOns);
        auto [rms, mx] = pitchError (r, gliss, 0.3, 3.4);
        check (rms < 10.0, "RMS tracking error < 10 cents", rms);
    }

    // Wobbly pitches near notes, as a player would produce.
    auto wobbly = [] (double t)
    {
        const float targets[] = { 60.2f, 62.8f, 64.1f, 66.9f };
        const int idx = std::min (3, (int) (t / 0.5));
        return targets[idx] + 0.15f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t);
    };

    {
        std::puts ("Note glide mode, chromatic, 100% correction:");
        auto s = defaults();
        s.pitchMode = ThereminEngine::PitchMode::noteGlide;
        s.glideMs = 200.0f;
        auto r = run (2.0, wobbly, constAmp, s);
        bool allOnNotes = true;
        float worst = 0;
        for (auto& smp : r.samples)
        {
            const double inSegment = std::fmod (smp.time, 0.5);
            if (smp.noteOn && smp.time > 0.2 && inSegment > 0.3)
            {
                const float off = std::abs (smp.midiPitch - std::round (smp.midiPitch));
                worst = std::max (worst, off);
                allOnNotes &= off < 0.01f;
            }
        }
        check (allOnNotes, "settles exactly on semitones (max off, cents)", worst * 100);
        // Smoothstep peak slope is 1.5x the average: 3 st over 200 ms -> ~12 cents per 5.3 ms block.
        check (r.maxStep < 0.2f, "transitions glide rather than jump (max cents/block)", r.maxStep * 100);
    }

    {
        std::puts ("Note glide mode, C major scale:");
        auto s = defaults();
        s.pitchMode = ThereminEngine::PitchMode::noteGlide;
        s.scale = 1;
        s.key = 0;
        auto r = run (2.0, wobbly, constAmp, s);
        bool inScale = true;
        for (auto& smp : r.samples)
            if (smp.noteOn && smp.time > 0.2 && std::fmod (smp.time, 0.5) > 0.25)
            {
                const int n = ((int) std::round (smp.midiPitch)) % 12;
                inScale &= (n == 0 || n == 2 || n == 4 || n == 5 || n == 7 || n == 9 || n == 11);
            }
        check (inScale, "all settled notes in C major");
    }

    {
        std::puts ("Silence gap -> note off, then new note:");
        auto pitch = [] (double t) { return (t > 0.5 && t < 0.8) ? 0.0f : 64.0f; };
        auto r = run (1.3, pitch, constAmp, defaults());
        check (r.noteOns == 2, "two note-ons", r.noteOns);
        check (r.noteOffs == 1, "one note-off", r.noteOffs);
    }

    {
        std::puts ("Hold note through silence:");
        auto s = defaults();
        s.holdNote = true;
        auto pitch = [] (double t) { return (t > 0.5 && t < 0.8) ? 0.0f : 64.0f; };
        auto r = run (1.3, pitch, constAmp, s);
        check (r.noteOns == 1 && r.noteOffs == 0, "note held, no retrigger", r.noteOns);
        check (r.volumeMsgs > 5, "volume CC follows level", r.volumeMsgs);
    }

    {
        std::puts ("MPE, +/-2 bend so the slide spans several notes:");
        auto s = defaults();
        s.midi.mpe = true;
        s.midi.bendRange = 2;
        auto r = run (3.5, gliss, constAmp, s);
        bool channelsOk = r.channelsUsed.count (1) == 0;
        for (auto& [c, n] : r.channelsUsed)
            channelsOk &= c >= 2 && c <= 16;
        check (channelsOk, "notes only on member channels 2-16", (double) r.channelsUsed.size());
        check (r.channelsUsed.size() > 5, "channels rotate", (double) r.channelsUsed.size());
        auto [rms, mx] = pitchError (r, gliss, 0.3, 3.4);
        check (rms < 10.0, "RMS tracking error < 10 cents", rms);
        check (r.rpnMsgs >= 2 * 16, "MPE configuration + bend range RPNs sent", r.rpnMsgs);
    }

    {
        std::puts ("Below gate -> no notes:");
        auto pitch = [] (double) { return 64.0f; };
        auto r = run (1.0, pitch, [] (double) { return 0.001f; }, defaults());
        check (r.noteOns == 0, "no notes for -60 dB input", r.noteOns);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
