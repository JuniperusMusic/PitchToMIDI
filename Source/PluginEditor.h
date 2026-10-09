#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

/** A labelled control (slider / combo / toggle) bound to one parameter. */
class ParamControl : public juce::Component
{
public:
    ParamControl (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
    void resized() override;

private:
    juce::Label label;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
};

/** Tuner read-out plus a scrolling trace of input and output pitch. */
class PitchView : public juce::Component, private juce::Timer
{
public:
    explicit PitchView (ThereminEngine::Display& d);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    ThereminEngine::Display& display;
    static constexpr int historySize = 300; // ~10 s at 30 Hz
    std::vector<float> inHistory, outHistory;
    int writeIndex = 0;
};

class ThereBendEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ThereBendEditor (ThereBendProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    ParamControl* find (const juce::String& id) const;

    struct Column
    {
        juce::String title;
        std::vector<std::unique_ptr<ParamControl>> controls;
    };

    ThereBendProcessor& proc;
    PitchView pitchView;
    std::array<Column, 3> columns;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThereBendEditor)
};
