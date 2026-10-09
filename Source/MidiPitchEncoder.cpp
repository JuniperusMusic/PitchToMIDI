#include "MidiPitchEncoder.h"

using juce::MidiMessage;

static void addRpn (juce::MidiBuffer& out, int pos, int channel, int rpnMsb, int rpnLsb, int value)
{
    out.addEvent (MidiMessage::controllerEvent (channel, 101, rpnMsb), pos);
    out.addEvent (MidiMessage::controllerEvent (channel, 100, rpnLsb), pos);
    out.addEvent (MidiMessage::controllerEvent (channel, 6, value), pos);
    out.addEvent (MidiMessage::controllerEvent (channel, 38, 0), pos);
    out.addEvent (MidiMessage::controllerEvent (channel, 101, 127), pos); // RPN null
    out.addEvent (MidiMessage::controllerEvent (channel, 100, 127), pos);
}

void MidiPitchEncoder::sendSetup (juce::MidiBuffer& out, int pos) const
{
    const int range = juce::jlimit (1, 96, config.bendRange);

    if (config.mpe)
    {
        addRpn (out, pos, 1, 0, 6, juce::jlimit (1, 15, config.mpeMembers)); // MPE configuration message
        for (int ch = 2; ch <= 1 + config.mpeMembers; ++ch)
            addRpn (out, pos, ch, 0, 0, range);
    }
    else
    {
        addRpn (out, pos, config.channel, 0, 0, range);
    }
}

int MidiPitchEncoder::bendValueFor (float offset) const noexcept
{
    const float range = (float) juce::jlimit (1, 96, config.bendRange);
    return juce::jlimit (0, 16383, juce::roundToInt (8192.0f + offset / range * 8192.0f));
}

int MidiPitchEncoder::nextMpeChannel() noexcept
{
    const int members = juce::jlimit (1, 15, config.mpeMembers);
    int idx = lastMpeChannel - 2 + 1;
    if (idx < 0 || idx >= members)
        idx = 0;
    lastMpeChannel = idx + 2;
    return lastMpeChannel;
}

void MidiPitchEncoder::sendBend (int channel, float offset, juce::MidiBuffer& out, int pos, bool force)
{
    const int value = bendValueFor (offset);
    if (force || value != lastBend)
    {
        out.addEvent (MidiMessage::pitchWheel (channel, value), pos);
        lastBend = value;
    }
}

void MidiPitchEncoder::sendVolumeOn (int channel, int value, juce::MidiBuffer& out, int pos) const
{
    switch (config.volumeMode)
    {
        case VolumeMode::off: break;
        case VolumeMode::cc1:  out.addEvent (MidiMessage::controllerEvent (channel, 1, value), pos); break;
        case VolumeMode::cc2:  out.addEvent (MidiMessage::controllerEvent (channel, 2, value), pos); break;
        case VolumeMode::cc7:  out.addEvent (MidiMessage::controllerEvent (channel, 7, value), pos); break;
        case VolumeMode::cc11: out.addEvent (MidiMessage::controllerEvent (channel, 11, value), pos); break;
        case VolumeMode::cc74: out.addEvent (MidiMessage::controllerEvent (channel, 74, value), pos); break;
        case VolumeMode::channelPressure: out.addEvent (MidiMessage::channelPressureChange (channel, value), pos); break;
    }
}

void MidiPitchEncoder::startOn (int channel, int note, float pitch, juce::MidiBuffer& out, int pos)
{
    // Bend (and per-note volume) go out before the note-on so the note never sounds at the wrong pitch.
    if (config.mpe && lastVolume >= 0)
        sendVolumeOn (channel, lastVolume, out, pos);

    sendBend (channel, pitch - (float) note, out, pos, true);
    out.addEvent (MidiMessage::noteOn (channel, note, (juce::uint8) juce::jlimit (1, 127, lastVelocity)), pos);
    currentChannel = channel;
    currentNote = note;
    noteOn = true;
}

void MidiPitchEncoder::noteStart (float pitch, int velocity, juce::MidiBuffer& out, int pos)
{
    if (noteOn)
        noteEnd (out, pos);

    lastVelocity = velocity;
    const int note = config.fixedAnchor ? juce::jlimit (0, 127, config.anchorNote)
                                        : juce::jlimit (0, 127, juce::roundToInt (pitch));
    const int channel = config.mpe ? nextMpeChannel() : config.channel;
    startOn (channel, note, pitch, out, pos);
}

void MidiPitchEncoder::pitchUpdate (float pitch, juce::MidiBuffer& out, int pos)
{
    if (! noteOn)
        return;

    const float range = (float) juce::jlimit (1, 96, config.bendRange);
    const float margin = juce::jmin (0.5f, range * 0.25f);
    const float offset = pitch - (float) currentNote;

    if (! config.fixedAnchor && std::abs (offset) > range - margin)
    {
        const int newNote = juce::jlimit (0, 127, juce::roundToInt (pitch));
        if (newNote != currentNote)
        {
            // Legato hand-over: new note on first, then release the old one.
            const int oldNote = currentNote, oldChannel = currentChannel;
            const int channel = config.mpe ? nextMpeChannel() : config.channel;
            startOn (channel, newNote, pitch, out, pos);
            out.addEvent (MidiMessage::noteOff (oldChannel, oldNote), pos);
            return;
        }
    }

    sendBend (currentChannel, offset, out, pos, false);
}

void MidiPitchEncoder::noteEnd (juce::MidiBuffer& out, int pos)
{
    if (! noteOn)
        return;

    out.addEvent (MidiMessage::noteOff (currentChannel, currentNote), pos);
    noteOn = false;
    currentNote = -1;
}

void MidiPitchEncoder::setVolume (int value, juce::MidiBuffer& out, int pos)
{
    value = juce::jlimit (0, 127, value);
    if (value == lastVolume || config.volumeMode == VolumeMode::off)
        return;

    lastVolume = value;

    if (config.mpe)
    {
        if (noteOn)
            sendVolumeOn (currentChannel, value, out, pos);
    }
    else
    {
        sendVolumeOn (config.channel, value, out, pos);
    }
}

void MidiPitchEncoder::reset() noexcept
{
    noteOn = false;
    currentNote = -1;
    lastBend = 8192;
    lastVolume = -1;
    lastMpeChannel = 1;
}
