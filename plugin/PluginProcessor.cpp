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
    else if (m.isSustainPedalOn())
        params.voice.sustainPedal = true;
    else if (m.isSustainPedalOff()) {
        params.voice.sustainPedal = false;
        engine.sustainPedal(false, params);
    }
    else if (m.isAllNotesOff())
        engine.allNotesOff(params);
    else if (m.isAllSoundOff())
        engine.allSoundOff();
}

void EpMk2Processor::processBlock(juce::AudioBuffer<float>& buffer,
                                  juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Pull the parameters once per block, and only re-derive filter
    // coefficients when something that feeds one has actually moved.
    if (epmk2::params::apply(state, params, lastParamValues))
        engine.configureAll(params);

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
