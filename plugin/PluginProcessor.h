#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <clap-juce-extensions/clap-juce-extensions.h>

#include "../dsp/Engine.h"
#include "Parameters.h"

// MIDI-in / audio-out wrapper around the DSP engine, with the model's controls
// exposed as host parameters (see Parameters.h for the table).
class EpMk2Processor : public juce::AudioProcessor,
                       public clap_juce_extensions::clap_juce_audio_processor_capabilities
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

    // Editor size lives in a settings file in the user's config directory,
    // not in the session: it outlives every instance, so a freshly added one
    // opens at the size last used rather than the factory default.
    juce::Point<int> getSavedEditorSize() const;
    void saveEditorSize(int width, int height);
    // Write the settings file now.  Resizes are batched behind a timer, which
    // will not have fired if the window is closed or the instance removed
    // straight after a resize -- which is exactly when it matters.
    void flushSettings();

    bool isBusesLayoutSupported(const BusesProperties&) const { return true; }

    // A CLAP host loading one of the factory presets.  The presets live in
    // the binary rather than in files, so the location is PLUGIN and the
    // load_key is the preset's name -- see ClapPresets.cpp.
    bool supportsPresetLoad() const noexcept override { return true; }
    bool presetLoadFromLocation(uint32_t locationKind, const char* location,
                                const char* loadKey) noexcept override;

    // The tuning, when it is not the equal divisions the parameters describe.
    // An empty scale means it is.  Message thread only.
    const epmk2::Scale& getScale() const noexcept { return scale; }
    void setScale(const epmk2::Scale&);

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

    // Where MIDI last put the wheel, and what was last pushed into the
    // parameter from it.  Kept apart so the parameter is only written when
    // the wheel actually moves -- otherwise every block would overwrite the
    // knob with a stale wheel position and it could never be used by hand.
    float pendingBend = 0.0f;
    float lastBendWritten = 0.0f;

    bool ccSustain = false;      // from MIDI CC64
    bool paramSustain = false;   // from the panel toggle
    bool pedalDown = false;      // the two combined, as last applied

    epmk2::Engine engine;
    epmk2::EngineParams params;

    // The scale, published to the audio thread without a lock.
    //
    // The audio thread reads a plain array it is handed a pointer to, so
    // nothing it is reading can be reallocated underneath it.  Three slots
    // rather than two: with two, a second change arriving while a block was
    // still running would land in the very slot that block is reading.  The
    // writer round-robins, so a slot cannot come up again until two further
    // changes have happened, which at the speed a person picks a tuning is
    // never.
    //
    // 1024 is the parser's own limit on degrees, so a parsed scale always
    // fits and the audio thread never has to check.
    struct ScaleSlot {
        double cents[1024] = {};
        int degrees = 0;
    };
    ScaleSlot scaleSlots[3];
    std::atomic<int> liveScale { -1 };   // -1 until a scale is loaded
    int nextScaleSlot = 0;

    epmk2::Scale scale;   // the message thread's copy: name, and for the state

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpMk2Processor)
};
