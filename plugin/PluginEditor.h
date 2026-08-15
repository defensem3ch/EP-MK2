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

    // Hovering anywhere on the control, including its slider, puts its help in
    // the panel's info bar.  The slider covers most of the cell, so this class
    // listens to it rather than relying on its own mouse events.
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    juce::String label;
    juce::String help;
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
    // The space above the first row and below the last.  A section should
    // show the same, and the same as the gap between its rows.
    int topMargin() const;
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
// The font the panel draws control names in.  Exposed because the panel's
// whole width is derived from how wide the longest name is in it, so a test
// has to measure the font that is actually drawn -- approximating it with the
// system default measured a font the plugin no longer uses anywhere.
juce::Font panelLabelFont();

struct PanelLookAndFeel : juce::LookAndFeel_V4
{
    juce::Font getLabelFont(juce::Label&) override;

    // Catches the fonts this file never names: JUCE turns a slider's value box
    // into a TextEditor when it is typed into, and that picks its own font.
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

    // A solid knob with the travelled range drawn as an arc around it, in the
    // colour of the section it belongs to.  JUCE's default rotary puts a dot
    // where the pointer is, which reads as a second object rather than as part
    // of the knob.
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider&) override;

    // Toggles are LEDs, not tick boxes: a lit lamp reads as on from across a
    // room, and a tick box reads as a form to fill in.
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool highlighted, bool down) override;
};

class PanelContent : public juce::Component
{
public:
    PanelContent(EpMk2Processor&, juce::AudioProcessorValueTreeState&);
    ~PanelContent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void setVoiceCount(int n);
    void showHelp(const juce::String& name, const juce::String& text);

    // The panel is as tall as its contents need at the design width.  Deriving
    // it rather than hard-coding it means adding parameters cannot silently
    // push controls out of the bottom of a section, which is exactly what the
    // extra tine modes did.
    int designHeight() const { return height; }

private:
    // Where each section sits, packed rather than laid out on a fixed grid.
    void computeLayout();

    struct Placement { int column, y, height; };

    // Declared first so it outlives every component that draws through it.
    PanelLookAndFeel lookAndFeel;

    EpMk2Processor& proc;
    juce::OwnedArray<ParamSection> sections;
    std::vector<Placement> placements;
    juce::String helpName, helpText;
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
    // Set by the type, not chosen: three columns of four controls at the
    // width the longest name needs on one line.  See kControlWidth.
    static constexpr int kDesignWidth  = 1604;
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
    // When the editor was built.  Resizes arriving in the first moments come
    // from the host opening the window, not from the user, and must not be
    // written down as a preference.
    juce::uint32 openedAt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpMk2Editor)
};
