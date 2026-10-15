// Rendu hors ligne : extraits audio (WAV 24 bits) et capture de l'interface (PNG).
// Usage : HyperHatsRender <dossier de sortie>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double bpm = 190.0;

    struct Setting { const char* id; float value; };

    void apply (HyperHatsAudioProcessor& p, std::initializer_list<Setting> settings)
    {
        for (auto& s : settings)
        {
            auto* param = p.apvts.getParameter (s.id);
            param->setValueNotifyingHost (param->convertTo0to1 (s.value));
        }
    }

    /** Motif hi-tech sur 2 mesures : closed en double-croches, open sur les contretemps, accents, un roll en triples-croches à la fin. */
    juce::MidiBuffer pattern (int& lengthOut)
    {
        juce::MidiBuffer midi;
        const double sixteenth = 60.0 / bpm / 4.0 * sampleRate;
        const int bars = 2;

        for (int i = 0; i < 16 * bars; ++i)
        {
            const int pos = juce::roundToInt (i * sixteenth);
            const bool open = (i % 4) == 2;
            const float velocity = open ? 0.95f : ((i % 2) == 0 ? 0.85f : 0.6f);

            if (i >= 16 * bars - 4)   // roll final
            {
                for (int k = 0; k < 2; ++k)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 42, 0.55f + 0.1f * k), juce::roundToInt (pos + k * sixteenth * 0.5));
                continue;
            }

            midi.addEvent (juce::MidiMessage::noteOn (1, open ? 64 : 42, velocity), pos);   // la hauteur est ignorée : voir renderTo
        }

        lengthOut = juce::roundToInt (16 * bars * sixteenth + sampleRate * 0.8);
        return midi;
    }

    /** Deux HyperHats aux mêmes réglages, l'un en CLOSED, l'autre en OPEN, comme dans un morceau. */
    void renderTo (HyperHatsAudioProcessor& p, const juce::File& file)
    {
        HyperHatsAudioProcessor openHat;
        juce::MemoryBlock state;
        p.getStateInformation (state);
        openHat.setStateInformation (state.getData(), (int) state.getSize());
        apply (p, { { HyperHatsAudioProcessor::typeId, 0.0f } });
        apply (openHat, { { HyperHatsAudioProcessor::typeId, 1.0f } });

        for (auto* proc : { &p, &openHat })
        {
            proc->setRateAndBufferSizeDetails (sampleRate, 512);
            proc->prepareToPlay (sampleRate, 512);
        }

        int length = 0;
        auto midi = pattern (length);
        juce::AudioBuffer<float> out (2, length);
        out.clear();
        juce::AudioBuffer<float> block (2, 512);

        for (int pos = 0; pos < length; pos += 512)
        {
            const int n = juce::jmin (512, length - pos);

            for (auto* proc : { &p, &openHat })
            {
                const bool isOpen = proc == &openHat;
                block.setSize (2, n, false, false, true);
                juce::MidiBuffer part;
                for (const auto m : midi)
                    if (m.samplePosition >= pos && m.samplePosition < pos + n && (m.getMessage().getNoteNumber() == 64) == isOpen)
                        part.addEvent (m.getMessage(), m.samplePosition - pos);

                proc->processBlock (block, part);
                for (int ch = 0; ch < 2; ++ch)
                    out.addFrom (ch, pos, block, ch, 0, n);
            }
        }

        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (out, 0, length);

        std::printf ("%s : crête %.1f dBFS\n", file.getFileName().toRawUTF8(), juce::Decibels::gainToDecibels (out.getMagnitude (0, length)));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();

    using P = HyperHatsAudioProcessor;
    const char* names[4] = { "02-808", "01-tight", "03-sparkle", "04-real" };

    for (int i = 0; i < 4; ++i)
    {
        P p;
        p.applyPreset (i);
        renderTo (p, dir.getChildFile (juce::String (names[i]) + ".wav"));
    }

    {
        P p;
        p.applyPreset (1);
        apply (p, { { P::alloyId, 2.0f }, { P::sourceId, 15.0f }, { P::crushId, 55.0f }, { P::colorId, 7000.0f }, { P::openId, 250.0f } });
        renderTo (p, dir.getChildFile ("05-ring-crush.wav"));
    }

    {
        P p;
        p.applyPreset (1);
        apply (p, { { P::shapeId, 100.0f }, { P::openId, 140.0f }, { P::closedId, 14.0f }, { P::snapId, 100.0f }, { P::widthId, 80.0f } });
        renderTo (p, dir.getChildFile ("06-gated-tight.wav"));
    }

    // Capture de l'interface
    {
        P p;
        p.setRateAndBufferSizeDetails (sampleRate, 512);
        p.prepareToPlay (sampleRate, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
        auto snapshot = [&] (const char* name)
        {
            auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            auto file = dir.getChildFile (name);
            file.deleteFile();
            if (auto stream = file.createOutputStream())
                png.writeImageToStream (image, *stream);
        };

        snapshot ("interface.png");

        // Barre des presets : un preset utilisateur modifié, puis le panneau SAVE
        const auto presetDir = dir.getChildFile ("presets");
        presetDir.deleteRecursively();
        p.presets.setFolderOverride (presetDir);
        apply (p, { { P::alloyId, 2.0f }, { P::characterId, 70.0f } });
        p.presets.save ("Darkpsy Tick 190");
        apply (p, { { P::tuneId, 3.0f } });

        if (auto* facade = dynamic_cast<HyperHatsAudioProcessorEditor*> (editor.get()))
        {
            auto& f = facade->getFacade();
            for (int i = 0; i < 3; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);   // laisse le minuteur rafraîchir
            snapshot ("interface-preset.png");

            if (auto* save = dynamic_cast<juce::TextButton*> (f.findChildWithID ("presetSave")))
                save->onClick();
            snapshot ("interface-save.png");
        }

        presetDir.deleteRecursively();
        p.editorBeingDeleted (editor.get());
    }

    return 0;
}
