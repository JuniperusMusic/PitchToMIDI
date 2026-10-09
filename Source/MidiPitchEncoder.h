#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/**
    Turns a continuous (fractional MIDI note) pitch into MIDI note + 14-bit pitch-bend messages.

    Mono mode:  one channel, one held note, pitch bend carries all movement. When the pitch
                leaves the bend range a new note is started legato (new note-on before old note-off).
    MPE mode:   lower zone (master channel 1, member channels 2..N+1). Each note gets its own
                member channel, so the release tail of a previous note keeps its own pitch.
*/
class MidiPitchEncoder
{
public:
    enum class VolumeMode { off = 0, cc1, cc2, cc7, cc11, cc74, channelPressure };

    struct Config
    {
        bool mpe = false;
        int channel = 1;          // mono mode, 1..16
        int bendRange = 48;       // semitones, must match the synth
        bool fixedAnchor = false; // always play anchorNote and reach everything with bend
        int anchorNote = 60;
        int mpeMembers = 15;      // 1..15
        VolumeMode volumeMode = VolumeMode::cc11;

        bool operator== (const Config& o) const noexcept
        {
            return mpe == o.mpe && channel == o.channel && bendRange == o.bendRange
                && fixedAnchor == o.fixedAnchor && anchorNote == o.anchorNote
                && mpeMembers == o.mpeMembers && volumeMode == o.volumeMode;
        }
        bool operator!= (const Config& o) const noexcept { return ! (*this == o); }
    };

    void setConfig (const Config& c) noexcept { config = c; }
    const Config& getConfig() const noexcept { return config; }

    /** Sends RPN 0 (pitch-bend sensitivity) and, in MPE mode, the MPE configuration message. */
    void sendSetup (juce::MidiBuffer& out, int pos) const;

    void noteStart (float pitch, int velocity, juce::MidiBuffer& out, int pos);
    void pitchUpdate (float pitch, juce::MidiBuffer& out, int pos);
    void noteEnd (juce::MidiBuffer& out, int pos);

    /** 0..127; only sends when the value changed. */
    void setVolume (int value, juce::MidiBuffer& out, int pos);

    bool isNoteOn() const noexcept { return noteOn; }
    int getCurrentNote() const noexcept { return currentNote; }
    int getCurrentChannel() const noexcept { return currentChannel; }
    int getLastBend() const noexcept { return lastBend; }

    void reset() noexcept;

private:
    int bendValueFor (float offsetSemitones) const noexcept;
    int nextMpeChannel() noexcept;
    void sendBend (int channel, float offset, juce::MidiBuffer& out, int pos, bool force);
    void sendVolumeOn (int channel, int value, juce::MidiBuffer& out, int pos) const;
    void startOn (int channel, int note, float pitch, juce::MidiBuffer& out, int pos);

    Config config;
    bool noteOn = false;
    int currentNote = -1;
    int currentChannel = 1;
    int lastMpeChannel = 1;
    int lastBend = 8192;
    int lastVelocity = 100;
    int lastVolume = -1;
};
