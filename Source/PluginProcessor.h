#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "HatEngine.h"
#include "PresetLibrary.h"

/**
    HyperHats : instrument VST3 de charleston en synthèse pure.
    MIDI : n'importe quelle note joue le même son, celui choisi par TYPE (CLOSED ou OPEN).
    Seule la vélocité compte. Une frappe coupe la précédente.
*/
class HyperHatsAudioProcessor final : public juce::AudioProcessor
{
public:
    //==========================================================================
    // Identifiants des paramètres (ne pas les renommer : les projets DAW sauvegardés en dépendent)
    static constexpr const char* typeId      = "type";
    static constexpr const char* alloyId     = "alloy";
    static constexpr const char* sourceId    = "source";
    static constexpr const char* characterId = "character";
    static constexpr const char* tuneId      = "tune";
    static constexpr const char* colorId     = "color";
    static constexpr const char* snapId      = "snap";
    static constexpr const char* closedId    = "closed";
    static constexpr const char* openId      = "open";
    static constexpr const char* shapeId     = "shape";
    static constexpr const char* widthId     = "width";
    static constexpr const char* variationId = "variation";
    static constexpr const char* crushId     = "crush";
    static constexpr const char* levelId     = "level";

    //==========================================================================
    /** Points de départ (les boutons 808 / TIGHT / SPARKLE / REAL). */
    struct Preset
    {
        const char* name;
        int alloy;
        float source, character, tune, color, snap, closed, open, shape, width, variation, crush;
    };

    static const std::array<Preset, 4>& getPresets();
    void applyPreset (int index);
    /** Index du point de départ dont tous les réglages correspondent, sinon -1. */
    int getMatchingPreset() const;

    //==========================================================================
    HyperHatsAudioProcessor();
    ~HyperHatsAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "HyperHats"; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 7.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Points de départ + presets de l'utilisateur (fichiers .hhpreset). */
    PresetLibrary presets;

    //==========================================================================
    // Pour l'interface
    /** Demande une frappe d'écoute (bouton HIT de l'interface), jouée au prochain bloc. */
    void requestPreview() noexcept                          { previewRequest.store (1, std::memory_order_relaxed); }
    /** TYPE choisi : true = open. */
    bool isOpenSelected() const noexcept                    { return typeParam->load() > 0.5f; }
    uint32_t getHitCount() const noexcept                   { return hitCount.load (std::memory_order_relaxed); }
    bool wasLastHitOpen() const noexcept                    { return lastHitOpen.load (std::memory_order_relaxed); }

    /** Réglages actuels du moteur, lus dans les paramètres. */
    hyperhats::HatParams readParams() const;

    /** Texte d'une durée en ms ("45 ms", "1.20 s"). */
    static juce::String msToText (float ms);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void renderChunk (juce::AudioBuffer<float>&, int start, int length);

    hyperhats::HatEngine engine;
    std::vector<float> scratchL, scratchR;

    std::atomic<float>* typeParam = nullptr;
    std::atomic<float>* alloyParam = nullptr;
    std::atomic<float>* sourceParam = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* tuneParam = nullptr;
    std::atomic<float>* colorParam = nullptr;
    std::atomic<float>* snapParam = nullptr;
    std::atomic<float>* closedParam = nullptr;
    std::atomic<float>* openParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* variationParam = nullptr;
    std::atomic<float>* crushParam = nullptr;
    std::atomic<float>* levelParam = nullptr;

    std::atomic<int> previewRequest { 0 };
    std::atomic<uint32_t> hitCount { 0 };
    std::atomic<bool> lastHitOpen { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HyperHatsAudioProcessor)
};
