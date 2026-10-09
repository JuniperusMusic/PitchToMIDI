#include "PluginEditor.h"

using namespace juce;

namespace Colours_
{
    const Colour background { 0xff15171c };
    const Colour panel      { 0xff1f232b };
    const Colour text       { 0xffe6e8ee };
    const Colour dim        { 0xff8a91a0 };
    const Colour input      { 0xff5aa9e6 };
    const Colour output     { 0xffffb347 };
    const Colour good       { 0xff6fd08c };
}

//==============================================================================
ParamControl::ParamControl (AudioProcessorValueTreeState& state, const String& paramID)
{
    setComponentID (paramID);
    auto* param = state.getParameter (paramID);
    jassert (param != nullptr);

    label.setText (param->getName (64), dontSendNotification);
    label.setColour (Label::textColourId, Colours_::text);
    label.setFont (FontOptions (13.0f));
    addAndMakeVisible (label);

    if (auto* choice = dynamic_cast<AudioParameterChoice*> (param))
    {
        combo = std::make_unique<ComboBox>();
        combo->addItemList (choice->choices, 1);
        addAndMakeVisible (*combo);
        comboAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramID, *combo);
    }
    else if (dynamic_cast<AudioParameterBool*> (param) != nullptr)
    {
        toggle = std::make_unique<ToggleButton>();
        addAndMakeVisible (*toggle);
        buttonAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, paramID, *toggle);
    }
    else
    {
        slider = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::TextBoxRight);
        slider->setTextBoxStyle (Slider::TextBoxRight, false, 78, 20);
        slider->setColour (Slider::trackColourId, Colours_::output.withAlpha (0.7f));
        addAndMakeVisible (*slider);
        sliderAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramID, *slider);
    }

}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (128));
    if (slider) slider->setBounds (r);
    if (combo) combo->setBounds (r.reduced (0, 2));
    if (toggle) toggle->setBounds (r.removeFromLeft (30));
}

//==============================================================================
PitchView::PitchView (ThereminEngine::Display& d)
    : display (d), inHistory ((size_t) historySize, 0.0f), outHistory ((size_t) historySize, 0.0f)
{
    startTimerHz (30);
}

void PitchView::timerCallback()
{
    const bool active = display.noteActive.load();
    inHistory[(size_t) writeIndex] = display.voiced.load() ? display.inputPitch.load() : 0.0f;
    outHistory[(size_t) writeIndex] = active ? display.outputPitch.load() : 0.0f;
    writeIndex = (writeIndex + 1) % historySize;
    repaint();
}

