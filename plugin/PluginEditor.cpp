#include <vector>
#include <limits>

#include <EpMk2Fonts.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

using namespace epmk2::params;

namespace {
constexpr int kControlWidth  = 100;
constexpr int kControlHeight = 124;
constexpr int kSectionHeader = 30;
constexpr int kHeaderHeight  = 56;

// The panel's typeface, embedded in the binary rather than asked of the host.
//
// JUCE's default is whatever the machine calls "sans-serif": Noto Sans on this
// box, Segoe UI on Windows, Helvetica on macOS.  That makes the panel look
// different everywhere, and worse, it makes label widths unknowable at build
// time -- the layout sizes its cells to the longest word, so a wider font on
// someone else's machine silently overflows what the tests measured here.
//
// Liberation Sans, under the SIL Open Font License 1.1, which permits
// embedding and redistribution.  Bundled unmodified; the licence ships in
// resources/fonts/.
//
// The two weights are separate files rather than one synthesised from the
// other: JUCE fakes bold by smearing the outline, which at 15 px on a dark
// background fills in the counters of a, e and s.
inline juce::Typeface::Ptr panelTypeface(bool bold)
{
    static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor(
        EpMk2Fonts::LiberationSansRegular_ttf, EpMk2Fonts::LiberationSansRegular_ttfSize);
    static const juce::Typeface::Ptr heavy = juce::Typeface::createSystemTypefaceFor(
        EpMk2Fonts::LiberationSansBold_ttf, EpMk2Fonts::LiberationSansBold_ttfSize);
    return bold ? heavy : regular;
}

// Style is resolved here, into a real weight, and the options are then plain:
// asking for bold *as well* as handing over the bold face is what produces
// double-emboldened text.
inline juce::FontOptions panelFont(float height, int style = juce::Font::bold)
{
    return juce::FontOptions(height, juce::Font::plain)
        .withTypeface(panelTypeface((style & juce::Font::bold) != 0));
}

// Type sizes, in one place.  Everything is bold: at panel scale a regular
// weight on a dark background is hard to read at a glance while playing.
constexpr float kLabelFont   = 15.0f;
constexpr float kSectionFont = 17.0f;
constexpr float kTitleFont   = 30.0f;
constexpr float kValueFont   = 16.0f;
constexpr float kCreditFont  = 12.0f;
constexpr float kInfoFont    = 14.0f;
// How far the glow reaches past the lamp, as a multiple of its radius.  The
// lamp is sized so that lamp + glow exactly fills its component.
constexpr float kGlowReach   = 1.9f;
// How long after the editor opens a resize is assumed to be the host's doing.
constexpr juce::uint32 kSettleMs = 700;

// Kept together so the panel can be re-weighted in one place.  These sit
// between the mid grey this started at and the near black it went to: dark
// enough that the pastels carry, light enough that the panel has depth rather
// than being a hole with controls in it.
namespace col {
constexpr juce::uint32 panelTop     = 0xff232323;
constexpr juce::uint32 panelBottom  = 0xff181818;
constexpr juce::uint32 headerTop    = 0xff313131;
constexpr juce::uint32 headerBottom = 0xff232323;
constexpr juce::uint32 sectionTop   = 0xff2b2b2b;
constexpr juce::uint32 sectionBot   = 0xff222222;
constexpr juce::uint32 cellTop      = 0xff262626;
constexpr juce::uint32 cellBottom   = 0xff1e1e1e;
constexpr juce::uint32 valueBox     = 0xff121212;
constexpr juce::uint32 infoTop      = 0xff1b1b1b;
constexpr juce::uint32 infoBottom   = 0xff141414;
constexpr juce::uint32 knobTop      = 0xffdcdcdc;
constexpr juce::uint32 knobBottom   = 0xff909090;
constexpr juce::uint32 knobRim      = 0xff101010;
constexpr juce::uint32 track        = 0xff3d3d3d;
constexpr juce::uint32 lampOff      = 0xff2f2f2f;
constexpr juce::uint32 pointer      = 0xff2b2b2b;
}

// A vertical gradient, which is all the depth any of this needs.
inline void fillVertical(juce::Graphics& g, juce::Rectangle<float> r,
                         juce::uint32 top, juce::uint32 bottom)
{
    g.setGradientFill(juce::ColourGradient(juce::Colour(top), r.getX(), r.getY(),
                                           juce::Colour(bottom), r.getX(), r.getBottom(),
                                           false));
}
// Room for two lines of label.
// Two lines of name, so every knob below it is the same size.
constexpr int   kLabelArea   = 36;
constexpr int   kLabelLine   = kLabelArea / 2;
// Space between the section's coloured header and the first row of names.
// Paired with kSectionBottomPad: the header is a solid block of colour and
// the names are the first thing under it, so with only a few pixels between
// them the names read as part of the header rather than as part of the row.
constexpr int   kSectionTopPad = 14;
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
        button.setColour(juce::ToggleButton::tickColourId,
                         sectionColour(spec.section).withMultipliedSaturation(1.15f));
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
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(col::valueBox));
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        // The travelled arc takes the colour of the section it sits in, so the
        // pastel grouping does some work rather than only labelling a header.
        slider.setColour(juce::Slider::rotarySliderFillColourId,
                         sectionColour(spec.section).withMultipliedSaturation(1.15f));
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
    // A tint behind each control, so label, knob and value read as one object.
    // Without it the value box sits at the bottom of its cell with the next
    // row's label directly beneath, at the same spacing as the gap inside the
    // control -- and the eye pairs the value with the wrong name.  This is the
    // cheapest way to say which three things belong together.
    const auto cell = getLocalBounds().reduced(2, 1).toFloat();
    fillVertical(g, cell, col::cellTop, col::cellBottom);
    g.fillRoundedRectangle(cell, 4.0f);

    g.setColour(juce::Colour(0xffe4e4e4));
    g.setFont(panelFont(kLabelFont));
    // The name area is a fixed two lines so that every knob comes out the same
    // size -- sizing it to the text made controls with long names smaller than
    // their neighbours.  The text is aligned to the *top* of that area, so
    // every name in a row starts on the same line whether it wraps or not; a
    // one-line name then sits a line clear of its knob rather than against it.
    // No horizontal squashing (1.0) and no more than two lines.  JUCE's
    // default lets it compress glyphs to about 0.7 of their width to make text
    // fit, which is why some names looked narrower than others -- and it
    // shrinks the font height too.  Forbidding both means every label on the
    // panel is drawn at exactly the same size, and the cell has to be wide
    // enough for the longest word instead.
    auto content = contentArea();
    g.drawFittedText(label, content.removeFromTop(labelLines * kLabelLine).reduced(2, 0),
                     juce::Justification::centredTop, labelLines, 1.0f);
}

