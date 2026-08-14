#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"

class EpMk2Processor;

// One labelled control, bound to a parameter.  Toggles get a button, anything
// else gets a rotary with a value box.
class ParamControl : public juce::Component
{
public:
    ParamControl(juce::AudioProcessorValueTreeState& tree, const epmk2::params::Spec& spec);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    juce::String label;
    bool isToggle;

    juce::Slider slider;
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamControl)
};

// A titled group of controls, coloured to match the section headers of the
// original Pd panel.
class ParamSection : public juce::Component
{
public:
    ParamSection(juce::AudioProcessorValueTreeState& tree, const juce::String& name);

    void resized() override;
    void paint(juce::Graphics&) override;

    int rowsNeeded(int columns) const;

private:
    juce::String title;
    juce::Colour colour;
    juce::OwnedArray<ParamControl> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamSection)
};

// The panel is laid out once at a fixed design size and then scaled to fill
// the window with an AffineTransform.  Doing it this way rather than
// recomputing every font and bound on resize means text, knobs and value boxes
// all scale together, and JUCE still renders them as vectors at the final
// resolution rather than magnifying a bitmap.
class PanelContent : public juce::Component
{
public:
    PanelContent(EpMk2Processor&, juce::AudioProcessorValueTreeState&);

    void paint(juce::Graphics&) override;
    void resized() override;

    void setVoiceCount(int n);

private:
    EpMk2Processor& proc;
    juce::OwnedArray<ParamSection> sections;
    int activeVoices = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelContent)
};

class EpMk2Editor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    static constexpr int kDesignWidth  = 900;
    static constexpr int kDesignHeight = 620;

    explicit EpMk2Editor(EpMk2Processor&);
    ~EpMk2Editor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    EpMk2Processor& proc;
    PanelContent content;
    int activeVoices = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpMk2Editor)
};