void PitchView::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Colours_::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto area = bounds.reduced (10.0f);
    auto readout = area.removeFromLeft (230.0f);
    area.removeFromLeft (10.0f);

    // --- Tuner read-out
    const bool active = display.noteActive.load();
    const float outPitch = display.outputPitch.load();
    const float hz = display.detectedHz.load();
    const int nearest = roundToInt (outPitch);
    const float cents = (outPitch - (float) nearest) * 100.0f;

    g.setColour (active ? Colours_::text : Colours_::dim);
    g.setFont (FontOptions (44.0f, Font::bold));
    g.drawText (active ? MidiMessage::getMidiNoteName (nearest, true, true, 3) : String ("--"),
                readout.removeFromTop (54.0f), Justification::centredLeft);

    // Cents meter
    auto meter = readout.removeFromTop (18.0f);
    g.setColour (Colours_::background);
    g.fillRoundedRectangle (meter, 3.0f);
    g.setColour (Colours_::dim);
    g.drawVerticalLine ((int) meter.getCentreX(), meter.getY(), meter.getBottom());
    if (active)
    {
        const float x = meter.getCentreX() + jlimit (-50.0f, 50.0f, cents) / 50.0f * meter.getWidth() * 0.5f;
        g.setColour (std::abs (cents) < 5.0f ? Colours_::good : Colours_::output);
        g.fillRoundedRectangle (x - 3.0f, meter.getY(), 6.0f, meter.getHeight(), 2.0f);
    }
    readout.removeFromTop (6.0f);

    g.setFont (FontOptions (13.0f));
    auto line = [&] (const String& s, Colour c)
    {
        g.setColour (c);
        g.drawText (s, readout.removeFromTop (17.0f), Justification::centredLeft);
    };

    line ("Input:  " + (hz > 0.0f ? String (hz, 1) + " Hz" : String ("-")), Colours_::input);
    line ("Cents:  " + (active ? String (cents, 1) : String ("-")), Colours_::text);
    line ("Level:  " + String (display.levelDb.load(), 1) + " dB   conf " + String (display.confidence.load(), 2), Colours_::dim);
    const int note = display.midiNote.load();
    line ("MIDI:   " + (note >= 0 ? MidiMessage::getMidiNoteName (note, true, true, 3) + "  bend " + String (display.bend.load() - 8192)
                                  : String ("no note")), Colours_::output);
    line ("Volume: " + String (display.volume.load()), Colours_::dim);

    // --- Pitch trace
    g.setColour (Colours_::background);
    g.fillRoundedRectangle (area, 4.0f);

    // Auto-range around recent pitches.
    float lo = 1000.0f, hi = -1000.0f;
    for (int i = 0; i < historySize; ++i)
        for (float v : { inHistory[(size_t) i], outHistory[(size_t) i] })
            if (v > 0.0f) { lo = jmin (lo, v); hi = jmax (hi, v); }
    if (lo > hi) { lo = 48.0f; hi = 84.0f; }
    lo = std::floor (lo - 2.0f);
    hi = std::ceil (hi + 2.0f);
    if (hi - lo < 12.0f) { const float c = 0.5f * (hi + lo); lo = c - 6.0f; hi = c + 6.0f; }

    auto yFor = [&] (float p) { return area.getBottom() - (p - lo) / (hi - lo) * area.getHeight(); };

    g.setFont (FontOptions (10.0f));
    for (int n = (int) std::ceil (lo); n <= (int) hi; ++n)
    {
        const bool isC = n % 12 == 0;
        g.setColour (Colours_::dim.withAlpha (isC ? 0.35f : 0.12f));
        g.drawHorizontalLine ((int) yFor ((float) n), area.getX(), area.getRight());
        const bool natural = MidiMessage::isMidiNoteBlack (n) == false;
        if (isC || ((hi - lo) <= 14.0f) || ((hi - lo) <= 28.0f && natural))
        {
            g.setColour (Colours_::dim);
            g.drawText (MidiMessage::getMidiNoteName (n, true, true, 3),
                        Rectangle<float> (area.getX() + 3.0f, yFor ((float) n) - 6.0f, 40.0f, 12.0f), Justification::centredLeft);
        }
    }

    auto drawTrace = [&] (const std::vector<float>& h, Colour c, float thickness)
    {
        Path p;
        bool drawing = false;
        for (int i = 0; i < historySize; ++i)
        {
            const float v = h[(size_t) ((writeIndex + i) % historySize)];
            const float x = area.getX() + area.getWidth() * (float) i / (float) (historySize - 1);
            if (v <= 0.0f) { drawing = false; continue; }
            if (! drawing) p.startNewSubPath (x, yFor (v));
            else           p.lineTo (x, yFor (v));
            drawing = true;
        }
        g.setColour (c);
        g.strokePath (p, PathStrokeType (thickness, PathStrokeType::curved, PathStrokeType::rounded));
    };

    drawTrace (inHistory, Colours_::input.withAlpha (0.8f), 1.5f);
    drawTrace (outHistory, Colours_::output, 2.0f);

    g.setFont (FontOptions (11.0f));
    g.setColour (Colours_::input);
    g.drawText ("detected", area.reduced (6.0f).removeFromTop (14.0f), Justification::topRight);
    g.setColour (Colours_::output);
    g.drawText ("MIDI output", area.reduced (6.0f).withTrimmedTop (14.0f).removeFromTop (14.0f), Justification::topRight);
}

