#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../dsp/Engine.h"
#include "Parameters.h"

// MIDI-in / audio-out wrapper around the DSP engine, with the model's controls
// exposed as host parameters (see Parameters.h for the table).
class EpMk2Processor : public juce::AudioProcessor
{
public:
    EpMk2Processor();
    ~EpMk2Processor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    int getActiveVoiceCount() const noexcept { return engine.activeVoices(); }

    const juce::String getName() const override { return "EP-MK2"; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

    bool isBusesLayoutSupported(const BusesProperties&) const { return true; }

private:
    void handleMidi(const juce::MidiMessage& m);

    juce::AudioProcessorValueTreeState state;
    std::vector<float> lastParamValues;

    epmk2::Engine engine;
    epmk2::EngineParams params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpMk2Processor)
};
