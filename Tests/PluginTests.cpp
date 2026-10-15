// Tests du plugin complet (paramètres, MIDI, état, interface).

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cstdio>

#define PRESET_EXT ".hhpreset"
#define PLUGIN_NAME "HyperHats"
#define EDITOR_CLASS HyperHatsAudioProcessorEditor
#define P_A_ID HyperHatsAudioProcessor::tuneId
#define P_A_VALUE 5.0f
#define P_A_OTHER -3.0f
#define P_LEVEL_ID HyperHatsAudioProcessor::levelId

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  ECHEC (%s:%d) ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    struct Note { int at; int number; float velocity; };

    /** Rend `samples` échantillons en blocs de `block`, avec les notes données (positions absolues). */
    juce::AudioBuffer<float> run (HyperHatsAudioProcessor& p, int samples, const std::vector<Note>& notes, int block = blockSize, int channels = 2)
    {
        juce::AudioBuffer<float> out (channels, samples);
        out.clear();
        juce::AudioBuffer<float> buffer (channels, block);

        for (int pos = 0; pos < samples; pos += block)
        {
            const int n = juce::jmin (block, samples - pos);
            buffer.setSize (channels, n, false, false, true);
            juce::MidiBuffer midi;

            for (auto& note : notes)
                if (note.at >= pos && note.at < pos + n)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note.number, note.velocity), note.at - pos);

            p.processBlock (buffer, midi);

            for (int ch = 0; ch < channels; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }

        return out;
    }

    double rms (const juce::AudioBuffer<float>& b, int from, int to, int channel = 0)
    {
        to = juce::jmin (to, b.getNumSamples());
        if (to <= from) return 0.0;
        double s = 0;
        auto* d = b.getReadPointer (channel);
        for (int i = from; i < to; ++i) s += (double) d[i] * d[i];
        return std::sqrt (s / (to - from));
    }

    void setParam (HyperHatsAudioProcessor& p, const char* id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    std::printf ("Paramètres\n");
    {
        HyperHatsAudioProcessor p;
        CHECK (p.getParameters().size() == 14, "%d paramètres au lieu de 14", p.getParameters().size());

        for (auto* id : { HyperHatsAudioProcessor::typeId, HyperHatsAudioProcessor::alloyId, HyperHatsAudioProcessor::sourceId, HyperHatsAudioProcessor::characterId,
                          HyperHatsAudioProcessor::tuneId, HyperHatsAudioProcessor::colorId, HyperHatsAudioProcessor::snapId,
                          HyperHatsAudioProcessor::closedId, HyperHatsAudioProcessor::openId, HyperHatsAudioProcessor::shapeId,
                          HyperHatsAudioProcessor::widthId, HyperHatsAudioProcessor::variationId, HyperHatsAudioProcessor::crushId,
                          HyperHatsAudioProcessor::levelId })
            CHECK (p.apvts.getParameter (id) != nullptr, "paramètre %s absent", id);

        CHECK (p.getMatchingPreset() == 1, "le réglage par défaut devrait être TIGHT (%d)", p.getMatchingPreset());

        for (int i = 0; i < 4; ++i)
        {
            p.applyPreset (i);
            CHECK (p.getMatchingPreset() == i, "point de départ %d non reconnu après application", i);
        }

        // Textes et saisie
        auto* color = p.apvts.getParameter (HyperHatsAudioProcessor::colorId);
        CHECK (color->getText (color->convertTo0to1 (9500.0f), 32) == "9.5 kHz", "texte de COLOR : %s", color->getText (color->convertTo0to1 (9500.0f), 32).toRawUTF8());
        CHECK (std::abs (color->convertFrom0to1 (color->getValueForText ("12k")) - 12000.0f) < 2.0f, "saisie 12k");
        auto* open = p.apvts.getParameter (HyperHatsAudioProcessor::openId);
        CHECK (open->getText (open->convertTo0to1 (1200.0f), 32) == "1.20 s", "texte de OPEN : %s", open->getText (open->convertTo0to1 (1200.0f), 32).toRawUTF8());
        CHECK (std::abs (open->convertFrom0to1 (open->getValueForText ("1.5 s")) - 1500.0f) < 1.0f, "saisie 1.5 s");
        auto* tune = p.apvts.getParameter (HyperHatsAudioProcessor::tuneId);
        CHECK (tune->getText (tune->convertTo0to1 (3.0f), 32) == "+3 st", "texte de TUNE : %s", tune->getText (tune->convertTo0to1 (3.0f), 32).toRawUTF8());
    }

    std::printf ("MIDI : toutes les notes jouent le même son, choisi par TYPE\n");
    {
        HyperHatsAudioProcessor p;
        p.setRateAndBufferSizeDetails (sampleRate, blockSize);
        p.prepareToPlay (sampleRate, blockSize);
        CHECK (! p.isOpenSelected(), "TYPE devrait être CLOSED par défaut");

        auto silent = run (p, 4800, {});
        CHECK (silent.getMagnitude (0, 4800) == 0.0f, "son sans note");

        // Même son, note à note, quelle que soit la hauteur
        for (bool open : { false, true })
        {
            setParam (p, HyperHatsAudioProcessor::typeId, open ? 1.0f : 0.0f);
            setParam (p, HyperHatsAudioProcessor::variationId, 0.0f);
            setParam (p, HyperHatsAudioProcessor::widthId, 0.0f);
            juce::AudioBuffer<float> reference;
            for (int note : { 0, 36, 42, 60, 64, 96, 127 })
            {
                p.reset();
                HyperHatsAudioProcessor fresh;   // même graine de frappe pour chaque note
                fresh.setStateInformation (nullptr, 0);
                juce::MemoryBlock state;
                p.getStateInformation (state);
                fresh.setStateInformation (state.getData(), (int) state.getSize());
                fresh.setRateAndBufferSizeDetails (sampleRate, blockSize);
                fresh.prepareToPlay (sampleRate, blockSize);
                auto out = run (fresh, 24000, { { 100, note, 0.8f } });

                if (reference.getNumSamples() == 0)
                    reference = out;
                else
                {
                    float diff = 0;
                    for (int i = 0; i < out.getNumSamples(); ++i)
                        diff = juce::jmax (diff, std::abs (out.getSample (0, i) - reference.getSample (0, i)));
                    CHECK (diff == 0.0f, "la note %d sonne différemment (écart %.2e)", note, diff);
                }
            }

            const double tail = rms (reference, 100 + 4800, 100 + 9600);
            if (open) CHECK (tail > 1e-3, "TYPE OPEN : pas de queue (%.2e)", tail);
            else      CHECK (tail < 1e-5, "TYPE CLOSED : queue trop longue (%.2e)", tail);
        }

        setParam (p, HyperHatsAudioProcessor::typeId, 0.0f);

        // Position exacte
        p.reset();
        auto exact = run (p, 2048, { { 700, 72, 1.0f } });
        int first = -1;
        for (int i = 0; i < 2048; ++i) if (exact.getSample (0, i) != 0.0f) { first = i; break; }
        CHECK (first >= 700 && first <= 702, "première valeur non nulle à %d (attendu 700)", first);

        // Note-on de vélocité nulle = note-off : rien ne doit sonner
        p.reset();
        auto zero = run (p, 4800, { { 100, 64, 0.0f } });
        CHECK (zero.getMagnitude (0, 4800) == 0.0f, "une vélocité nulle déclenche une frappe");

        // Une frappe coupe la précédente
        setParam (p, HyperHatsAudioProcessor::typeId, 1.0f);
        p.reset();
        auto open = run (p, 48000, { { 0, 62, 1.0f } });
        setParam (p, HyperHatsAudioProcessor::typeId, 0.0f);
        p.reset();
        auto cut = run (p, 48000, { { 0, 62, 1.0f } });
        CHECK (rms (open, 9600, 14400) > 1e-3 && rms (cut, 9600, 14400) < 1e-5, "TYPE ne change pas la durée");
    }

    std::printf ("Écoute depuis l'interface, coupure, contournement, sortie mono, blocs variables\n");
    {
        HyperHatsAudioProcessor p;
        p.setRateAndBufferSizeDetails (sampleRate, blockSize);
        p.prepareToPlay (sampleRate, blockSize);

        const auto before = p.getHitCount();
        setParam (p, HyperHatsAudioProcessor::typeId, 1.0f);
        p.requestPreview();
        auto preview = run (p, 4800, {});
        CHECK (p.getHitCount() == before + 1 && p.wasLastHitOpen(), "la frappe d'écoute n'a pas joué");
        CHECK (preview.getMagnitude (0, 4800) > 0.05f, "frappe d'écoute silencieuse");

        // All sound off
        p.reset();
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 70, 1.0f), 0);
        p.processBlock (b, m);
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allSoundOff (1), 10);
        p.processBlock (b, off);
        CHECK (b.getMagnitude (0, 20, 492) == 0.0f, "All Sound Off n'arrête pas le son");

        // Contournement : silence
        p.reset();
        juce::MidiBuffer hit;
        hit.addEvent (juce::MidiMessage::noteOn (1, 70, 1.0f), 0);
        b.clear();
        p.processBlockBypassed (b, hit);
        CHECK (b.getMagnitude (0, 512) == 0.0f, "son en contournement");

        // Blocs plus grands que prévu
        p.reset();
        auto big = run (p, 20000, { { 5000, 40, 1.0f }, { 15000, 70, 1.0f } }, 8192);
        CHECK (big.getMagnitude (0, 5000) == 0.0f && big.getMagnitude (5000, 2000) > 0.05f && big.getMagnitude (15000, 2000) > 0.05f, "blocs de 8192 mal gérés");

        // Mono
        HyperHatsAudioProcessor mono;
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (mono.setBusesLayout (layout), "sortie mono refusée");
        mono.setRateAndBufferSizeDetails (sampleRate, blockSize);
        mono.prepareToPlay (sampleRate, blockSize);
        auto monoOut = run (mono, 4800, { { 0, 70, 1.0f } }, blockSize, 1);
        CHECK (monoOut.getMagnitude (0, 4800) > 0.05f, "sortie mono silencieuse");

        // Entrée audio refusée
        juce::AudioProcessor::BusesLayout withInput;
        withInput.inputBuses.add (juce::AudioChannelSet::stereo());
        withInput.outputBuses.add (juce::AudioChannelSet::stereo());
        CHECK (! p.checkBusesLayoutSupported (withInput), "une entrée audio est acceptée");
    }

    std::printf ("Sauvegarde et rechargement\n");
    {
        HyperHatsAudioProcessor a;
        setParam (a, HyperHatsAudioProcessor::alloyId, 2.0f);
        setParam (a, HyperHatsAudioProcessor::colorId, 4321.0f);
        setParam (a, HyperHatsAudioProcessor::openId, 1234.0f);
        setParam (a, HyperHatsAudioProcessor::levelId, -7.5f);
        a.apvts.state.setProperty ("editorWidth", 850, nullptr);

        juce::MemoryBlock state;
        a.getStateInformation (state);

        HyperHatsAudioProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());

        for (auto* param : a.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param);
            auto* other = b.apvts.getParameter (withId->paramID);
            CHECK (std::abs (param->getValue() - other->getValue()) < 1e-6f, "%s non restauré", withId->paramID.toRawUTF8());
        }

        CHECK ((int) b.apvts.state.getProperty ("editorWidth") == 850, "taille de fenêtre non restaurée");

        // États abîmés : pas de plantage, réglages inchangés
        const char garbage[] = "not a state at all";
        b.setStateInformation (garbage, (int) sizeof (garbage));
        b.setStateInformation (nullptr, 0);
        juce::MemoryBlock truncated (state.getData(), state.getSize() / 2);
        b.setStateInformation (truncated.getData(), (int) truncated.getSize());
        CHECK (std::abs (b.apvts.getRawParameterValue (HyperHatsAudioProcessor::levelId)->load() + 7.5f) < 1e-4f, "un état abîmé a changé les réglages");
    }

    std::printf ("Presets utilisateur\n");
    {
        using Proc = HyperHatsAudioProcessor;
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PresetTests-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        dir.deleteRecursively();
        const auto ext = juce::String (PRESET_EXT);

        Proc p;
        p.presets.setFolderOverride (dir);
        const int factoryCount = (int) Proc::getPresets().size();

        // Nouvelle instance : point de départ par défaut reconnu, pas d'étoile
        CHECK (! p.presets.getCurrent().user && p.presets.getCurrent().factoryIndex == p.getMatchingPreset() && p.getMatchingPreset() >= 0,
               "nouvelle instance : point de départ par défaut non reconnu");
        CHECK (! p.presets.isModified(), "nouvelle instance marquée modifiée");
        CHECK (p.presets.getUserPresets().isEmpty(), "dossier de test pas vide");

        // Noms
        CHECK (PresetLibrary::cleanName ("  a/b:c  ").isNotEmpty() && ! PresetLibrary::cleanName ("a/b:c").containsAnyOf ("/:"), "nom mal nettoyé");
        CHECK (PresetLibrary::cleanName ("   ").isEmpty() && PresetLibrary::cleanName ("...").isEmpty(), "nom vide accepté");
        CHECK (! p.presets.save ("   "), "un nom vide a été enregistré");
        CHECK (PresetLibrary::cleanName (juce::String::repeatedString ("x", 200)).length() <= 48, "nom trop long");

        // Enregistrer : fichier créé, preset en cours, pas d'étoile
        setParam (p, P_A_ID, P_A_VALUE);
        setParam (p, P_LEVEL_ID, -7.5f);
        const float aBefore = p.apvts.getRawParameterValue (P_A_ID)->load();
        CHECK (p.presets.save ("Darkpsy Tick"), "enregistrement impossible");
        const auto file = dir.getChildFile ("Darkpsy Tick" + ext);
        CHECK (file.existsAsFile(), "fichier du preset absent");
        CHECK (p.presets.getCurrent().user && p.presets.getDisplayName() == "Darkpsy Tick", "preset en cours incorrect après enregistrement");
        CHECK (! p.presets.isModified(), "marqué modifié juste après l'enregistrement");

        // Modifier -> étoile ; recharger -> valeurs du fichier, plus d'étoile
        setParam (p, P_A_ID, P_A_OTHER);
        setParam (p, P_LEVEL_ID, 3.0f);
        CHECK (p.presets.isModified(), "modification non détectée");
        CHECK (p.presets.load (file), "rechargement impossible");
        CHECK (std::abs (p.apvts.getRawParameterValue (P_A_ID)->load() - aBefore) < 1e-3f, "valeur non restaurée");
        CHECK (std::abs (p.apvts.getRawParameterValue (P_LEVEL_ID)->load() + 7.5f) < 1e-3f, "LEVEL non restauré");
        CHECK (! p.presets.isModified(), "marqué modifié après chargement");

        // Tous les paramètres : enregistrer puis recharger donne exactement les mêmes valeurs
        {
            juce::Random r (7);
            for (auto* prm : p.getParameters())
                prm->setValueNotifyingHost (r.nextFloat());
            std::vector<float> saved;
            for (auto* prm : p.getParameters())
                saved.push_back (prm->getValue());
            CHECK (p.presets.save ("Random"), "enregistrement Random impossible");
            for (auto* prm : p.getParameters())
                prm->setValueNotifyingHost (prm->getDefaultValue());
            CHECK (p.presets.load (dir.getChildFile ("Random" + ext)), "chargement Random impossible");
            float worst = 0.0f;
            for (int i = 0; i < p.getParameters().size(); ++i)
                worst = juce::jmax (worst, std::abs (p.getParameters()[i]->getValue() - saved[(size_t) i]));
            CHECK (worst < 1e-4f, "aller-retour imparfait (écart %g)", (double) worst);
        }

        // Remplacer un preset existant
        setParam (p, P_A_ID, P_A_OTHER);
        CHECK (p.presets.exists ("Random") && p.presets.save ("Random"), "remplacement impossible");
        CHECK (p.presets.getUserPresets().size() == 2, "remplacer a créé un doublon (%d fichiers)", p.presets.getUserPresets().size());

        // Ordre ‹ › : usine puis utilisateur (tri par nom), en boucle
        {
            const auto list = p.presets.getAllEntries();
            CHECK ((int) list.size() == factoryCount + 2, "liste : %d entrées", (int) list.size());
            CHECK (list[(size_t) factoryCount].name == "Darkpsy Tick" && list[(size_t) factoryCount + 1].name == "Random", "tri des presets utilisateur");

            p.presets.loadFactory (0);
            p.presets.step (-1);
            CHECK (p.presets.getDisplayName() == "Random", "‹ depuis le premier ne boucle pas (%s)", p.presets.getDisplayName().toRawUTF8());
            p.presets.step (+1);
            CHECK (! p.presets.getCurrent().user && p.presets.getCurrent().factoryIndex == 0, "› depuis le dernier ne boucle pas");
            for (int i = 0; i < factoryCount; ++i)
                p.presets.step (+1);
            CHECK (p.presets.getDisplayName() == "Darkpsy Tick", "› ne passe pas aux presets utilisateur");
            CHECK (std::abs (p.apvts.getRawParameterValue (P_A_ID)->load() - aBefore) < 1e-3f, "› n'a pas chargé le preset");
            p.presets.loadFactory (1);
            CHECK (p.getMatchingPreset() == 1 && ! p.presets.isModified(), "point de départ via la bibliothèque");
        }

        // L'état du projet garde le preset en cours, et l'étoile
        {
            p.presets.load (file);
            setParam (p, P_A_ID, P_A_OTHER);
            juce::MemoryBlock state;
            p.getStateInformation (state);

            Proc q;
            q.presets.setFolderOverride (dir);
            q.setStateInformation (state.getData(), (int) state.getSize());
            CHECK (q.presets.getCurrent().user && q.presets.getDisplayName() == "Darkpsy Tick", "nom du preset perdu avec le projet");
            CHECK (q.presets.isModified(), "l'étoile est perdue avec le projet");

            p.presets.load (file);
            p.getStateInformation (state);
            Proc r;
            r.setStateInformation (state.getData(), (int) state.getSize());
            CHECK (r.presets.getDisplayName() == "Darkpsy Tick" && ! r.presets.isModified(), "preset non modifié rechargé avec une étoile");

            // Ancien projet (sans nom de preset) : le point de départ est reconnu
            Proc old;
            old.applyPreset (2);
            auto tree = old.apvts.copyState();
            tree.removeProperty ("presetName", nullptr);
            tree.removeProperty ("presetFile", nullptr);
            tree.removeProperty ("presetFactory", nullptr);
            juce::MemoryBlock oldState;
            old.copyXmlToBinary (*tree.createXml(), oldState);
            Proc s;
            s.setStateInformation (oldState.getData(), (int) oldState.getSize());
            CHECK (! s.presets.getCurrent().user && s.presets.getCurrent().factoryIndex == 2, "ancien projet : point de départ non reconnu");
        }

        // Fichiers abîmés : refusés, réglages inchangés
        {
            const auto bad = dir.getChildFile ("Broken" + ext);
            bad.replaceWithText ("<nope");
            const auto other = dir.getChildFile ("Other" + ext);
            other.replaceWithText ("<SomethingElse><PARAM id=\"level\" value=\"-20\"/></SomethingElse>");
            setParam (p, P_LEVEL_ID, 1.0f);
            CHECK (! p.presets.load (bad) && ! p.presets.load (other) && ! p.presets.load (dir.getChildFile ("missing" + ext)), "fichier invalide accepté");
            CHECK (std::abs (p.apvts.getRawParameterValue (P_LEVEL_ID)->load() - 1.0f) < 1e-3f, "un fichier invalide a changé les réglages");

            // Paramètre absent ou valeur folle : défaut / borné, sans plantage
            const auto partial = dir.getChildFile ("Partial" + ext);
            partial.replaceWithText ("<" + juce::String (PLUGIN_NAME) + "Preset version=\"1\"><PARAM id=\"level\" value=\"999\"/><PARAM id=\"" + juce::String (P_A_ID) + "\" value=\"nan\"/></" + juce::String (PLUGIN_NAME) + "Preset>");
            CHECK (p.presets.load (partial), "preset partiel refusé");
            CHECK (std::abs (p.apvts.getRawParameterValue (P_LEVEL_ID)->load() - 6.0f) < 1e-3f, "valeur hors plage non bornée");
            bad.deleteFile();
            other.deleteFile();
            partial.deleteFile();
        }

        // Supprimer : le fichier part, le preset en cours est oublié
        {
            const auto random = dir.getChildFile ("Random" + ext);
            p.presets.load (random);
            CHECK (p.presets.remove (random) && ! random.existsAsFile(), "suppression impossible");
            CHECK (! p.presets.getCurrent().user, "preset supprimé encore en cours");
            CHECK (! p.presets.remove (random), "suppression d'un fichier absent acceptée");
        }

        // Interface : barre des presets
        {
            p.setRateAndBufferSizeDetails (sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            p.presets.loadFactory (0);
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
            auto& facade = dynamic_cast<EDITOR_CLASS*> (editor.get())->getFacade();
            auto* next = dynamic_cast<juce::TextButton*> (facade.findChildWithID ("presetNext"));
            auto* name = dynamic_cast<juce::TextButton*> (facade.findChildWithID ("presetName"));
            auto* save = dynamic_cast<juce::TextButton*> (facade.findChildWithID ("presetSave"));
            CHECK (next != nullptr && name != nullptr && save != nullptr && facade.findChildWithID ("presetPrev") != nullptr, "barre des presets incomplète");

            if (next != nullptr && name != nullptr)
            {
                CHECK (name->getButtonText() == juce::String (Proc::getPresets()[0].name), "nom affiché : %s", name->getButtonText().toRawUTF8());
                next->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
                CHECK (name->getButtonText() == juce::String (Proc::getPresets()[1].name), "› dans l'interface : %s", name->getButtonText().toRawUTF8());
                setParam (p, P_A_ID, P_A_OTHER);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
                CHECK (name->getButtonText().endsWith (" *"), "étoile absente dans l'interface : %s", name->getButtonText().toRawUTF8());
                CHECK (name->getRight() < 800 && name->getWidth() >= 150, "barre mal placée (%d, %d)", name->getX(), name->getWidth());
            }

            if (save != nullptr)
            {
                save->onClick();
                juce::Component* panel = nullptr;
                for (auto* c : facade.getChildren())
                    if (dynamic_cast<SavePresetPanel*> (c) != nullptr)
                        panel = c;
                CHECK (panel != nullptr && panel->isVisible(), "panneau SAVE non affiché");
                if (panel != nullptr)
                {
                    auto* field = dynamic_cast<juce::TextEditor*> (panel->getChildren()[0]);
                    CHECK (field != nullptr, "champ du nom absent");
                    if (field != nullptr)
                    {
                        field->setText ("From UI", true);
                        field->onReturnKey();
                        CHECK (! panel->isVisible() && dir.getChildFile ("From UI" + ext).existsAsFile(), "SAVE depuis l'interface");
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
                        CHECK (name == nullptr || name->getButtonText() == "From UI", "nom non mis à jour après SAVE");
                    }
                }
            }

            p.editorBeingDeleted (editor.get());
            editor.reset();
        }

        dir.deleteRecursively();
    }

    std::printf ("Interface\n");
    {
        HyperHatsAudioProcessor p;
        p.setRateAndBufferSizeDetails (sampleRate, blockSize);
        p.prepareToPlay (sampleRate, blockSize);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
        CHECK (editor != nullptr, "pas d'interface");
        CHECK (editor->getWidth() == HatsFacade::width && editor->getHeight() == HatsFacade::height, "taille initiale %dx%d", editor->getWidth(), editor->getHeight());

        auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        CHECK (image.isValid(), "rendu de l'interface impossible");

        editor->setSize (600, 432);
        CHECK (editor->getWidth() == 600, "redimensionnement refusé");
        CHECK ((int) p.apvts.state.getProperty ("editorWidth") == 600, "taille non mémorisée");

        // Les boutons agissent sur les paramètres
        auto* ed = dynamic_cast<HyperHatsAudioProcessorEditor*> (editor.get());
        int clicked = 0;
        for (auto* child : ed->getFacade().getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child))
                if (button->getButtonText() == "RING" || button->getButtonText() == "SPARKLE" || button->getButtonText() == "OPEN")
                {
                    button->triggerClick();
                    ++clicked;
                }

        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        CHECK (clicked == 3, "boutons introuvables");
        CHECK (p.isOpenSelected(), "le bouton OPEN ne choisit pas l'open");
        CHECK (p.getMatchingPreset() == 2 || juce::roundToInt (p.apvts.getRawParameterValue (HyperHatsAudioProcessor::alloyId)->load()) == 2,
               "les boutons n'ont rien changé");

        p.editorBeingDeleted (editor.get());
        editor.reset();
    }

    std::printf ("\n%d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
