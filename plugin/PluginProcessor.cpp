#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

int EpMk2Processor::getNumPrograms()
{
    return (int) epmk2::presets::table().size();
}

const juce::String EpMk2Processor::getProgramName(int index)
{
    const auto& t = epmk2::presets::table();
    return (index >= 0 && index < (int) t.size()) ? juce::String(t[(size_t) index].name)
                                                  : juce::String();
}

void EpMk2Processor::setCurrentProgram(int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;

    currentProgram = index;
    epmk2::presets::apply(state, index);
}

EpMk2Processor::EpMk2Processor()
    : juce::AudioProcessor(BusesProperties()
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "EPMK2", epmk2::params::layout())
{
    epmk2::params::apply(state, params, lastParamValues);
}

EpMk2Processor::Settings::Settings()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "EP-MK2";
    o.filenameSuffix      = ".settings";
   #if JUCE_LINUX || JUCE_BSD
    // JUCE's userApplicationDataDirectory is the home directory on Linux, so a
    // bare folder name drops a visible directory straight into it.  Put it
    // where the rest of the desktop keeps its configuration.
    o.folderName          = ".config/defensem3ch";
   #else
    o.folderName          = "defensem3ch";
   #endif
    o.osxLibrarySubFolder = "Application Support";
    // Batch writes: resized() fires continuously while a window is dragged,
    // and each one must not become a file write.
    o.millisecondsBeforeSaving = 1000;
    properties.setStorageParameters(o);
}

juce::Point<int> EpMk2Processor::Settings::editorSize() const
{
    if (auto* p = const_cast<juce::ApplicationProperties&>(properties).getUserSettings())
        return { p->getIntValue("editorWidth", 0), p->getIntValue("editorHeight", 0) };
    return {};
}

void EpMk2Processor::Settings::setEditorSize(int width, int height)
{
    if (auto* p = properties.getUserSettings()) {
        if (p->getIntValue("editorWidth", 0) == width
            && p->getIntValue("editorHeight", 0) == height)
            return;
        p->setValue("editorWidth", width);
        p->setValue("editorHeight", height);
    }
}

void EpMk2Processor::flushSettings()
{
    settings->properties.saveIfNeeded();
}

juce::Point<int> EpMk2Processor::getSavedEditorSize() const
{
    // The settings file is the only source.  This used to prefer a size stored
    // in the session state, on the reasoning that a project saved at a
    // particular size should reopen at it -- but a host applying *any* state
    // to a freshly added instance then overrode the user's actual preference,
    // and a new instance opened at the default no matter what had been stored.
    //
    // A window size is a property of the person, not of the piece of music.
    return settings->editorSize();
}

void EpMk2Processor::saveEditorSize(int width, int height)
{
    settings->setEditorSize(width, height);
}

// The scale travels in the session, not as a path to the file it came from.
// A path is fragile -- it breaks when the project moves machines, and it is
// wrong the moment the file is edited -- and the table is a few hundred bytes.
static const juce::Identifier kScaleName("scaleName"), kScaleCents("scaleCents");

void EpMk2Processor::setScale(const epmk2::Scale& s)
{
    scale = s;

    if (s.empty()) {
        liveScale.store(-1, std::memory_order_release);
    } else {
        const int slot = nextScaleSlot;
        nextScaleSlot = (nextScaleSlot + 1) % 3;

        auto& dst = scaleSlots[slot];
        const int n = juce::jmin(s.degrees(), (int) std::size(dst.cents));
        for (int i = 0; i < n; ++i)
            dst.cents[i] = s.cents[(size_t) i];
        dst.degrees = n;
        // Published last: the audio thread must not be able to see the count
        // before the values it counts.
        liveScale.store(slot, std::memory_order_release);
    }

    juce::StringArray cents;
    for (double c : s.cents)
        cents.add(juce::String(c, 6));
    state.state.setProperty(kScaleName, juce::String(s.name), nullptr);
    state.state.setProperty(kScaleCents, cents.joinIntoString(","), nullptr);
}