juce::Rectangle<int> ParamControl::contentArea() const
{
    const int used = labelLines * kLabelLine + (kControlHeight - kLabelArea);
    return getLocalBounds().withSizeKeepingCentre(
        getWidth(), juce::jmin(getHeight(), used));
}

int ParamControl::labelLinesNeeded(int width) const
{
    // What drawFittedText will do: one line if the whole name fits across the
    // cell, two if it has to break.  The reduced(2, 0) in paint is why this
    // measures against four pixels less than the cell.
    const juce::Font f(panelFont(kLabelFont));
    return juce::GlyphArrangement::getStringWidthInt(f, label) <= width - 4 ? 1 : 2;
}

void ParamControl::setLabelLines(int lines)
{
    lines = juce::jlimit(1, 2, lines);
    if (lines == labelLines)
        return;
    labelLines = lines;
    resized();
    repaint();
}

void ParamControl::resized()
{
    auto r = contentArea();
    r.removeFromTop(labelLines * kLabelLine);
    if (isToggle)
        button.setBounds(r.withTrimmedBottom(12).withSizeKeepingCentre(56, 56));
    else
        // The trim is the gap *between* rows.  It has to be larger than the
        // space between the knob and its own value, or proximity groups them
        // the wrong way round.
        // A few pixels above the knob and below it, so it is not pressed
        // against its name or its value.
        slider.setBounds(r.reduced(4, 0).withTrimmedTop(5).withTrimmedBottom(12));
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
    fillVertical(g, r, col::sectionTop, col::sectionBot);
    g.fillRoundedRectangle(r, 4.0f);

    auto header = r.removeFromTop((float)kSectionHeader);
    g.setColour(colour);
    g.fillRoundedRectangle(header, 4.0f);
    g.fillRect(header.withTrimmedTop(header.getHeight() * 0.5f));

    g.setColour(juce::Colour(0xff262626));
    g.setFont(panelFont(kSectionFont));
    g.drawText(title.toUpperCase(), header.reduced(8.0f, 0.0f),
               juce::Justification::centredLeft);
}

