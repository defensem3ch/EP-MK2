#include "PluginEditor.h"
#include "PluginProcessor.h"

using namespace epmk2::params;

namespace {
constexpr int kControlWidth  = 100;
constexpr int kControlHeight = 104;
constexpr int kSectionHeader = 30;
constexpr int kHeaderHeight  = 56;

// Type sizes, in one place.  Everything is bold: at panel scale a regular
// weight on a dark background is hard to read at a glance while playing.
constexpr float kLabelFont   = 15.0f;
constexpr float kSectionFont = 17.0f;
constexpr float kTitleFont   = 30.0f;
constexpr float kValueFont   = 16.0f;
constexpr float kCreditFont  = 12.0f;
constexpr float kInfoFont    = 14.0f;
// Room for two lines of label.
constexpr int   kLabelHeight = 34;
// Space below the last row of controls, inside a section.  Note a section is
// inset by 4 on every side when it is positioned, so 8 of this is eaten by
// that -- allowing only kPad here left the bottom row of value boxes sitting
// flush on the section border.
constexpr int   kSectionBottomPad = 22;
// A fixed strip rather than a floating tooltip: it does not cover the control
// being read, it does not wait for a hover timeout, and it is visible before
// anyone thinks to look for it.
constexpr int   kInfoBarHeight = 52;
constexpr int kPad           = 8;
constexpr int kSectionColumns = 3;
}

//==============================================================================
ParamControl::ParamControl(juce::AudioProcessorValueTreeState& tree, const Spec& spec)
    : label(spec.name), help(spec.help), isToggle(spec.unit == Unit::Toggle)
{
    if (isToggle) {
        addAndMakeVisible(button);
        button.addMouseListener(this, true);
        buttonAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment>(tree, spec.id, button);
    } else {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 88, 24);
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
        slider.addMouseListener(this, true);
        sliderAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(tree, spec.id, slider);
    }
}

void ParamControl::mouseEnter(const juce::MouseEvent&)
{
    if (auto* panel = findParentComponentOfClass<PanelContent>())
        panel->showHelp(label, help);
}

void ParamControl::mouseExit(const juce::MouseEvent&)
{
    if (auto* panel = findParentComponentOfClass<PanelContent>())
        panel->showHelp({}, {});
}

void ParamControl::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xffe4e4e4));
    g.setFont(juce::FontOptions(kLabelFont, juce::Font::bold));
    // Two lines, so a long name wraps instead of running into its neighbour.
    g.drawFittedText(label, getLocalBounds().removeFromTop(kLabelHeight).reduced(2, 0),
                     juce::Justification::centredTop, 2);
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(kLabelHeight);
    if (isToggle)
        button.setBounds(r.withSizeKeepingCentre(36, 36));
    else
        slider.setBounds(r.reduced(2, 0).withTrimmedBottom(5));
}

//==============================================================================
ParamSection::ParamSection(juce::AudioProcessorValueTreeState& tree, const juce::String& name)
    : title(name), colour(sectionColour(name))
{
    setName(name);
    for (const auto& spec : table())
        if (name == spec.section)
            addAndMakeVisible(controls.add(new ParamControl(tree, spec)));
}

int ParamSection::rowsNeeded(int columns) const
{
    return (controls.size() + columns - 1) / juce::jmax(1, columns);
}

int ParamSection::controlsNotPlaced() const
{
    int bad = 0;
    for (const auto* c : controls) {
        const auto b = c->getBounds();
        if (b.getWidth() < kControlWidth / 2 || b.getHeight() < kControlHeight / 2
            || !getLocalBounds().contains(b))
            ++bad;
    }
    return bad;
}

int ParamSection::bottomMargin() const
{
    int lowest = 0;
    for (const auto* c : controls)
        lowest = juce::jmax(lowest, c->getBottom());
    return getHeight() - lowest;
}

int ParamSection::columnsForWidth(int width)
{
    return juce::jmax(1, (width - 2 * kPad) / kControlWidth);
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

    g.setColour(juce::Colour(0xff262626));
    g.setFont(juce::FontOptions(kSectionFont, juce::Font::bold));
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
juce::Font PanelLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions(kValueFont, juce::Font::bold));
}

PanelContent::~PanelContent()
{
    setLookAndFeel(nullptr);
}

PanelContent::PanelContent(EpMk2Processor& p, juce::AudioProcessorValueTreeState& tree)
    : proc(p)
{
    setLookAndFeel(&lookAndFeel);
    for (const auto& name : sectionOrder())
        addAndMakeVisible(sections.add(new ParamSection(tree, name)));

    computeLayout(EpMk2Editor::kDesignWidth);
}

