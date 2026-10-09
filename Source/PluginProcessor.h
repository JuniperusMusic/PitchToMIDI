#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ThereminEngine.h"

namespace ParamIDs
{
    // Detection
    inline constexpr auto minHz       = "minHz";
    inline constexpr auto maxHz       = "maxHz";
    inline constexpr auto threshold   = "threshold";
    inline constexpr auto gateDb      = "gateDb";
    inline constexpr auto releaseMs   = "releaseMs";
    inline constexpr auto tuningA4    = "tuningA4";
    inline constexpr auto transpose   = "transpose";
    inline constexpr auto audioThru   = "audioThru";
    // Pitch
    inline constexpr auto pitchMode   = "pitchMode";
    inline constexpr auto correction  = "correction";
    inline constexpr auto glideMs     = "glideMs";
    inline constexpr auto smoothingMs = "smoothingMs";
    inline constexpr auto scale       = "scale";
    inline constexpr auto key         = "key";
    inline constexpr auto hysteresis  = "hysteresis";
    // Output
    inline constexpr auto outputMode  = "outputMode";
    inline constexpr auto channel     = "channel";
    inline constexpr auto bendRange   = "bendRange";
    inline constexpr auto fixedAnchor = "fixedAnchor";
    inline constexpr auto anchorNote  = "anchorNote";
    inline constexpr auto mpeMembers  = "mpeMembers";
    inline constexpr auto sendRpn     = "sendRpn";
    inline constexpr auto holdNote    = "holdNote";
    inline constexpr auto velMode     = "velMode";
    inline constexpr auto velocity    = "velocity";
    inline constexpr auto volumeOut   = "volumeOut";
    inline constexpr auto levelMaxDb  = "levelMaxDb";
}

class ThereBendProcessor : public juce::AudioProcessor
{
public:
    ThereBendProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    ThereminEngine engine;

private:
    ThereminEngine::Settings readSettings() const;
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioBuffer<float> monoBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThereBendProcessor)
};