void EpMk2Processor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void EpMk2Processor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));

    // Rebuild the tuning from the restored tree.  Going back through setScale
    // is what publishes it to the audio thread; setting the properties alone
    // would restore a session that looks right and plays in 12-equal.
    epmk2::Scale restored;
    restored.name = state.state.getProperty(kScaleName, juce::String()).toString().toStdString();
    const auto cents = state.state.getProperty(kScaleCents, juce::String()).toString();
    for (const auto& c : juce::StringArray::fromTokens(cents, ",", ""))
        if (c.isNotEmpty())
            restored.cents.push_back(c.getDoubleValue());
    setScale(restored);
}

void EpMk2Processor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate, 32);
}

void EpMk2Processor::handleMidi(const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn(m.getNoteNumber(), m.getVelocity(), params);
    else if (m.isNoteOff())
        engine.noteOff(m.getNoteNumber(), params);
    else if (m.isSustainPedalOn() || m.isSustainPedalOff()) {
        ccSustain = m.isSustainPedalOn();
        // Drive the panel's toggle too, so the lamp shows the pedal's real
        // state.  A control that does not move when the thing it represents
        // moves is worse than no control at all.
        if (auto* p = state.getParameter("sustain"))
            p->setValueNotifyingHost(ccSustain ? 1.0f : 0.0f);
        updatePedal();
    }
    else if (m.isAllNotesOff())
        engine.allNotesOff(params);
    else if (m.isAllSoundOff())
        engine.allSoundOff();
}

// CC64 and the panel toggle are two ways to press one pedal, so the pedal is
// down if either says so.  This has to be reapplied every block: params::apply
// writes the panel toggle straight into params.voice.sustainPedal, which used
// to wipe out a pedal held by CC64 at the very next block boundary -- about
// 10 ms, long enough for the pedal to look like it worked and short enough for
// it never to actually hold a note.
void EpMk2Processor::updatePedal()
{
    const bool wanted = paramSustain || ccSustain;
    params.voice.sustainPedal = wanted;
    if (wanted == pedalDown)
        return;
    pedalDown = wanted;
    // Releasing the pedal has to let go of every voice whose key is already up.
    // Nothing did this when the panel toggle was switched off, either.
    if (!wanted)
        engine.sustainPedal(false, params);
}

void EpMk2Processor::processBlock(juce::AudioBuffer<float>& buffer,
                                  juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Pull the parameters once per block, and only re-derive filter
    // coefficients when something that feeds one has actually moved.
    if (epmk2::params::apply(state, params, lastParamValues))
        engine.configureAll(params);

    // apply() has just written the panel toggle into params.voice.sustainPedal;
    // fold CC64 back in before anything is rendered.
    paramSustain = params.voice.sustainPedal;
    updatePedal();

    // Point at whichever scale slot is live, once, so the whole block uses one
    // tuning even if the message thread publishes another halfway through it.
    if (const int live = liveScale.load(std::memory_order_acquire); live >= 0) {
        params.scaleCents = scaleSlots[live].cents;
        params.scaleDegrees = scaleSlots[live].degrees;
    } else {
        params.scaleCents = nullptr;
        params.scaleDegrees = 0;
    }

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    // Render between MIDI events rather than at block boundaries, so note
    // timing is sample-accurate.  The Heavy build could not do this: hv.vline~
    // quantised every strike to the start of a block.
    auto renderTo = [&](int from, int to) {
        for (int i = from; i < to; ++i) {
            float l = 0.0f, r = 0.0f;
            engine.render(params, l, r);
            left[i] = l;
            if (right != nullptr) right[i] = r;
        }
    };

    int pos = 0;
    for (const auto meta : midi) {
        const int eventPos = juce::jlimit(0, numSamples, meta.samplePosition);
        renderTo(pos, eventPos);
        pos = eventPos;
        handleMidi(meta.getMessage());
    }
    renderTo(pos, numSamples);

    midi.clear();
}

juce::AudioProcessorEditor* EpMk2Processor::createEditor()
{
    return new EpMk2Editor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EpMk2Processor();
}
