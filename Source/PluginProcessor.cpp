#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    float textToNumber (const juce::String& text)
    {
        return text.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue();
    }

    float textToTime (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
        return (t.endsWithChar ('s') && ! t.endsWith ("ms")) ? value * 1000.0f : value;
    }

    juce::String frequencyToText (float hertz, int)
    {
        return juce::String (hertz / 1000.0f, 1) + " kHz";
    }

    float textToFrequency (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
        return (t.containsChar ('k') || value < 100.0f) ? value * 1000.0f : value;
    }

    /** Plage exponentielle (temps, fréquences) : le milieu de la course est la moyenne géométrique. */
    juce::NormalisableRange<float> makeLogRange (float lowest, float highest, float step)
    {
        return { lowest, highest,
                 [] (float start, float end, float normalised)
                 {
                     return start * std::pow (end / start, juce::jlimit (0.0f, 1.0f, normalised));
                 },
                 [] (float start, float end, float value)
                 {
                     return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (start, end, value) / start) / std::log (end / start));
                 },
                 [step] (float start, float end, float value)
                 {
                     return juce::jlimit (start, end, std::round (value / step) * step);
                 } };
    }

    std::unique_ptr<juce::AudioParameterFloat> makePercent (const char* id, const char* name, float defaultPercent)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), defaultPercent,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; })
                                                                .withValueFromStringFunction (textToNumber));
    }

    std::unique_ptr<juce::AudioParameterFloat> makeTime (const char* id, const char* name, float lowest, float highest, float defaultMs)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            makeLogRange (lowest, highest, 0.1f), defaultMs,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float ms, int) { return HyperHatsAudioProcessor::msToText (ms); })
                                                                .withValueFromStringFunction (textToTime));
    }
}

//==============================================================================
const std::array<HyperHatsAudioProcessor::Preset, 4>& HyperHatsAudioProcessor::getPresets()
{
    //                                       alloy  src  char tune  color    snap closed open  shape width var crush
    static const std::array<Preset, 4> presets { {
        { "808",     hyperhats::alloySquare, 30,  10,  0.0f, 6000.0f, 40,  45,    450,  20,   20,   10,  0  },
        { "TIGHT",   hyperhats::alloyFm,     50,  35,  0.0f, 9500.0f, 75,  20,    300,  70,   35,   25,  10 },
        { "SPARKLE", hyperhats::alloyFm,     25,  55,  2.0f, 11500.0f, 55, 35,    600,  30,   70,   35,  0  },
        { "REAL",    hyperhats::alloySquare, 45,  90, -1.0f, 7000.0f, 50,  60,    900,  10,   45,   40,  0  }
    } };

    return presets;
}

namespace
{
    template <typename Fn>
    void forEachPresetValue (const HyperHatsAudioProcessor::Preset& p, Fn&& fn)
    {
        fn (HyperHatsAudioProcessor::alloyId, (float) p.alloy);
        fn (HyperHatsAudioProcessor::sourceId, p.source);
        fn (HyperHatsAudioProcessor::characterId, p.character);
        fn (HyperHatsAudioProcessor::tuneId, p.tune);
        fn (HyperHatsAudioProcessor::colorId, p.color);
        fn (HyperHatsAudioProcessor::snapId, p.snap);
        fn (HyperHatsAudioProcessor::closedId, p.closed);
        fn (HyperHatsAudioProcessor::openId, p.open);
        fn (HyperHatsAudioProcessor::shapeId, p.shape);
        fn (HyperHatsAudioProcessor::widthId, p.width);
        fn (HyperHatsAudioProcessor::variationId, p.variation);
        fn (HyperHatsAudioProcessor::crushId, p.crush);
    }
}

void HyperHatsAudioProcessor::applyPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) getPresets().size()))
        return;

    forEachPresetValue (getPresets()[(size_t) index], [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            param->endChangeGesture();
        }
    });
}

int HyperHatsAudioProcessor::getMatchingPreset() const
{
    for (int i = 0; i < (int) getPresets().size(); ++i)
    {
        bool same = true;

        forEachPresetValue (getPresets()[(size_t) i], [this, &same] (const char* id, float value)
        {
            if (auto* param = apvts.getParameter (id))
                if (std::abs (param->getValue() - param->convertTo0to1 (value)) > 0.002f)
                    same = false;
        });

        if (same)
            return i;
    }

    return -1;
}

//==============================================================================
juce::String HyperHatsAudioProcessor::msToText (float ms)
{
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 2) + " s";

    return juce::String (juce::roundToInt (ms)) + " ms";
}

juce::AudioProcessorValueTreeState::ParameterLayout HyperHatsAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { typeId, 1 }, "Type",
                                                              juce::StringArray { "Closed", "Open" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { alloyId, 1 }, "Alloy",
                                                              juce::StringArray { "Square", "FM", "Ring" }, hyperhats::alloyFm));
    layout.add (makePercent (sourceId, "Source (metal / noise)", 50.0f));
    layout.add (makePercent (characterId, "Character (synth / acoustic)", 35.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { tuneId, 1 }, "Tune",
                                                             juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float st, int)
                                                                 {
                                                                     const bool whole = std::abs (st - std::round (st)) < 0.005f;
                                                                     const auto number = whole ? juce::String (juce::roundToInt (st)) : juce::String (st, 2);
                                                                     return (st > 0.004f ? "+" : "") + number + " st";
                                                                 })
                                                                 .withValueFromStringFunction (textToNumber)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { colorId, 1 }, "Color",
                                                             makeLogRange (2000.0f, 16000.0f, 1.0f), 9500.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction (frequencyToText)
                                                                 .withValueFromStringFunction (textToFrequency)));

    layout.add (makePercent (snapId, "Snap", 75.0f));
    layout.add (makeTime (closedId, "Closed decay", 10.0f, 300.0f, 20.0f));
    layout.add (makeTime (openId, "Open decay", 80.0f, 2500.0f, 300.0f));
    layout.add (makePercent (shapeId, "Shape (natural / gated)", 70.0f));
    layout.add (makePercent (widthId, "Width", 35.0f));
    layout.add (makePercent (variationId, "Variation", 25.0f));
    layout.add (makePercent (crushId, "Crush", 10.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { levelId, 1 }, "Level",
                                                             juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float db, int)
                                                                 {
                                                                     return (db > 0.04f ? "+" : "") + juce::String (db, 1) + " dB";
                                                                 })
                                                                 .withValueFromStringFunction (textToNumber)));
    return layout;
}

