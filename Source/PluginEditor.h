#pragma once

#include "PluginProcessor.h"

/**
    Look noir et blanc : fond presque noir, traits fins gris, texte blanc.
    Polices embarquées : Michroma (titres) et IBM Plex Mono (étiquettes, valeurs), licence OFL.
*/
class HatsLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HatsLookAndFeel();

    static juce::Colour ground()      { return juce::Colour (0xff050505); }
    static juce::Colour ink()         { return juce::Colour (0xffefefef); }
    static juce::Colour white()       { return juce::Colour (0xfff2f2f2); }
    static juce::Colour label()       { return juce::Colour (0xff8c8c8c); }
    static juce::Colour dim()         { return juce::Colour (0xff6f6f6f); }
    static juce::Colour faint()       { return juce::Colour (0xff5f5f5f); }
    static juce::Colour line()        { return juce::Colour (0xff1f1f1f); }
    static juce::Colour outline()     { return juce::Colour (0xff4a4a4a); }

    /** Titres (Michroma). tracking = espacement des lettres en fraction de la hauteur. */
    juce::Font title (float height, float tracking) const;
    /** Étiquettes et valeurs (IBM Plex Mono). */
    juce::Font mono (float height, float tracking, bool medium = false) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    // Menu des presets
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    // Champ du nom
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

private:
    juce::Typeface::Ptr michroma, plexRegular, plexMedium;
};

//==============================================================================
/** L'afficheur : anneaux en perspective, crête spectrale, particules ; il s'allume à chaque frappe. */
class HatsDisplay final : public juce::Component
{
public:
    struct Look
    {
        int alloy = 1;
        float source = 0.5f, character = 0.35f, color = 0.75f, snap = 0.75f;
        float closed = 0.1f, openDecay = 0.4f, width = 0.35f, crush = 0.1f;
        bool open = false;

        bool operator!= (const Look& o) const
        {
            auto differs = [] (float x, float y) { return std::abs (x - y) > 1.0e-6f; };
            return alloy != o.alloy || open != o.open
                || differs (source, o.source) || differs (character, o.character) || differs (color, o.color)
                || differs (snap, o.snap) || differs (closed, o.closed) || differs (openDecay, o.openDecay)
                || differs (width, o.width) || differs (crush, o.crush);
        }
    };

    void setLook (const Look& l)                  { if (l != look) { look = l; repaint(); } }
    void setGlow (float g)                        { if (std::abs (g - glow) > 0.002f) { glow = g; repaint(); } }

    void paint (juce::Graphics&) override;

private:
    Look look;
    float glow = 0.0f;
};

//==============================================================================
/** Fenêtre « SAVE PRESET » dessinée dans la façade (pas de fenêtre séparée dans le DAW). */
class SavePresetPanel final : public juce::Component
{
public:
    SavePresetPanel (HatsLookAndFeel&, PresetLibrary&);

    /** Ouvre le panneau, nom proposé = preset en cours. */
    void open (const juce::String& suggestedName);
    void close();

    std::function<void()> onSaved;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override   { close(); }

private:
    void updateSaveButton();
    void save();

    HatsLookAndFeel& lf;
    PresetLibrary& library;
    juce::TextEditor nameField;
    juce::TextButton cancelButton { "CANCEL" }, saveButton { "SAVE" };
    juce::String message;
    juce::Rectangle<int> box;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SavePresetPanel)
};

//==============================================================================
/** La façade à taille fixe (1000 x 720) ; l'éditeur la met à l'échelle. */
class HatsFacade final : public juce::Component,
                         private juce::Timer
{
public:
    static constexpr int width = 1000, height = 720;

    explicit HatsFacade (HyperHatsAudioProcessor&);
    ~HatsFacade() override;

    void paint (juce::Graphics&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider slider;
        const char* id = nullptr;
        juce::String name;
    };

    void setupKnob (Knob&, const char* id, const char* name, juce::Rectangle<int> bounds);
    void setupLinear (juce::Slider&, const char* id, juce::Rectangle<int> bounds);
    void setupSegment (juce::TextButton&, juce::Rectangle<int> bounds);
    void timerCallback() override;
    void refreshFromParameters();
    void showPresetMenu();
    void layoutPresetBar();

    juce::String valueText (const char* id) const;
    float normalised (const char* id) const;

    HyperHatsAudioProcessor& audioProcessor;
    HatsLookAndFeel lookAndFeel;

    HatsDisplay display;
    juce::Slider sourceSlider, characterSlider;
    Knob tune, color, snap, closed, open, shape, widthKnob, variation, crush, level;
    juce::TextButton alloyButtons[3];
    juce::TextButton presetButtons[4];
    juce::TextButton typeClosed { "CLOSED" }, typeOpen { "OPEN" }, hit { "HIT" };

    // Barre des presets (en-tête)
    juce::TextButton presetPrev, presetName, presetNext, presetSave { "SAVE" };
    SavePresetPanel savePanel;
    juce::String shownPresetName;

    std::vector<std::unique_ptr<APVTS::SliderAttachment>> attachments;

    std::vector<float> lastValues;
    uint32_t lastHitCount = 0;
    float glow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HatsFacade)
};

//==============================================================================
class HyperHatsAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit HyperHatsAudioProcessorEditor (HyperHatsAudioProcessor&);
    ~HyperHatsAudioProcessorEditor() override;

    void resized() override;

    HatsFacade& getFacade() noexcept              { return facade; }

private:
    HyperHatsAudioProcessor& audioProcessor;
    HatsFacade facade;
    bool sized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HyperHatsAudioProcessorEditor)
};