//==============================================================================
ThereBendEditor::ThereBendEditor (ThereBendProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pitchView (p.engine.display)
{
    addAndMakeVisible (pitchView);

    const std::array<std::pair<const char*, std::vector<const char*>>, 3> layout {{
        { "DETECTION", { ParamIDs::minHz, ParamIDs::maxHz, ParamIDs::threshold, ParamIDs::gateDb,
                         ParamIDs::releaseMs, ParamIDs::tuningA4, ParamIDs::transpose, ParamIDs::audioThru } },
        { "PITCH",     { ParamIDs::pitchMode, ParamIDs::correction, ParamIDs::glideMs, ParamIDs::smoothingMs,
                         ParamIDs::scale, ParamIDs::key, ParamIDs::hysteresis } },
        { "MIDI OUTPUT", { ParamIDs::outputMode, ParamIDs::channel, ParamIDs::bendRange, ParamIDs::fixedAnchor,
                           ParamIDs::anchorNote, ParamIDs::mpeMembers, ParamIDs::sendRpn, ParamIDs::holdNote,
                           ParamIDs::velMode, ParamIDs::velocity, ParamIDs::volumeOut, ParamIDs::levelMaxDb } },
    }};

    for (size_t c = 0; c < layout.size(); ++c)
    {
        columns[c].title = layout[c].first;
        for (auto* id : layout[c].second)
        {
            auto ctrl = std::make_unique<ParamControl> (p.apvts, id);
            addAndMakeVisible (*ctrl);
            columns[c].controls.push_back (std::move (ctrl));
        }
    }

    setSize (1020, 660);
    timerCallback();
    startTimerHz (10);
}

ParamControl* ThereBendEditor::find (const String& id) const
{
    for (auto& col : columns)
        for (auto& c : col.controls)
            if (c->getComponentID() == id)
                return c.get();
    return nullptr;
}

void ThereBendEditor::timerCallback()
{
    auto value = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    auto enable = [this] (const char* id, bool on)
    {
        if (auto* c = find (id))
        {
            c->setEnabled (on);
            c->setAlpha (on ? 1.0f : 0.4f);
        }
    };

    const bool glide = value (ParamIDs::pitchMode) > 0.5f;
    for (auto* id : { ParamIDs::correction, ParamIDs::glideMs, ParamIDs::scale, ParamIDs::key, ParamIDs::hysteresis })
        enable (id, glide);

    const bool mpe = value (ParamIDs::outputMode) > 0.5f;
    enable (ParamIDs::channel, ! mpe);
    enable (ParamIDs::mpeMembers, mpe);
    enable (ParamIDs::anchorNote, value (ParamIDs::fixedAnchor) > 0.5f);
    enable (ParamIDs::velocity, value (ParamIDs::velMode) < 0.5f);
    enable (ParamIDs::levelMaxDb, value (ParamIDs::volumeOut) > 0.5f || value (ParamIDs::velMode) > 0.5f);
}

void ThereBendEditor::paint (Graphics& g)
{
    g.fillAll (Colours_::background);

    auto header = getLocalBounds().removeFromTop (36).reduced (12, 0);
    g.setColour (Colours_::text);
    g.setFont (FontOptions (20.0f, Font::bold));
    g.drawText ("ThereBend", header, Justification::centredLeft);
    g.setColour (Colours_::dim);
    g.setFont (FontOptions (12.0f));
    g.drawText ("audio pitch -> smooth MIDI pitch bend", header, Justification::centredRight);

    for (auto& col : columns)
    {
        if (col.controls.empty())
            continue;
        auto first = col.controls.front()->getBounds();
        auto panel = Rectangle<int> (first.getX() - 8, first.getY() - 26, first.getWidth() + 16,
                                     col.controls.back()->getBottom() - first.getY() + 34);
        g.setColour (Colours_::panel);
        g.fillRoundedRectangle (panel.toFloat(), 6.0f);
        g.setColour (Colours_::dim);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText (col.title, panel.removeFromTop (24).reduced (8, 0), Justification::centredLeft);
    }
}

void ThereBendEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (26);
    pitchView.setBounds (r.removeFromTop (190));
    r.removeFromTop (12);

    const int gap = 12;
    const int colWidth = (r.getWidth() - 2 * gap) / 3;
    for (size_t c = 0; c < columns.size(); ++c)
    {
        auto col = r.removeFromLeft (colWidth).reduced (8, 0);
        r.removeFromLeft (gap);
        col.removeFromTop (26);
        for (auto& ctrl : columns[c].controls)
            ctrl->setBounds (col.removeFromTop (28));
    }
}
