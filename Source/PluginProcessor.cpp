#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

ThereBendProcessor::ThereBendProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ThereBend", createParameterLayout())
{
}

AudioProcessorValueTreeState::ParameterLayout ThereBendProcessor::createParameterLayout()
{
    using P = ParameterID;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto ms = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto db = [] (float v, int) { return String (v, 1) + " dB"; };
    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto st = [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 2) + " st"; };

    auto floatParam = [&] (const char* id, const char* name, NormalisableRange<float> range, float def,
                           std::function<String (float, int)> toText)
    {
        layout.add (std::make_unique<AudioParameterFloat> (P { id, 1 }, name, range, def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (toText)));
    };

    auto skewed = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };

    // Detection
    floatParam (ParamIDs::minHz, "Lowest pitch", skewed (30.0f, 500.0f, 100.0f), 60.0f, hz);
    floatParam (ParamIDs::maxHz, "Highest pitch", skewed (200.0f, 5000.0f, 1500.0f), 2500.0f, hz);
    floatParam (ParamIDs::threshold, "Detection tolerance", { 0.05f, 0.5f, 0.01f }, 0.15f,
                [] (float v, int) { return String (v, 2); });
    floatParam (ParamIDs::gateDb, "Gate", { -80.0f, -10.0f, 0.1f }, -50.0f, db);
    floatParam (ParamIDs::releaseMs, "Release hold", { 0.0f, 500.0f, 1.0f }, 40.0f, ms);
    floatParam (ParamIDs::tuningA4, "Tuning (A4)", { 415.0f, 466.0f, 0.1f }, 440.0f,
                [] (float v, int) { return String (v, 1) + " Hz"; });
    floatParam (ParamIDs::transpose, "Transpose", { -48.0f, 48.0f, 1.0f }, 0.0f, st);
    layout.add (std::make_unique<AudioParameterBool> (P { ParamIDs::audioThru, 1 }, "Audio through", true));

    // Pitch
    layout.add (std::make_unique<AudioParameterChoice> (P { ParamIDs::pitchMode, 1 }, "Pitch mode",
                                                        StringArray { "Continuous", "Note glide" }, 0));
    floatParam (ParamIDs::correction, "Note correction", { 0.0f, 1.0f, 0.01f }, 1.0f, pct);
    floatParam (ParamIDs::glideMs, "Glide time", skewed (0.0f, 2000.0f, 150.0f), 80.0f, ms);
    floatParam (ParamIDs::smoothingMs, "Smoothing", skewed (0.0f, 300.0f, 30.0f), 6.0f, ms);
    layout.add (std::make_unique<AudioParameterChoice> (P { ParamIDs::scale, 1 }, "Scale", ThereminEngine::getScaleNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (P { ParamIDs::key, 1 }, "Key",
                                                        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    floatParam (ParamIDs::hysteresis, "Snap hysteresis", { 0.0f, 50.0f, 1.0f }, 15.0f,
                [] (float v, int) { return String (roundToInt (v)) + " cents"; });

    // Output
    layout.add (std::make_unique<AudioParameterChoice> (P { ParamIDs::outputMode, 1 }, "Output mode",
                                                        StringArray { "Mono + pitch bend", "MPE" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (P { ParamIDs::channel, 1 }, "MIDI channel", 1, 16, 1));
    layout.add (std::make_unique<AudioParameterInt> (P { ParamIDs::bendRange, 1 }, "Bend range", 1, 96, 48,
                                                     AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return "+/-" + String (v) + " st"; })));
    layout.add (std::make_unique<AudioParameterBool> (P { ParamIDs::fixedAnchor, 1 }, "Fixed anchor note", false));
    layout.add (std::make_unique<AudioParameterInt> (P { ParamIDs::anchorNote, 1 }, "Anchor note", 0, 127, 60,
                                                     AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 3); })));
    layout.add (std::make_unique<AudioParameterInt> (P { ParamIDs::mpeMembers, 1 }, "MPE channels", 1, 15, 15));
    layout.add (std::make_unique<AudioParameterBool> (P { ParamIDs::sendRpn, 1 }, "Send bend range (RPN)", true));
    layout.add (std::make_unique<AudioParameterBool> (P { ParamIDs::holdNote, 1 }, "Hold note through silence", false));
    layout.add (std::make_unique<AudioParameterChoice> (P { ParamIDs::velMode, 1 }, "Velocity mode",
                                                        StringArray { "Fixed", "From level" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (P { ParamIDs::velocity, 1 }, "Velocity", 1, 127, 100));
    layout.add (std::make_unique<AudioParameterChoice> (P { ParamIDs::volumeOut, 1 }, "Volume output",
                                                        StringArray { "Off", "CC1 Mod wheel", "CC2 Breath", "CC7 Volume",
                                                                      "CC11 Expression", "CC74 Brightness", "Channel pressure" }, 4));
    floatParam (ParamIDs::levelMaxDb, "Full-volume level", { -60.0f, 0.0f, 0.1f }, -12.0f, db);

    return layout;
}

bool ThereBendProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (in == AudioChannelSet::mono() || in == AudioChannelSet::stereo());
}

void ThereBendProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    monoBuffer.setSize (1, samplesPerBlock);
}

ThereminEngine::Settings ThereBendProcessor::readSettings() const
{
    ThereminEngine::Settings s;
    s.minHz = get (ParamIDs::minHz);
    s.maxHz = get (ParamIDs::maxHz);
    s.threshold = get (ParamIDs::threshold);
    s.gateDb = get (ParamIDs::gateDb);
    s.releaseMs = get (ParamIDs::releaseMs);
    s.tuningA4 = get (ParamIDs::tuningA4);
    s.transpose = get (ParamIDs::transpose);

    s.pitchMode = (ThereminEngine::PitchMode) roundToInt (get (ParamIDs::pitchMode));
    s.correction = get (ParamIDs::correction);
    s.glideMs = get (ParamIDs::glideMs);
    s.smoothingMs = get (ParamIDs::smoothingMs);
    s.scale = roundToInt (get (ParamIDs::scale));
    s.key = roundToInt (get (ParamIDs::key));
    s.hysteresisCents = get (ParamIDs::hysteresis);

    s.midi.mpe = roundToInt (get (ParamIDs::outputMode)) == 1;
    s.midi.channel = roundToInt (get (ParamIDs::channel));
    s.midi.bendRange = roundToInt (get (ParamIDs::bendRange));
    s.midi.fixedAnchor = get (ParamIDs::fixedAnchor) > 0.5f;
    s.midi.anchorNote = roundToInt (get (ParamIDs::anchorNote));
    s.midi.mpeMembers = roundToInt (get (ParamIDs::mpeMembers));
    s.midi.volumeMode = (MidiPitchEncoder::VolumeMode) roundToInt (get (ParamIDs::volumeOut));

    s.sendRpn = get (ParamIDs::sendRpn) > 0.5f;
    s.holdNote = get (ParamIDs::holdNote) > 0.5f;
    s.velocityMode = (ThereminEngine::VelocityMode) roundToInt (get (ParamIDs::velMode));
    s.velocity = roundToInt (get (ParamIDs::velocity));
    s.levelMaxDb = get (ParamIDs::levelMaxDb);
    return s;
}

void ThereBendProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    midi.clear();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = getTotalNumInputChannels();

    if (monoBuffer.getNumSamples() < numSamples)
        monoBuffer.setSize (1, numSamples, false, false, true); // only if the host exceeds its announced block size

    // Analyse a mono sum of the input.
    auto* mono = monoBuffer.getWritePointer (0);
    if (numChannels <= 1)
    {
        FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);
    }
    else
    {
        FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);
        for (int ch = 1; ch < numChannels; ++ch)
            FloatVectorOperations::add (mono, buffer.getReadPointer (ch), numSamples);
        FloatVectorOperations::multiply (mono, 1.0f / (float) numChannels, numSamples);
    }

    engine.process (mono, numSamples, midi, readSettings());

    if (get (ParamIDs::audioThru) < 0.5f)
        buffer.clear();
}

void ThereBendProcessor::processBlockBypassed (AudioBuffer<float>&, MidiBuffer& midi)
{
    midi.clear();
    engine.allNotesOff (midi, 0);
}

AudioProcessorEditor* ThereBendProcessor::createEditor()
{
    return new ThereBendEditor (*this);
}

void ThereBendProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ThereBendProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ThereBendProcessor();
}
