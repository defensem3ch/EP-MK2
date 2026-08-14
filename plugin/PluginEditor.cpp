#include "PluginEditor.h"
#include "PluginProcessor.h"

using namespace epmk2::params;

namespace {
constexpr int kControlWidth  = 90;
constexpr int kControlHeight = 78;
constexpr int kSectionHeader = 26;
constexpr int kHeaderHeight  = 50;
constexpr int kPad           = 8;
}

//==============================================================================
ParamControl::ParamControl(juce::AudioProcessorValueTreeState& tree, const Spec& spec)
    : label(spec.name), isToggle(spec.unit == Unit::Toggle)
{
    if (isToggle) {
        addAndMakeVisible(button);
        buttonAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment>(tree, spec.id, button);
    } else {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 19);
        // No suffix or decimal count here: the parameter's own
        // stringFromValue supplies both, and setting them again doubles the
        // unit ("0.0 dB dB").
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1c1c1c));
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff8fb8d8));
        // Frequencies and Q span decades; a linear knob spends nearly all of
        // its travel where nothing interesting happens.
        if (spec.unit == Unit::Hertz || spec.unit == Unit::Q || spec.unit == Unit::Millis)
            slider.setSkewFactor(0.35);
        addAndMakeVisible(slider);
        sliderAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(tree, spec.id, slider);
    }
}

void ParamControl::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xffcfcfcf));
    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText(label, getLocalBounds().removeFromTop(16),
                     juce::Justification::centredTop, 1);
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(16);
    if (isToggle)
        button.setBounds(r.withSizeKeepingCentre(30, 30));
    else
        slider.setBounds(r.reduced(2, 0));
}

//==============================================================================
ParamSection::ParamSection(juce::AudioProcessorValueTreeState& tree, const juce::String& name)
    : title(name), colour(sectionColour(name))
{
    for (const auto& spec : table())
        if (name == spec.section)
            addAndMakeVisible(controls.add(new ParamControl(tree, spec)));
}

int ParamSection::rowsNeeded(int columns) const
{
    return (controls.size() + columns - 1) / juce::jmax(1, columns);
}

void ParamSection::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff383838));
    g.fillRoundedRectangle(r, 4.0f);

    auto header = r.removeFromTop((float)kSectionHeader);
    g.setColour(colour);
    g.fillRoundedRectangle(header, 4.0f);
    g.fillRect(header.withTrimmedTop(header.getHeight() * 0.5f));

    g.setColour(juce::Colour(0xff303030));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText(title.toUpperCase(), header.reduced(8.0f, 0.0f),
               juce::Justification::centredLeft);
}

void ParamSection::resized()
{
    auto r = getLocalBounds().reduced(kPad, 0);
    r.removeFromTop(kSectionHeader + 4);

    const int columns = juce::jmax(1, r.getWidth() / kControlWidth);
    int i = 0;
    while (i < controls.size() && r.getHeight() > 0) {
        auto row = r.removeFromTop(kControlHeight);
        for (int c = 0; c < columns && i < controls.size(); ++c, ++i)
            controls[i]->setBounds(row.removeFromLeft(row.getWidth() / (columns - c)));
    }
}

//==============================================================================
PanelContent::PanelContent(EpMk2Processor& p, juce::AudioProcessorValueTreeState& tree)
    : proc(p)
{
    for (const auto& name : sectionOrder())
        addAndMakeVisible(sections.add(new ParamSection(tree, name)));
}

void PanelContent::setVoiceCount(int n)
{
    if (n == activeVoices)
        return;
    activeVoices = n;
    repaint(getLocalBounds().removeFromTop(kHeaderHeight));
}

void PanelContent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));

    auto header = getLocalBounds().removeFromTop(kHeaderHeight);
    g.setColour(juce::Colour(0xff404040));
    g.fillRect(header);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(26.0f));
    g.drawText("EP-MK2", header.reduced(16, 0), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff9a9a9a));
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(juce::String(activeVoices) + (activeVoices == 1 ? " voice" : " voices"),
               header.reduced(16, 0), juce::Justification::centredRight);
}

void PanelContent::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(kHeaderHeight);
    r.reduce(kPad, kPad);

    // Two rows of three, roughly how the original panel was arranged.
    const int columns = 3;
    const int rows = (sections.size() + columns - 1) / columns;
    const int cellW = r.getWidth() / columns;
    const int cellH = r.getHeight() / juce::jmax(1, rows);

    for (int i = 0; i < sections.size(); ++i) {
        const int col = i % columns, row = i / columns;
        sections[i]->setBounds(juce::Rectangle<int>(r.getX() + col * cellW,
                                                    r.getY() + row * cellH,
                                                    cellW, cellH).reduced(4));
    }
}

//==============================================================================
EpMk2Editor::EpMk2Editor(EpMk2Processor& p)
    : juce::AudioProcessorEditor(&p), proc(p), content(p, p.getState())
{
    addAndMakeVisible(content);

    setResizable(true, true);
    // Locked to the design aspect so the scale factor is the same in both
    // directions and nothing stretches.
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)kDesignWidth / (double)kDesignHeight);
    setResizeLimits(kDesignWidth * 2 / 3, kDesignHeight * 2 / 3,
                    kDesignWidth * 2,     kDesignHeight * 2);

    setSize(kDesignWidth, kDesignHeight);
    startTimerHz(10);
}

void EpMk2Editor::timerCallback()
{
    content.setVoiceCount(proc.getActiveVoiceCount());
}

void EpMk2Editor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
}

void EpMk2Editor::resized()
{
    const float scale = juce::jmin((float)getWidth()  / (float)kDesignWidth,
                                   (float)getHeight() / (float)kDesignHeight);
    // Bounds are in pre-transform coordinates; JUCE maps mouse events through
    // the transform, so the controls stay hittable at any size.
    content.setTransform(juce::AffineTransform::scale(scale));
    content.setBounds(0, 0, kDesignWidth, kDesignHeight);
}