void ParamSection::resized()
{
    auto r = getLocalBounds().reduced(kPad, 0);
    r.removeFromTop(kSectionHeader + kSectionTopPad);

    const int columns = juce::jmax(1, r.getWidth() / kControlWidth);
    int i = 0;
    while (i < controls.size() && r.getHeight() > 0) {
        auto row = r.removeFromTop(kControlHeight);

        // Widths first, because how many lines a name needs depends on the
        // width it gets, and the whole row has to agree on a line count before
        // any of it can be positioned.  A short last row still divides by the
        // full column count, so its cells stay under the ones above.
        std::vector<juce::Rectangle<int>> cells;
        {
            auto rest = row;
            for (int c = 0; c < columns && i + c < controls.size(); ++c)
                cells.push_back(rest.removeFromLeft(rest.getWidth() / (columns - c)));
        }

        int lines = 1;
        for (size_t c = 0; c < cells.size(); ++c)
            lines = juce::jmax(lines,
                               controls[i + (int) c]->labelLinesNeeded(cells[c].getWidth()));

        for (size_t c = 0; c < cells.size(); ++c, ++i) {
            controls[i]->setLabelLines(lines);
            controls[i]->setBounds(cells[c]);
        }
    }
}

//==============================================================================
juce::Font PanelLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(panelFont(kValueFont));
}

juce::Typeface::Ptr PanelLookAndFeel::getTypefaceForFont(const juce::Font& f)
{
    return panelTypeface(f.isBold());
}

void PanelLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y,
                                        int width, int height, float pos,
                                        float startAngle, float endAngle,
                                        juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    auto square = bounds.withSizeKeepingCentre(size, size).reduced(2.0f);

    const float ringWidth = juce::jmax(3.0f, size * 0.11f);
    const float ringRadius = square.getWidth() * 0.5f - ringWidth * 0.5f;
    const auto centre = square.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    // The full travel, so the range is visible even where nothing is set.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                        startAngle, endAngle, true);
    g.setColour(juce::Colour(col::track));
    g.strokePath(track, juce::PathStrokeType(ringWidth,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // How far it has travelled, in the section's own colour.
    if (angle > startAngle + 0.01f) {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                            startAngle, angle, true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.strokePath(value, juce::PathStrokeType(ringWidth,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // A solid body, so the knob reads as an object rather than an outline.
    const float bodyRadius = ringRadius - ringWidth * 0.85f;
    g.setColour(juce::Colour(col::knobRim));
    g.fillEllipse(juce::Rectangle<float>(bodyRadius * 2.0f + 3.0f,
                                         bodyRadius * 2.0f + 3.0f)
                      .withCentre(centre));
    const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f)
                          .withCentre(centre);
    fillVertical(g, body, col::knobTop, col::knobBottom);
    g.fillEllipse(body);

    // The pointer: a line to the rim, not a dot beside it.
    juce::Path pointer;
    const float thickness = juce::jmax(2.0f, size * 0.075f);
    pointer.addRoundedRectangle(-thickness * 0.5f, -bodyRadius * 0.92f,
                                thickness, bodyRadius * 0.62f, thickness * 0.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle)
                               .translated(centre.x, centre.y));
    g.setColour(juce::Colour(col::pointer));
    g.fillPath(pointer);
}

void PanelLookAndFeel::drawToggleButton(juce::Graphics& g,
                                        juce::ToggleButton& button,
                                        bool highlighted, bool)
{
    auto bounds = button.getLocalBounds().toFloat();
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    // The lamp is small relative to its component because the glow is drawn
    // *outside* it, and anything past the component's bounds is clipped --
    // which is what made a round glow come out square at the corners.
    const float radius = size * 0.5f / kGlowReach;
    auto lamp = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f)
                    .withCentre(bounds.getCentre());
    const auto centre = lamp.getCentre();

    const bool on = button.getToggleState();
    const juce::Colour tint = button.findColour(juce::ToggleButton::tickColourId);

    // The socket, so an unlit lamp still reads as something that could light.
    g.setColour(juce::Colour(0xff141414));
    g.fillEllipse(lamp.expanded(2.0f));

    if (on) {
        // Glow: a couple of soft rings outside the lamp itself.
        for (int i = 3; i >= 1; --i) {
            g.setColour(tint.withAlpha(0.14f / (float) i));
            g.fillEllipse(lamp.expanded(radius * ((kGlowReach - 1.0f) / 3.0f)
                                        * (float) i));
        }
        g.setColour(tint);
        g.fillEllipse(lamp);
        // A brighter centre, so it looks lit rather than merely filled.
        g.setColour(tint.brighter(0.7f).withAlpha(0.85f));
        g.fillEllipse(lamp.reduced(radius * 0.45f).translated(0.0f, -radius * 0.12f));
    } else {
        g.setColour(juce::Colour(col::lampOff));
        g.fillEllipse(lamp);
        g.setColour(tint.withAlpha(highlighted ? 0.35f : 0.16f));
        g.fillEllipse(lamp.reduced(radius * 0.34f));
    }

    g.setColour(juce::Colour(0xff0d0d0d));
    g.drawEllipse(lamp, 1.4f);
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
    fillVertical(g, getLocalBounds().toFloat(), col::panelTop, col::panelBottom);
    g.fillRect(getLocalBounds());

    auto header = getLocalBounds().removeFromTop(kHeaderHeight);
    fillVertical(g, header.toFloat(), col::headerTop, col::headerBottom);
    g.fillRect(header);

    auto titleArea = header.reduced(16, 0);
    g.setColour(juce::Colours::white);
    g.setFont(panelFont(kTitleFont));
    const int titleWidth = juce::GlyphArrangement::getStringWidthInt(
        juce::Font(panelFont(kTitleFont)), "EP-MK2");
    g.drawText("EP-MK2", titleArea, juce::Justification::centredLeft);

    // The model is not the one Miguel Moreno wrote, but it descends from it,
    // and GPL-3 lineage should be visible in the thing itself rather than only
    // in a licence file nobody opens.
    g.setColour(juce::Colour(0xff9a9a9a));
    g.setFont(panelFont(kCreditFont));
    g.drawText("after EP-MK1 by Miguel Moreno",
               titleArea.withTrimmedLeft(titleWidth + 14),
               juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffb4b4b4));
    g.setFont(panelFont(kLabelFont));
    g.drawText(juce::String(activeVoices) + (activeVoices == 1 ? " voice" : " voices"),
               header.reduced(16, 0), juce::Justification::centredRight);

    // ---- info bar ---------------------------------------------------------
    auto bar = getLocalBounds().removeFromBottom(kInfoBarHeight);
    fillVertical(g, bar.toFloat(), col::infoTop, col::infoBottom);
    g.fillRect(bar);
    g.setColour(juce::Colour(0xff2c2c2c));
    g.fillRect(bar.removeFromTop(1));
    bar = bar.reduced(16, 5);

    if (helpName.isEmpty()) {
        g.setColour(juce::Colour(0xff707070));
        g.setFont(panelFont(kInfoFont, juce::Font::plain));
        g.drawText("Hover a control to see what it does.", bar,
                   juce::Justification::centredLeft);
        return;
    }

    g.setColour(juce::Colours::white);
    g.setFont(panelFont(kInfoFont));
    const int nameWidth = juce::GlyphArrangement::getStringWidthInt(
        juce::Font(panelFont(kInfoFont)), helpName + "  ");
    g.drawText(helpName, bar.withWidth(nameWidth), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffc8c8c8));
    g.setFont(panelFont(kInfoFont, juce::Font::plain));
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

    const int n = sections.size();
    std::vector<int> tall((size_t) n);
    for (int i = 0; i < n; ++i)
        tall[(size_t) i] = kSectionHeader + kSectionTopPad
                         + sections[i]->rowsNeeded(innerCols) * kControlHeight
                         + kSectionBottomPad;

    // Split the sections into three *contiguous* runs, one per column, and
    // keep the split with the shortest tallest column.
    //
    // Contiguous is the point.  Dealing sections into whichever column
    // balances best packs them tighter, but it reorders them -- and the order
    // is signal flow, which is the one thing the grouping is for.  Reading
    // column by column, a contiguous split preserves it exactly.
    //
    // Filling the shortest column as you go, which is what this did before,
    // does neither: it reorders *and* it commits early, so it cannot see a
    // tall section still to come and leaves one column a section short.
    std::vector<int> best((size_t) n, 0);
    int bestTallest = std::numeric_limits<int>::max();
    int bestSpread = std::numeric_limits<int>::max();

    for (int firstCut = 1; firstCut <= n; ++firstCut) {
        for (int secondCut = firstCut; secondCut <= n; ++secondCut) {
            int used[kSectionColumns] = {};
            for (int i = 0; i < n; ++i) {
                const int c = i < firstCut ? 0 : (i < secondCut ? 1 : 2);
                used[c] += tall[(size_t) i];
            }
            int hi = 0, lo = std::numeric_limits<int>::max();
            for (int c = 0; c < kSectionColumns; ++c) {
                hi = juce::jmax(hi, used[c]);
                lo = juce::jmin(lo, used[c]);
            }
            if (hi < bestTallest || (hi == bestTallest && hi - lo < bestSpread)) {
                bestTallest = hi;
                bestSpread = hi - lo;
                for (int i = 0; i < n; ++i)
                    best[(size_t) i] = i < firstCut ? 0 : (i < secondCut ? 1 : 2);
            }
        }
    }

    int columnHeight[kSectionColumns] = {};
    placements.clear();
    for (int i = 0; i < n; ++i) {
        const int c = best[(size_t) i];
        placements.push_back({ c, columnHeight[c], tall[(size_t) i] });
        columnHeight[c] += tall[(size_t) i];
    }

    height = kHeaderHeight + 2 * kPad + bestTallest + kInfoBarHeight;
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
    openedAt = juce::Time::getMillisecondCounter();
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

    // Reopen at the size it was left at -- but restore the *width* and derive
    // the height from this build's design ratio, rather than restoring both.
    //
    // The panel's aspect is fixed by the constrainer, so height was never an
    // independent quantity; and the design size changes whenever the layout
    // does, which has happened with most releases.  Storing both meant a
    // stored height from an older layout no longer matched the current ratio,
    // failed the range check, and dropped the window back to the default size
    // on every update.  Width alone survives a layout change.
    const int wanted = saved.x > 0 ? juce::jlimit(minW, maxW, saved.x) : kDesignWidth;
    setSize(wanted, juce::roundToInt(wanted * (double) designHeight
                                            / (double) kDesignWidth));
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
    g.fillAll(juce::Colour(col::panelBottom));
}

void EpMk2Editor::resized()
{
    // Only remember a size the user could have chosen.
    //
    // A host sizes the editor immediately after constructing it -- and the
    // first time it sees a new build, it has nothing remembered and uses the
    // default.  Saving that overwrote the stored preference with the factory
    // size, permanently, which is why shipping a build appeared to wipe it:
    // the size was being read correctly and then immediately thrown away.
    //
    // Anything in the first moments is therefore the host talking.  Later
    // resizes are the user, whether by our corner or the host's window frame.
    if (juce::Time::getMillisecondCounter() - openedAt > kSettleMs)
        proc.saveEditorSize(getWidth(), getHeight());

    const float scale = juce::jmin((float)getWidth()  / (float)kDesignWidth,
                                   (float)getHeight() / (float)designHeight);
    // Bounds are in pre-transform coordinates; JUCE maps mouse events through
    // the transform, so the controls stay hittable at any size.
    content.setTransform(juce::AffineTransform::scale(scale));
    content.setBounds(0, 0, kDesignWidth, designHeight);
}
