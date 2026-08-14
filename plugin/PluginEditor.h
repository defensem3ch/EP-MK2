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
    // How many controls fit across a section of this outer width.
    static int columnsForWidth(int width);
    // Controls that were not given a usable place inside this section.  When
    // a section runs out of room its remaining controls keep empty bounds, so
    // the symptom is a *missing* control rather than one hanging outside.
    int controlsNotPlaced() const;
    // Space between the lowest control and the bottom of the section.
    int bottomMargin() const;

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
// Bigger, bolder type than JUCE's defaults, applied to the whole panel so the
// value boxes inside sliders pick it up too -- those draw through a Label the
// slider owns, so setting a font on the slider does not reach them.
struct PanelLookAndFeel : juce::LookAndFeel_V4
{
    juce::Font getLabelFont(juce::Label&) override;
};

class PanelContent : public juce::Component
{
public:
    PanelContent(EpMk2Processor&, juce::AudioProcessorValueTreeState&);
    ~PanelContent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void setVoiceCount(int n);

    // The panel is as tall as its contents need at the design width.  Deriving
    // it rather than hard-coding it means adding parameters cannot silently
    // push controls out of the bottom of a section, which is exactly what the
    // extra tine modes did.
    int designHeight() const { return height; }

private:
    // Where each section sits, packed rather than laid out on a fixed grid.
    void computeLayout(int width);

    struct Placement { int column, y, height; };

    // Declared first so it outlives every component that draws through it.
    PanelLookAndFeel lookAndFeel;

    EpMk2Processor& proc;
    juce::OwnedArray<ParamSection> sections;
    std::vector<Placement> placements;
    int height = 0;
    int activeVoices = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelContent)
};

class EpMk2Editor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    // Wide enough that each of the three section columns fits four controls
    // across: 4 * kControlWidth, plus the section's own padding and the inset
    // inside its column.  Three sections of three knobs left the panel tall
    // and narrow, which is the wrong shape for a screen.
    static constexpr int kDesignWidth  = 1300;
    // Height comes from the panel's contents; see PanelContent::designHeight.
    int designHeight = 0;

    explicit EpMk2Editor(EpMk2Processor&);
    ~EpMk2Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // The unscaled design height, so a test can restore the default size.
    int designHeightForTest() const { return designHeight; }

private:
    void timerCallback() override;

    EpMk2Processor& proc;
    PanelContent content;
    int activeVoices = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpMk2Editor)
};