void PanelContent::showHelp(const juce::String& name, const juce::String& text)
{
    if (name == helpName)
        return;
    helpName = name;
    helpText = text;
    repaint(getLocalBounds().removeFromBottom(kInfoBarHeight));
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

    auto titleArea = header.reduced(16, 0);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(kTitleFont, juce::Font::bold));
    const int titleWidth = juce::GlyphArrangement::getStringWidthInt(
        juce::Font(juce::FontOptions(kTitleFont, juce::Font::bold)), "EP-MK2");
    g.drawText("EP-MK2", titleArea, juce::Justification::centredLeft);

    // The model is not the one Miguel Moreno wrote, but it descends from it,
    // and GPL-3 lineage should be visible in the thing itself rather than only
    // in a licence file nobody opens.
    g.setColour(juce::Colour(0xff9a9a9a));
    g.setFont(juce::FontOptions(kCreditFont, juce::Font::bold));
    g.drawText("after EP-MK1 by Miguel Moreno",
               titleArea.withTrimmedLeft(titleWidth + 14),
               juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffb4b4b4));
    g.setFont(juce::FontOptions(kLabelFont, juce::Font::bold));
    g.drawText(juce::String(activeVoices) + (activeVoices == 1 ? " voice" : " voices"),
               header.reduced(16, 0), juce::Justification::centredRight);

    // ---- info bar ---------------------------------------------------------
    auto bar = getLocalBounds().removeFromBottom(kInfoBarHeight);
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(bar);
    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillRect(bar.removeFromTop(1));
    bar = bar.reduced(16, 5);

    if (helpName.isEmpty()) {
        g.setColour(juce::Colour(0xff707070));
        g.setFont(juce::FontOptions(kInfoFont));
        g.drawText("Hover a control to see what it does.", bar,
                   juce::Justification::centredLeft);
        return;
    }

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(kInfoFont, juce::Font::bold));
    const int nameWidth = juce::GlyphArrangement::getStringWidthInt(
        juce::Font(juce::FontOptions(kInfoFont, juce::Font::bold)), helpName + "  ");
    g.drawText(helpName, bar.withWidth(nameWidth), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffc8c8c8));
    g.setFont(juce::FontOptions(kInfoFont));
    g.drawFittedText(helpText,
                     getLocalBounds().removeFromBottom(kInfoBarHeight).reduced(16, 5)
                         .withTrimmedLeft(nameWidth),
                     juce::Justification::centredLeft, 2);
}

// Three columns, with each section going into whichever is currently shortest.
// A fixed grid cannot do this well: the sections hold 2, 4, 10, 8, 8 and 4
// controls, so an equal split both cut the bottom off Tine and drew Output as
// a mostly empty box four rows tall.  Packing keeps the columns level and the
// panel as short as its contents allow.
void PanelContent::computeLayout(int width)
{
    const int cellW = (width - 2 * kPad) / kSectionColumns;
    // A section is inset by 4 either side inside its column.
    const int innerCols = ParamSection::columnsForWidth(cellW - 8);

    int columnHeight[kSectionColumns] = {};
    placements.clear();

    for (int i = 0; i < sections.size(); ++i) {
        int shortest = 0;
        for (int c = 1; c < kSectionColumns; ++c)
            if (columnHeight[c] < columnHeight[shortest])
                shortest = c;

        const int h = kSectionHeader + 4
                    + sections[i]->rowsNeeded(innerCols) * kControlHeight
                    + kSectionBottomPad;
        placements.push_back({ shortest, columnHeight[shortest], h });
        columnHeight[shortest] += h;
    }

    int tallest = 0;
    for (int c = 0; c < kSectionColumns; ++c)
        tallest = juce::jmax(tallest, columnHeight[c]);
    height = kHeaderHeight + 2 * kPad + tallest + kInfoBarHeight;
}

void PanelContent::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(kHeaderHeight);
    r.removeFromBottom(kInfoBarHeight);
    r.reduce(kPad, kPad);

    const int cellW = r.getWidth() / kSectionColumns;
    for (int i = 0; i < sections.size() && i < (int)placements.size(); ++i) {
        const auto& pl = placements[(size_t)i];
        sections[i]->setBounds(juce::Rectangle<int>(r.getX() + pl.column * cellW,
                                                    r.getY() + pl.y,
                                                    cellW, pl.height).reduced(4));
    }
}

//==============================================================================
EpMk2Editor::EpMk2Editor(EpMk2Processor& p)
    : juce::AudioProcessorEditor(&p), proc(p), content(p, p.getState())
{
    designHeight = content.designHeight();
    // Read before anything sizes the window.  setResizeLimits() resizes a
    // still-empty editor to the minimum to satisfy them, which fires resized()
    // and would overwrite the stored size with 2/3 scale before we got to it.
    const auto saved = proc.getSavedEditorSize();

    addAndMakeVisible(content);

    setResizable(true, true);
    // Locked to the design aspect so the scale factor is the same in both
    // directions and nothing stretches.
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)kDesignWidth / (double)designHeight);
    const int minW = kDesignWidth * 2 / 3, minH = designHeight * 2 / 3;
    const int maxW = kDesignWidth * 2,      maxH = designHeight * 2;
    setResizeLimits(minW, minH, maxW, maxH);

    // Reopen at the size it was left at.  Clamped to the current limits rather
    // than trusted, since the design size can change between versions and a
    // stale value would otherwise reopen the window at a size this build does
    // not allow.
    if (saved.x >= minW && saved.x <= maxW && saved.y >= minH && saved.y <= maxH)
        setSize(saved.x, saved.y);
    else
        setSize(kDesignWidth, designHeight);
    startTimerHz(10);
}

EpMk2Editor::~EpMk2Editor()
{
    proc.flushSettings();
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
    proc.saveEditorSize(getWidth(), getHeight());

    const float scale = juce::jmin((float)getWidth()  / (float)kDesignWidth,
                                   (float)getHeight() / (float)designHeight);
    // Bounds are in pre-transform coordinates; JUCE maps mouse events through
    // the transform, so the controls stay hittable at any size.
    content.setTransform(juce::AffineTransform::scale(scale));
    content.setBounds(0, 0, kDesignWidth, designHeight);
}
