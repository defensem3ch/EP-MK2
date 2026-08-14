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

juce::Point<int> EpMk2Processor::getSavedEditorSize() const
{
    return { (int) state.state.getProperty("editorWidth", 0),
             (int) state.state.getProperty("editorHeight", 0) };
}

void EpMk2Processor::saveEditorSize(int width, int height)
{
    // No undo manager: a window resize is not an edit the user wants to undo
    // through the parameter history.
    state.state.setProperty("editorWidth", width, nullptr);
    state.state.setProperty("editorHeight", height, nullptr);
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
    else if (m.isSustainPedalOn()) {
        ccSustain = true;
        updatePedal();
    }
    else if (m.isSustainPedalOff()) {
        ccSustain = false;
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

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    // Render between MIDI events rather than at block boundaries, so note
    // timing is sample-accurate.  The Heavy build could not do this: hv.vline~
    // quantised every strike to the start of a block.
    int pos = 0;
    for (const auto meta : midi) {
        const int eventPos = juce::jlimit(0, numSamples, meta.samplePosition);
        for (; pos < eventPos; ++pos) {
            const float s = engine.process(params);
            left[pos] = s;
            if (right != nullptr) right[pos] = s;
        }
        handleMidi(meta.getMessage());
    }

    for (; pos < numSamples; ++pos) {
        const float s = engine.process(params);
        left[pos] = s;
        if (right != nullptr) right[pos] = s;
    }

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
