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

    // Factory presets (see Presets.h).  Hosts show these in their own preset
    // menu, so they work in VST3 and LV2 without preset files on disk.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

    // Editor size is remembered in two places, and they answer different
    // questions.  The value tree travels with the session, so reopening a
    // saved project restores the window it was saved with.  A settings file in
    // the user's config directory outlives every instance, so *adding a fresh
    // instance* gets the size last used rather than the factory default --
    // which the value tree alone cannot do, because a new instance has no
    // state to restore from.  Session state wins where both exist.
    juce::Point<int> getSavedEditorSize() const;
    void saveEditorSize(int width, int height);
    // Write the settings file now.  Resizes are batched behind a timer, which
    // will not have fired if the window is closed or the instance removed
    // straight after a resize -- which is exactly when it matters.
    void flushSettings();

    bool isBusesLayoutSupported(const BusesProperties&) const { return true; }

private:
    void handleMidi(const juce::MidiMessage& m);
    // The pedal can be put down by MIDI CC64 or by the panel toggle, and the
    // two have to be combined rather than one overwriting the other.
    void updatePedal();

    // Shared across every instance in the process, so they do not fight over
    // the file.
    struct Settings
    {
        Settings();
        juce::Point<int> editorSize() const;
        void setEditorSize(int width, int height);
        juce::ApplicationProperties properties;
    };
    juce::SharedResourcePointer<Settings> settings;

    juce::AudioProcessorValueTreeState state;
    std::vector<float> lastParamValues;
    int currentProgram = 0;

    bool ccSustain = false;      // from MIDI CC64
    bool paramSustain = false;   // from the panel toggle
    bool pedalDown = false;      // the two combined, as last applied

    epmk2::Engine engine;
    epmk2::EngineParams params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpMk2Processor)
};