//==============================================================================
HyperHatsAudioProcessor::HyperHatsAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HyperHats", createParameterLayout()),
      presets (apvts, "HyperHats", ".hhpreset",
               { [] { return (int) getPresets().size(); },
                 [] (int i) { return juce::String (getPresets()[(size_t) i].name); },
                 [this] (int i) { applyPreset (i); },
                 [this] { return getMatchingPreset(); } })
{
    typeParam      = apvts.getRawParameterValue (typeId);
    alloyParam     = apvts.getRawParameterValue (alloyId);
    sourceParam    = apvts.getRawParameterValue (sourceId);
    characterParam = apvts.getRawParameterValue (characterId);
    tuneParam      = apvts.getRawParameterValue (tuneId);
    colorParam     = apvts.getRawParameterValue (colorId);
    snapParam      = apvts.getRawParameterValue (snapId);
    closedParam    = apvts.getRawParameterValue (closedId);
    openParam      = apvts.getRawParameterValue (openId);
    shapeParam     = apvts.getRawParameterValue (shapeId);
    widthParam     = apvts.getRawParameterValue (widthId);
    variationParam = apvts.getRawParameterValue (variationId);
    crushParam     = apvts.getRawParameterValue (crushId);
    levelParam     = apvts.getRawParameterValue (levelId);

    engine.setParams (readParams());
    presets.restoreFromState();   // réglages par défaut = TIGHT
}

hyperhats::HatParams HyperHatsAudioProcessor::readParams() const
{
    hyperhats::HatParams p;
    p.alloy     = juce::jlimit (0, 2, juce::roundToInt (alloyParam->load()));
    p.source    = sourceParam->load() * 0.01f;
    p.character = characterParam->load() * 0.01f;
    p.tune      = tuneParam->load();
    p.colorHz   = colorParam->load();
    p.snap      = snapParam->load() * 0.01f;
    p.closedMs  = closedParam->load();
    p.openMs    = openParam->load();
    p.shape     = shapeParam->load() * 0.01f;
    p.width     = widthParam->load() * 0.01f;
    p.variation = variationParam->load() * 0.01f;
    p.crush     = crushParam->load() * 0.01f;
    p.levelDb   = levelParam->load();
    return p;
}

void HyperHatsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto size = (size_t) juce::jmax (512, samplesPerBlock);
    scratchL.assign (size, 0.0f);
    scratchR.assign (size, 0.0f);

    engine.setParams (readParams());
    engine.prepare (sampleRate);
}

void HyperHatsAudioProcessor::reset()
{
    engine.reset();
}

bool HyperHatsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.inputBuses.size() > 0 && ! layouts.getMainInputChannelSet().isDisabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void HyperHatsAudioProcessor::renderChunk (juce::AudioBuffer<float>& buffer, int start, int length)
{
    const int channels = buffer.getNumChannels();

    while (length > 0)
    {
        const int n = juce::jmin (length, (int) scratchL.size());
        engine.process (scratchL.data(), scratchR.data(), n);

        if (channels >= 2)
        {
            buffer.copyFrom (0, start, scratchL.data(), n);
            buffer.copyFrom (1, start, scratchR.data(), n);
        }
        else if (channels == 1)
        {
            auto* out = buffer.getWritePointer (0, start);
            for (int i = 0; i < n; ++i)
                out[i] = 0.5f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
        }

        start += n;
        length -= n;
    }
}

void HyperHatsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (scratchL.empty())   // l'hôte n'a pas appelé prepareToPlay
        return;

    engine.setParams (readParams());

    const bool open = isOpenSelected();

    if (previewRequest.exchange (0, std::memory_order_relaxed) != 0)
        engine.trigger (open, 0.9f);

    int position = 0;

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const int at = juce::jlimit (position, numSamples, metadata.samplePosition);

        if (message.isNoteOn())
        {
            renderChunk (buffer, position, at - position);
            position = at;
            engine.trigger (open, message.getFloatVelocity());   // la hauteur de la note est ignorée
        }
        else if (message.isAllSoundOff())
        {
            renderChunk (buffer, position, at - position);
            position = at;
            engine.reset();
        }
    }

    renderChunk (buffer, position, numSamples - position);

    hitCount.store (engine.getHitCount(), std::memory_order_relaxed);
    lastHitOpen.store (engine.wasLastHitOpen(), std::memory_order_relaxed);
}

void HyperHatsAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    engine.reset();
}

//==============================================================================
void HyperHatsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HyperHatsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.restoreFromState();
        }
}

juce::AudioProcessorEditor* HyperHatsAudioProcessor::createEditor()
{
    return new HyperHatsAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HyperHatsAudioProcessor();
}
