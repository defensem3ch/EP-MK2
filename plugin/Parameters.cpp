#include "Parameters.h"

namespace epmk2::params {

bool apply(const juce::AudioProcessorValueTreeState& tree,
           EngineParams& p,
           std::vector<float>& previous)
{
    const auto& specs = table();
    if (previous.size() != specs.size())
        previous.assign(specs.size(), std::numeric_limits<float>::quiet_NaN());

    bool coefficientsChanged = false;

    for (size_t i = 0; i < specs.size(); ++i) {
        const Spec& s = specs[i];
        const auto* raw = tree.getRawParameterValue(s.id);
        if (raw == nullptr)
            continue;

        const float v = raw->load();
        if (v != previous[i]) {
            previous[i] = v;
            if (s.affectsCoefficients)
                coefficientsChanged = true;
        }

        const float lin = toLinear(s, v);
        const juce::String id(s.id);

        // Tuning
        if      (id == "bass_freq")       p.baseFreq = v;
        else if (id == "base_note")       p.baseNote = v;
        else if (id == "divisions")       p.divisions = v;
        else if (id == "interval")        p.interval = v;
        // Tine
        else if (id == "tine_ratio1")     p.voice.tineRatio1 = v;
        else if (id == "tine_ratio2")     p.voice.tineRatio2 = v;
        else if (id == "tine_ratio3")     p.voice.tineRatio3 = v;
        else if (id == "tine_mode2_lvl")  p.voice.tineMode2LevelLin = lin;
        else if (id == "tine_mode3_lvl")  p.voice.tineMode3LevelLin = lin;
        else if (id == "tine_mode_damp")  p.voice.tineModeDamping = v;
        else if (id == "tine_hipass")     p.voice.tineHighpassHz = v;
        else if (id == "tine_decay")      p.voice.tineQ = v;
        else if (id == "tine_level")      p.voice.tineLevelLin = lin;
        else if (id == "tine_send")       p.voice.tineSendLin = lin;
        // Tone bar
        else if (id == "tone_decay")      p.voice.toneQ = v;
        else if (id == "tone_release")    p.voice.toneReleaseQ = v;
        else if (id == "tone_level")      p.voice.toneLevelLin = lin;
        else if (id == "hammer_level")    p.voice.hammerLevelLin = lin;
        else if (id == "noteoff_level")   p.voice.noteOffLevelLin = lin;
        else if (id == "hammer_contact")  p.voice.hammerContactMs = v;
        else if (id == "hammer_vel_ctc")  p.voice.hammerVelContact = v;
        // Pickup
        else if (id == "pickup_gain")     p.voice.pickupGainLin = lin;
        else if (id == "pickup_attack")   p.voice.pickupAttackLin = lin;
        else if (id == "pickup_lopass")   p.voice.pickupLowpassHz = v;
        else if (id == "pickup_symmetry") p.voice.pickupSymmetryLin = lin;
        else if (id == "pickup_level")    p.voice.pickupLevelLin = lin;
        else if (id == "buzz_level")      p.voice.buzzLevelLin = lin;
        // The panel toggle sends 0/1; the patch maps that to -1/+1 via [* 2]->[- 1].
        else if (id == "buzz_phase")      p.voice.buzzPhase = v > 0.5f ? 1.0f : -1.0f;
        else if (id == "sustain")         p.voice.sustainPedal = v > 0.5f;
        // Tremolo
        else if (id == "trem_on")         p.tremoloOn = v > 0.5f;
        else if (id == "trem_rate")       p.tremoloRateHz = v;
        else if (id == "trem_shape")      p.tremoloShape = v;
        else if (id == "trem_depth")      p.tremoloDepthLin = lin;
        // Output
        else if (id == "master")          p.masterLin = lin;
        else if (id == "polyphony")       p.polyphony = (int)(v + 0.5f);
    }

    return coefficientsChanged;
}

} // namespace epmk2::params
