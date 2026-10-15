#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    /** Générateur pseudo-aléatoire de l'afficheur (mulberry32) : même dessin à chaque image. */
    struct Mulberry
    {
        uint32_t a;

        float next() noexcept
        {
            a += 0x6D2B79F5u;
            uint32_t t = (a ^ (a >> 15)) * (1u | a);
            t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
            return (float) ((t ^ (t >> 14)) / 4294967296.0);
        }
    };

    void drawLabel (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                    juce::Rectangle<float> area, juce::Justification just)
    {
        g.setColour (colour);
        g.setFont (font);
        g.drawText (text, area, just, false);
    }

    float textWidth (const juce::Font& font, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (font, text);
    }

    // Disposition de la façade (pixels de la maquette 1000 x 720)
    constexpr int headerHeight = 64;
    constexpr int footerTop = 634;
    constexpr int leftWidth = 520;
    constexpr int rightX = 552, rightWidth = 416;
    constexpr int knobRowY = 332, knobSize = 44, knobColumn = 66, knobGap = 4;
    constexpr int footerKnobY = 655, footerKnobStep = 170;
    const juce::Rectangle<float> envelopeArea { (float) rightX, 440.0f, (float) rightWidth, 60.0f };
}

//==============================================================================
HatsLookAndFeel::HatsLookAndFeel()
{
    michroma    = juce::Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize);
    plexRegular = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
    plexMedium  = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);

    setColour (juce::ResizableWindow::backgroundColourId, ground());
    setColour (juce::TextButton::buttonColourId, ground());
    setColour (juce::TextButton::textColourOffId, ink());
}

juce::Font HatsLookAndFeel::title (float height, float tracking) const
{
    return juce::Font (juce::FontOptions (michroma).withPointHeight (height)).withExtraKerningFactor (tracking);
}

juce::Font HatsLookAndFeel::mono (float height, float tracking, bool medium) const
{
    return juce::Font (juce::FontOptions (medium ? plexMedium : plexRegular).withPointHeight (height)).withExtraKerningFactor (tracking);
}

void HatsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto box = juce::Rectangle<float> (size, size).withCentre (centre);
    const bool active = slider.isMouseOverOrDragging();

    // Couronne pointillée
    juce::Path ring;
    ring.addEllipse (box.reduced (0.5f));
    juce::Path dashed;
    const float dashes[] = { 2.5f, 2.5f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, ring, dashes, 2);
    g.setColour (active ? juce::Colour (0xff6a6a6a) : juce::Colour (0xff3d3d3d));
    g.fillPath (dashed);

    // Disque
    const auto disc = box.reduced (size * 6.0f / 44.0f);
    g.setColour (juce::Colour (0xff121212));
    g.fillEllipse (disc);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawEllipse (disc.reduced (0.5f), 1.0f);

    // Aiguille
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float rOuter = size * 0.5f - size * 3.0f / 44.0f;
    const float rInner = rOuter - size * 14.0f / 44.0f;
    const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
    g.setColour (white());
    g.drawLine ({ centre + dir * rInner, centre + dir * rOuter }, 1.2f);
}

void HatsLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                        float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (outline());
    g.fillRect ((float) x, cy - 0.5f, (float) w, 1.0f);

    const bool active = slider.isMouseOverOrDragging();
    g.setColour (active ? juce::Colours::white : white());
    g.fillRect (juce::Rectangle<float> (11.0f, 21.0f).withCentre ({ sliderPos, cy }));
}

void HatsLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool highlighted, bool down)
{
    const auto area = button.getLocalBounds().toFloat();
    const bool hit = button.getComponentID() == "hit";
    const bool on = button.getToggleState();

    if (hit)
    {
        g.setColour (down ? ground() : white());
        g.fillRect (area);
        g.setColour (white());
        g.drawRect (area, 1.0f);
        return;
    }

    if (on)
    {
        g.setColour (white());
        g.fillRect (area);
    }

    g.setColour (on || highlighted ? white() : outline());
    g.drawRect (area, 1.0f);
}

void HatsLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down)
{
    const auto id = button.getComponentID();
    const auto area = button.getLocalBounds().toFloat();

    // Flèches ‹ › de la barre des presets
    if (id == "presetPrev" || id == "presetNext")
    {
        const float dir = id == "presetPrev" ? -1.0f : 1.0f;
        const auto c = area.getCentre();
        juce::Path chevron;
        chevron.startNewSubPath (c.x - dir * 2.5f, c.y - 5.0f);
        chevron.lineTo (c.x + dir * 2.5f, c.y);
        chevron.lineTo (c.x - dir * 2.5f, c.y + 5.0f);
        g.setColour (highlighted || down ? juce::Colours::white : ink());
        g.strokePath (chevron, juce::PathStrokeType (1.2f));
        return;
    }

    // Nom du preset : texte à gauche, petit triangle à droite
    if (id == "presetName")
    {
        auto textArea = area.reduced (12.0f, 0.0f).withTrimmedRight (14.0f);
        auto text = button.getButtonText();
        const auto font = mono (11.0f, 0.12f);
        g.setFont (font);

        // L'étoile « modifié » reste visible même quand le nom est coupé
        if (text.endsWith (" *"))
        {
            text = text.dropLastCharacters (2);
            g.setColour (white());
            g.drawText ("*", textArea.removeFromRight (12.0f), juce::Justification::centredRight, false);
            textArea.removeFromRight (4.0f);
        }

        g.setColour (ink());
        g.drawText (text, textArea, juce::Justification::centredLeft, true);

        juce::Path caret;
        const float x = area.getRight() - 16.0f, y = area.getCentreY() - 1.5f;
        caret.addTriangle (x, y, x + 7.0f, y, x + 3.5f, y + 4.0f);
        g.setColour (highlighted ? juce::Colours::white : label());
        g.fillPath (caret);
        return;
    }

    const bool hit = id == "hit";
    const bool dark = hit ? ! down : button.getToggleState();

    g.setColour (dark ? ground() : ink());
    g.setFont (mono (10.0f, 0.2f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

juce::Font HatsLookAndFeel::getPopupMenuFont()
{
    return mono (12.0f, 0.06f);
}

void HatsLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff0b0b0b));
    g.setColour (outline());
    g.drawRect (0, 0, w, h, 1);
}

void HatsLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                         bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                         const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (line().brighter (0.3f));
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (1, 0);
    if (isHighlighted && isActive)
    {
        g.setColour (white());
        g.fillRect (r);
    }

    const auto textColour = ! isActive ? juce::Colour (0xff4a4a4a) : isHighlighted ? ground() : ink();

    if (isTicked)
    {
        g.setColour (textColour);
        g.fillRect (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ (float) r.getX() + 13.0f, (float) r.getCentreY() }));
    }

    g.setColour (textColour);
    g.setFont (getPopupMenuFont());
    g.drawText (text, r.withTrimmedLeft (26).withTrimmedRight (12), juce::Justification::centredLeft, true);
}

void HatsLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setColour (dim());
    g.setFont (mono (9.0f, 0.2f));
    g.drawText (sectionName, area.withTrimmedLeft (12).withTrimmedTop (4), juce::Justification::centredLeft, false);
}

void HatsLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 60;
        idealHeight = 9;
        return;
    }

    idealHeight = 24;
    idealWidth = juce::jmax (180, (int) std::ceil (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text)) + 46);
}

void HatsLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (juce::Colour (0xff0d0d0d));
    g.fillRect (0, 0, w, h);
}

void HatsLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? white() : outline());
    g.drawRect (0, 0, w, h, 1);
}

//==============================================================================
SavePresetPanel::SavePresetPanel (HatsLookAndFeel& l, PresetLibrary& lib)
    : lf (l), library (lib)
{
    setInterceptsMouseClicks (true, true);

    nameField.setFont (lf.mono (14.0f, 0.06f));
    nameField.setColour (juce::TextEditor::textColourId, HatsLookAndFeel::ink());
    nameField.setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff4a4a4a));
    nameField.setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    nameField.setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
    nameField.setIndents (12, 0);
    nameField.setJustification (juce::Justification::centredLeft);
    nameField.setInputRestrictions (48);
    nameField.setSelectAllWhenFocused (true);
    nameField.setTitle ("Preset name");
    nameField.onTextChange = [this] { updateSaveButton(); };
    nameField.onReturnKey = [this] { save(); };
    nameField.onEscapeKey = [this] { close(); };
    addAndMakeVisible (nameField);

    for (auto* b : { &cancelButton, &saveButton })
    {
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (*b);
    }

    saveButton.setComponentID ("hit");   // bouton plein, comme HIT
    cancelButton.onClick = [this] { close(); };
    saveButton.onClick = [this] { save(); };

    setVisible (false);
}

void SavePresetPanel::open (const juce::String& suggestedName)
{
    message = {};
    nameField.setText (suggestedName, juce::dontSendNotification);
    updateSaveButton();
    setVisible (true);
    toFront (false);

    if (nameField.isShowing())
        nameField.grabKeyboardFocus();
    nameField.selectAll();
}

void SavePresetPanel::close()
{
    setVisible (false);
    giveAwayKeyboardFocus();   // rend le clavier au DAW (barre d'espace, etc.)
}

void SavePresetPanel::updateSaveButton()
{
    const auto name = PresetLibrary::cleanName (nameField.getText());
    saveButton.setEnabled (name.isNotEmpty());
    saveButton.setButtonText (library.exists (name) ? "REPLACE" : "SAVE");
    message = {};
    repaint();
}

void SavePresetPanel::save()
{
    const auto name = PresetLibrary::cleanName (nameField.getText());
    if (name.isEmpty())
        return;

    if (! library.save (name))
    {
        message = "COULD NOT WRITE THE PRESET FILE";
        repaint();
        return;
    }

    close();
    if (onSaved)
        onSaved();
}

void SavePresetPanel::resized()
{
    box = juce::Rectangle<int> (440, 224).withCentre (getLocalBounds().getCentre());
    nameField.setBounds (box.getX() + 28, box.getY() + 66, box.getWidth() - 56, 40);
    const int buttonsY = box.getBottom() - 28 - 40;
    saveButton.setBounds (box.getRight() - 28 - 110, buttonsY, 110, 40);
    cancelButton.setBounds (saveButton.getX() - 8 - 110, buttonsY, 110, 40);
}

void SavePresetPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.78f));

    g.setColour (HatsLookAndFeel::ground());
    g.fillRect (box);
    g.setColour (HatsLookAndFeel::outline());
    g.drawRect (box, 1);

    drawLabel (g, "SAVE PRESET", lf.title (12.0f, 0.34f), juce::Colour (0xfff4f4f4),
               box.toFloat().reduced (28.0f, 0.0f).withY ((float) box.getY() + 26.0f).withHeight (18.0f), juce::Justification::centredLeft);

    const auto hint = juce::String (juce::CharPointer_UTF8 ("ENTER = SAVE \xc2\xb7 ESC = CANCEL"));
    drawLabel (g, message.isNotEmpty() ? message : hint, lf.mono (9.0f, 0.14f),
               message.isNotEmpty() ? juce::Colour (0xffd0d0d0) : HatsLookAndFeel::faint(),
               box.toFloat().reduced (28.0f, 0.0f).withY ((float) box.getY() + 114.0f).withHeight (14.0f), juce::Justification::centredLeft);
}

//==============================================================================
void HatsDisplay::paint (juce::Graphics& g)
{
    // Même dessin que la maquette : plus la frappe est longue, plus l'anneau est large ;
    // le métal fait un peigne, le bruit une crête irrégulière, CHARACTER ajoute des particules.
    const float c = look.character, m = look.source, col = look.color, snap = look.snap;
    const float crush = look.crush, wd = look.width;
    const float dn = look.open ? 0.45f + 0.55f * look.openDecay : 0.12f + 0.3f * look.closed;

    const float cx = 260.0f, cy = 262.0f, k = 0.3f;
    const float R = 180.0f * (0.62f + 0.38f * dn);
    const float Rx = R * (0.84f + 0.3f * wd);
    const float envWidth = 0.15f + 0.45f * dn;
    const float pulse = 1.0f + 0.15f * glow;

    auto env = [&] (float t) { return (5.0f + (50.0f + 85.0f * dn) * std::exp (-std::pow (t / envWidth, 2.0f))) * (0.7f + 0.6f * col); };
    auto pattern = [&] (int i, float t)
    {
        if (look.alloy == hyperhats::alloySquare) return (i % 14) < 2 ? 1.3f : 0.55f;
        if (look.alloy == hyperhats::alloyRing)   return (i % 7 == 0 || i % 7 == 2) ? 1.35f : 0.5f;
        return 0.6f + 0.5f * std::abs (std::cos (t * juce::MathConstants<float>::pi * 7.0f));
    };

    const float q = 1.0f + crush * 14.0f;
    Mulberry rand { 23 };
    const int N = 84;

    struct Bar { float x, y, w, h, o; };
    std::vector<Bar> bars, reflections;
    bars.reserve (N);
    reflections.reserve (N);

    for (int i = 0; i < N; ++i)
    {
        const float t = (float) i / (float) (N - 1) * 2.0f - 1.0f;
        const float x = cx + t * Rx * 0.96f;
        const float s = std::sqrt (juce::jmax (0.0f, 1.0f - t * t));
        const float back = cy - R * k * s, front = cy + R * k * s;
        const float r1 = rand.next(), r2 = rand.next();
        const float e = env (t);
        const float metal = e * pattern (i, t);
        const float noise = e * (0.3f + 1.2f * r1 * r1);
        const float base = (1.0f - m) * metal + m * noise;
        float h = base * ((1.0f - c) + c * (0.35f + 1.3f * r2));
        if (crush > 0.02f) h = std::round (h / q) * q;
        h = juce::jmax (1.5f, h * pulse);
        const float w = (i % 4 == 0) ? 2.0f : 1.0f;
        bars.push_back ({ x, back - h, w, h, juce::jmin (1.0f, 0.32f + 0.55f * (e / 150.0f) + 0.12f * r2) });
        reflections.push_back ({ x, front + 4.0f, w, h * 0.26f, 0.09f });
    }

    // Faisceau central
    const float peak = env (0.0f) * (0.8f + 0.7f * snap) * pulse;
    const float beamTop = cy - R * k - peak * 1.5f - 16.0f;
    const float beamAlpha = 0.6f + 0.4f * glow;
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (beamAlpha), cx, cy - R * k,
                                             juce::Colours::white.withAlpha (0.0f), cx, beamTop, false));
    g.fillRect (cx, beamTop, 1.0f, cy - R * k - beamTop);

    for (auto& b : bars)
    {
        g.setColour (HatsLookAndFeel::white().withAlpha (b.o));
        g.fillRect (b.x, b.y, b.w, b.h);
    }

    // Anneaux
    for (int j = 0; j < 9; ++j)
    {
        const float sc = (0.14f + 0.86f * (float) j / 8.0f) * (1.0f + (rand.next() - 0.5f) * 0.1f * c);
        const float rx = Rx * sc, ry = R * sc * k;
        const bool main = j == 6;
        const float alpha = main ? 0.92f : (0.14f + 0.5f * ((float) j / 8.0f)) * (1.0f - 0.35f * c * rand.next());

        juce::Path ellipse;
        ellipse.addEllipse (cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
        const float thickness = main ? 2.0f : 1.0f;
        g.setColour (HatsLookAndFeel::white().withAlpha (alpha));

        const bool dotted = crush > 0.5f && ! main;
        const bool dashedRing = ! dotted && c > 0.55f && ! main && j % 2 == 1;

        if (dotted || dashedRing)
        {
            juce::Path stroke;
            const float dots[] = { 1.0f, 2.0f };
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (thickness).createDashedStroke (stroke, ellipse, dotted ? dots : dashes, 2);
            g.fillPath (stroke);
        }
        else
        {
            g.strokePath (ellipse, juce::PathStrokeType (thickness));
        }
    }

    // Particules
    const int numDots = juce::roundToInt (juce::jmin (1.0f, c * 0.6f + m * 0.6f) * 160.0f);
    for (int d = 0; d < numDots; ++d)
    {
        const float a = rand.next() * juce::MathConstants<float>::twoPi;
        const float rad = 0.2f + rand.next() * 0.94f;
        const float size = rand.next() > 0.8f ? 2.5f : 1.5f;
        g.setColour (HatsLookAndFeel::white().withAlpha (0.15f + rand.next() * 0.6f));
        g.fillEllipse (cx + Rx * rad * std::cos (a), cy + R * rad * k * std::sin (a), size, size);
    }

    for (auto& b : reflections)
    {
        g.setColour (HatsLookAndFeel::white().withAlpha (b.o));
        g.fillRect (b.x, b.y, b.w, b.h);
    }

    // Lueur du centre (grandit à chaque frappe)
    const float gw = 36.0f + 32.0f * glow, gh = 12.0f + 10.0f * glow;
    const float halo = 22.0f + 26.0f * glow;
    juce::ColourGradient haloFill (juce::Colours::white.withAlpha (0.55f), cx, cy,
                                   juce::Colours::white.withAlpha (0.0f), cx + gw * 0.5f + halo, cy, true);
    g.setGradientFill (haloFill);
    g.fillEllipse (cx - gw * 0.5f - halo, cy - gh * 0.5f - halo * 0.6f, gw + 2.0f * halo, gh + 1.2f * halo);
    g.setColour (juce::Colours::white);
    g.fillEllipse (cx - gw * 0.5f, cy - gh * 0.5f, gw, gh);
}

//==============================================================================
HatsFacade::HatsFacade (HyperHatsAudioProcessor& p)
    : audioProcessor (p), savePanel (lookAndFeel, p.presets)
{
    setLookAndFeel (&lookAndFeel);
    setSize (width, height);
    setOpaque (true);

    display.setInterceptsMouseClicks (false, false);
    display.setBounds (0, 126, leftWidth, 400);
    addAndMakeVisible (display);

    // ALLOY
    const char* alloyNames[3] = { "SQUARE", "FM", "RING" };
    const float segWidth = (rightWidth - 12) / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        alloyButtons[i].setButtonText (alloyNames[i]);
        setupSegment (alloyButtons[i], juce::Rectangle<float> (rightX + i * (segWidth + 6.0f), 100.0f, segWidth, 44.0f).toNearestInt());
        alloyButtons[i].onClick = [this, i]
        {
            if (auto* param = audioProcessor.apvts.getParameter (HyperHatsAudioProcessor::alloyId))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
                param->endChangeGesture();
            }
        };
    }

    setupLinear (sourceSlider, HyperHatsAudioProcessor::sourceId, { rightX - 6, 174, rightWidth + 12, 28 });
    setupLinear (characterSlider, HyperHatsAudioProcessor::characterId, { rightX - 6, 244, rightWidth + 12, 28 });

    Knob* row[6] = { &tune, &color, &snap, &closed, &open, &shape };
    const char* rowIds[6] = { HyperHatsAudioProcessor::tuneId, HyperHatsAudioProcessor::colorId, HyperHatsAudioProcessor::snapId,
                              HyperHatsAudioProcessor::closedId, HyperHatsAudioProcessor::openId, HyperHatsAudioProcessor::shapeId };
    const char* rowNames[6] = { "TUNE", "COLOR", "SNAP", "CLOSED", "OPEN", "SHAPE" };

    for (int i = 0; i < 6; ++i)
    {
        const int x = rightX + i * (knobColumn + knobGap) + (knobColumn - knobSize) / 2;
        setupKnob (*row[i], rowIds[i], rowNames[i], { x, knobRowY, knobSize, knobSize });
    }

    Knob* footer[4] = { &widthKnob, &variation, &crush, &level };
    const char* footerIds[4] = { HyperHatsAudioProcessor::widthId, HyperHatsAudioProcessor::variationId,
                                 HyperHatsAudioProcessor::crushId, HyperHatsAudioProcessor::levelId };
    const char* footerNames[4] = { "WIDTH", "VARIATION", "CRUSH", "LEVEL" };

    for (int i = 0; i < 4; ++i)
        setupKnob (*footer[i], footerIds[i], footerNames[i], { 34 + i * footerKnobStep, footerKnobY, knobSize, knobSize });

    // Points de départ
    const float presetWidth = (rightWidth - 18) / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        presetButtons[i].setButtonText (HyperHatsAudioProcessor::getPresets()[(size_t) i].name);
        setupSegment (presetButtons[i], juce::Rectangle<float> (rightX + i * (presetWidth + 6.0f), 568.0f, presetWidth, 44.0f).toNearestInt());
        presetButtons[i].onClick = [this, i] { audioProcessor.presets.loadFactory (i); };
    }

    // TYPE (CLOSED / OPEN) : le son que joue chaque note. Choisir un type le fait aussi entendre.
    setupSegment (typeClosed, { 712, footerKnobY, 84, 44 });
    setupSegment (typeOpen, { 802, footerKnobY, 84, 44 });

    auto selectType = [this] (bool openType)
    {
        if (auto* param = audioProcessor.apvts.getParameter (HyperHatsAudioProcessor::typeId))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (openType ? 1.0f : 0.0f);
            param->endChangeGesture();
        }

        audioProcessor.requestPreview();
    };

    typeClosed.onClick = [selectType] { selectType (false); };
    typeOpen.onClick = [selectType] { selectType (true); };

    // Écoute
    hit.setComponentID ("hit");
    hit.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    hit.setBounds (892, footerKnobY, 76, 44);
    hit.onClick = [this] { audioProcessor.requestPreview(); };
    addAndMakeVisible (hit);

    // Presets : ‹ NOM ▾ › SAVE
    presetPrev.setComponentID ("presetPrev");
    presetName.setComponentID ("presetName");
    presetNext.setComponentID ("presetNext");
    presetSave.setComponentID ("presetSave");
    presetPrev.setTitle ("Previous preset");
    presetNext.setTitle ("Next preset");
    presetName.setTitle ("Presets");

    for (auto* b : { &presetPrev, &presetName, &presetNext, &presetSave })
        setupSegment (*b, {});

    layoutPresetBar();

    presetPrev.onClick = [this] { audioProcessor.presets.step (-1); };
    presetNext.onClick = [this] { audioProcessor.presets.step (+1); };
    presetName.onClick = [this] { showPresetMenu(); };
    presetSave.onClick = [this]
    {
        const auto& current = audioProcessor.presets.getCurrent();
        savePanel.open (current.user ? current.name : juce::String());
    };

    savePanel.setBounds (getLocalBounds());
    savePanel.onSaved = [this] { refreshFromParameters(); repaint(); };
    addChildComponent (savePanel);

    lastHitCount = audioProcessor.getHitCount();
    refreshFromParameters();
    startTimerHz (30);
}

HatsFacade::~HatsFacade()
{
    stopTimer();
    attachments.clear();
    setLookAndFeel (nullptr);
}

void HatsFacade::layoutPresetBar()
{
    // Entre « HI-HAT SYNTHESIS » et « BY PHRAGMA »
    const auto wordmark = lookAndFeel.title (20.0f, 0.36f);
    const auto label10 = lookAndFeel.mono (10.0f, 0.14f);
    const float left = 34.0f + textWidth (wordmark, "HYPERHATS") + 22.0f + textWidth (label10, "HI-HAT SYNTHESIS") + 20.0f;
    const auto value11 = lookAndFeel.mono (11.0f, 0.04f);
    const float byRight = 966.0f - textWidth (value11, "v" + juce::String (HYPERHATS_VERSION)) - 16.0f;
    const float right = byRight - textWidth (label10, "BY PHRAGMA") - 20.0f;

    const int arrow = 28, save = 56, gap = 4, h = 30, y = 17;
    const int nameWidth = juce::jlimit (110, 230, (int) (right - left) - 2 * arrow - save - 3 * gap - 6);
    const int total = 2 * arrow + nameWidth + save + 3 * gap + 6;
    int x = juce::roundToInt ((left + right - (float) total) * 0.5f);

    presetPrev.setBounds (x, y, arrow, h);           x += arrow + gap;
    presetName.setBounds (x, y, nameWidth, h);       x += nameWidth + gap;
    presetNext.setBounds (x, y, arrow, h);           x += arrow + gap + 6;
    presetSave.setBounds (x, y, save, h);
}

void HatsFacade::showPresetMenu()
{
    auto& library = audioProcessor.presets;
    const auto list = library.getAllEntries();
    const int currentIndex = library.indexOfCurrent (list);
    const auto current = library.getCurrent();

    enum { saveAsId = 100001, deleteId, folderId, emptyId };

    juce::PopupMenu menu;
    menu.addSectionHeader ("FACTORY");
    bool userSection = false;

    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].user && ! userSection)
        {
            menu.addSeparator();
            menu.addSectionHeader ("USER");
            userSection = true;
        }

        menu.addItem ((int) i + 1, list[i].name, true, (int) i == currentIndex);
    }

    if (! userSection)
    {
        menu.addSeparator();
        menu.addSectionHeader ("USER");
        menu.addItem (emptyId, "NO SAVED PRESET YET", false);
    }

    menu.addSeparator();
    menu.addItem (saveAsId, juce::String (juce::CharPointer_UTF8 ("SAVE AS\xe2\x80\xa6")));
    if (current.user && current.file.existsAsFile())
        menu.addItem (deleteId, "DELETE " + current.name.quoted());
    menu.addItem (folderId, "SHOW PRESETS FOLDER");

    juce::Component::SafePointer<HatsFacade> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName)
                                                  .withMinimumWidth (presetName.getWidth())
                                                  .withStandardItemHeight (24),
                        [safe, list] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            auto& lib = safe->audioProcessor.presets;

                            if (result == saveAsId)
                            {
                                const auto& cur = lib.getCurrent();
                                safe->savePanel.open (cur.user ? cur.name : juce::String());
                            }
                            else if (result == deleteId)
                            {
                                lib.remove (lib.getCurrent().file);
                            }
                            else if (result == folderId)
                            {
                                const auto dir = lib.folder();
                                dir.createDirectory();
                                dir.startAsProcess();
                            }
                            else if (juce::isPositiveAndBelow (result - 1, (int) list.size()))
                            {
                                lib.loadEntry (list[(size_t) (result - 1)]);
                            }

                            safe->refreshFromParameters();
                            safe->repaint();
                        });
}

void HatsFacade::setupKnob (Knob& knob, const char* id, const char* name, juce::Rectangle<int> bounds)
{
    knob.id = id;
    knob.name = name;

    auto& s = knob.slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setMouseDragSensitivity (180);
    s.setTitle (name);
    s.setBounds (bounds);
    addAndMakeVisible (s);
    attachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, id, s));

    if (auto* param = audioProcessor.apvts.getParameter (id))
        s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void HatsFacade::setupLinear (juce::Slider& s, const char* id, juce::Rectangle<int> bounds)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setBounds (bounds);
    addAndMakeVisible (s);
    attachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, id, s));

    if (auto* param = audioProcessor.apvts.getParameter (id))
    {
        s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        s.setTitle (param->getName (64));
    }
}

void HatsFacade::setupSegment (juce::TextButton& b, juce::Rectangle<int> bounds)
{
    b.setClickingTogglesState (false);
    b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    b.setBounds (bounds);
    addAndMakeVisible (b);
}

juce::String HatsFacade::valueText (const char* id) const
{
    if (auto* param = audioProcessor.apvts.getParameter (id))
        return param->getCurrentValueAsText();

    return {};
}

float HatsFacade::normalised (const char* id) const
{
    if (auto* param = audioProcessor.apvts.getParameter (id))
        return param->getValue();

    return 0.0f;
}

void HatsFacade::refreshFromParameters()
{
    const auto params = audioProcessor.readParams();

    for (int i = 0; i < 3; ++i)
        alloyButtons[i].setToggleState (params.alloy == i, juce::dontSendNotification);

    typeClosed.setToggleState (! audioProcessor.isOpenSelected(), juce::dontSendNotification);
    typeOpen.setToggleState (audioProcessor.isOpenSelected(), juce::dontSendNotification);

    const auto& library = audioProcessor.presets;
    shownPresetName = library.getDisplayName() + (library.isModified() ? " *" : "");
    presetName.setButtonText (shownPresetName);

    const int preset = audioProcessor.getMatchingPreset();
    for (int i = 0; i < 4; ++i)
        presetButtons[i].setToggleState (preset == i, juce::dontSendNotification);

    HatsDisplay::Look look;
    look.alloy = params.alloy;
    look.source = params.source;
    look.character = params.character;
    look.color = normalised (HyperHatsAudioProcessor::colorId);
    look.snap = params.snap;
    look.closed = normalised (HyperHatsAudioProcessor::closedId);
    look.openDecay = normalised (HyperHatsAudioProcessor::openId);
    look.width = params.width;
    look.crush = params.crush;
    look.open = audioProcessor.isOpenSelected();
    display.setLook (look);
}

void HatsFacade::timerCallback()
{
    // Frappes : la lueur s'allume puis retombe
    const auto hits = audioProcessor.getHitCount();
    bool changed = false;

    if (hits != lastHitCount)
    {
        lastHitCount = hits;
        glow = 1.0f;
    }
    else
    {
        glow *= 0.80f;
        if (glow < 0.01f) glow = 0.0f;
    }

    display.setGlow (glow);

    // Réglages
    const auto& params = audioProcessor.getParameters();
    if (lastValues.size() != (size_t) params.size())
    {
        lastValues.assign ((size_t) params.size(), -1.0f);
        changed = true;
    }

    for (int i = 0; i < params.size(); ++i)
    {
        const float v = params[i]->getValue();
        if (std::abs (v - lastValues[(size_t) i]) > 1.0e-7f)
        {
            lastValues[(size_t) i] = v;
            changed = true;
        }
    }

    // Nom du preset (change aussi quand on enregistre, sans toucher aux réglages)
    const auto& library = audioProcessor.presets;
    if (library.getDisplayName() + (library.isModified() ? " *" : "") != shownPresetName)
        changed = true;

    if (changed)
    {
        refreshFromParameters();
        repaint();
    }
}

void HatsFacade::paint (juce::Graphics& g)
{
    using LF = HatsLookAndFeel;
    const auto& lf = lookAndFeel;
    g.fillAll (LF::ground());

    const auto label10 = lf.mono (10.0f, 0.14f);
    const auto label9 = lf.mono (9.0f, 0.14f);
    const auto value11 = lf.mono (11.0f, 0.04f);
    const auto value10 = lf.mono (10.0f, 0.04f);

    // En-tête
    const auto wordmark = lf.title (20.0f, 0.36f);
    drawLabel (g, "HYPERHATS", wordmark, juce::Colour (0xfff4f4f4), { 34.0f, 18.0f, 400.0f, 28.0f }, juce::Justification::centredLeft);
    const float afterWordmark = 34.0f + textWidth (wordmark, "HYPERHATS") + 22.0f;
    drawLabel (g, "HI-HAT SYNTHESIS", label10, LF::dim(), { afterWordmark, 22.0f, 300.0f, 22.0f }, juce::Justification::centredLeft);
    const auto versionText = "v" + juce::String (HYPERHATS_VERSION);
    drawLabel (g, versionText, value11, LF::label(), { 866.0f, 22.0f, 100.0f, 22.0f }, juce::Justification::centredRight);
    const float byRight = 966.0f - textWidth (value11, versionText) - 16.0f;
    drawLabel (g, "BY PHRAGMA", label10, LF::label(), { byRight - 130.0f, 22.0f, 130.0f, 22.0f }, juce::Justification::centredRight);

    g.setColour (LF::line());
    g.fillRect (0, headerHeight - 1, width, 1);
    g.fillRect (0, footerTop, width, 1);
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRect (leftWidth, headerHeight, 1, footerTop - headerHeight);

    // Afficheur : textes
    const auto params = audioProcessor.readParams();
    const char* alloyNames[3] = { "SQUARE", "FM", "RING" };
    const int charPct = juce::roundToInt (params.character * 100.0f);
    const char* charWord = charPct < 20 ? "SYNTHETIC" : charPct < 45 ? "ELECTRONIC" : charPct < 70 ? "HYBRID" : "ACOUSTIC";

    drawLabel (g, juce::String (alloyNames[params.alloy]) + " \xc2\xb7 " + charWord, lf.title (14.0f, 0.34f), juce::Colour (0xfff4f4f4),
               { 0.0f, 82.0f, (float) leftWidth, 22.0f }, juce::Justification::centred);

    const bool openSelected = audioProcessor.isOpenSelected();
    const auto decayText = openSelected ? valueText (HyperHatsAudioProcessor::openId) : valueText (HyperHatsAudioProcessor::closedId);
    drawLabel (g, juce::String (openSelected ? "OPEN HAT" : "CLOSED HAT") + " \xc2\xb7 " + decayText.toUpperCase(),
               label10, LF::dim(), { 0.0f, 106.0f, (float) leftWidth, 16.0f }, juce::Justification::centred);

    const int metalPct = juce::roundToInt ((1.0f - params.source) * 100.0f);
    const int partials = juce::roundToInt (6.0f + params.character * 42.0f);
    drawLabel (g, "METAL " + juce::String (metalPct) + "% \xc2\xb7 NOISE " + juce::String (100 - metalPct) + "% \xc2\xb7 PARTIALS "
                   + juce::String (partials) + " \xc2\xb7 BAND " + valueText (HyperHatsAudioProcessor::colorId).toUpperCase(),
               label9, LF::faint(), { 0.0f, 532.0f, (float) leftWidth, 14.0f }, juce::Justification::centred);
    drawLabel (g, juce::String (juce::CharPointer_UTF8 ("ANY MIDI NOTE PLAYS THIS HAT \xc2\xb7 VELOCITY = ACCENT")),
               label9, juce::Colour (0xff4f4f4f), { 0.0f, 554.0f, (float) leftWidth, 14.0f }, juce::Justification::centred);

    // Colonne de droite
    const float rx = (float) rightX, rw = (float) rightWidth;
    drawLabel (g, "ALLOY", label10, LF::ink(), { rx, 80.0f, rw, 14.0f }, juce::Justification::centredLeft);

    drawLabel (g, "SOURCE", label10, LF::ink(), { rx, 156.0f, rw, 16.0f }, juce::Justification::centredLeft);
    drawLabel (g, juce::String (metalPct) + " / " + juce::String (100 - metalPct), value11, LF::ink(), { rx, 156.0f, rw, 16.0f }, juce::Justification::centredRight);
    drawLabel (g, "METAL", label10, LF::label(), { rx, 204.0f, rw, 14.0f }, juce::Justification::centredLeft);
    drawLabel (g, "NOISE", label10, LF::label(), { rx, 204.0f, rw, 14.0f }, juce::Justification::centredRight);

    drawLabel (g, "CHARACTER", label10, LF::ink(), { rx, 226.0f, rw, 16.0f }, juce::Justification::centredLeft);
    drawLabel (g, juce::String (charPct) + " \xc2\xb7 " + juce::String (charWord).toLowerCase(), value11, LF::ink(), { rx, 226.0f, rw, 16.0f }, juce::Justification::centredRight);
    drawLabel (g, "SYNTH", label10, LF::label(), { rx, 274.0f, rw, 14.0f }, juce::Justification::centredLeft);
    drawLabel (g, "ACOUSTIC", label10, LF::label(), { rx, 274.0f, rw, 14.0f }, juce::Justification::centredRight);

    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRect (rx, 300.0f, rw, 1.0f);

    const float decayX = rx + 3 * (knobColumn + knobGap) - knobGap * 0.5f;
    drawLabel (g, "TONE", label10, LF::ink(), { rx, 310.0f, 200.0f, 14.0f }, juce::Justification::centredLeft);
    g.setColour (LF::line());
    g.fillRect (decayX, 310.0f, 1.0f, 14.0f);
    drawLabel (g, "DECAY", label10, LF::ink(), { decayX + 8.0f, 310.0f, 200.0f, 14.0f }, juce::Justification::centredLeft);

    for (const Knob* k : { &tune, &color, &snap, &closed, &open, &shape })
    {
        const auto b = k->slider.getBounds().toFloat();
        const auto column = juce::Rectangle<float> (b.getCentreX() - knobColumn * 0.5f, b.getBottom(), (float) knobColumn, 14.0f);
        // La durée du type non choisi est grisée (elle reste réglable)
        const bool inactive = (k == &closed && openSelected) || (k == &open && ! openSelected);
        drawLabel (g, k->name, label9, inactive ? juce::Colour (0xff4a4a4a) : LF::label(), column.translated (0.0f, 8.0f), juce::Justification::centred);

        auto v = valueText (k->id);
        if (k == &shape)
        {
            const float s = audioProcessor.apvts.getRawParameterValue (HyperHatsAudioProcessor::shapeId)->load();
            v = s < 33.0f ? "natural" : s < 66.0f ? "tight" : "gated";
        }
        drawLabel (g, v, value10, inactive ? juce::Colour (0xff5a5a5a) : LF::ink(), column.translated (0.0f, 22.0f), juce::Justification::centred);
    }

    // Enveloppe
    drawLabel (g, "ENVELOPE", label9, LF::label(), { rx, 422.0f, rw, 14.0f }, juce::Justification::centredLeft);
    drawLabel (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 OPEN    - - CLOSED")), label9, LF::dim(), { rx, 422.0f, rw, 14.0f }, juce::Justification::centredRight);
    {
        const float T = juce::jmax (params.openMs * 1.08f, 300.0f);
        auto curve = [&] (float ms)
        {
            juce::Path path;
            const auto a = envelopeArea;
            auto toPoint = [&] (float x, float amp) { return juce::Point<float> (a.getX() + x / 420.0f * a.getWidth(), a.getY() + (58.0f - 52.0f * amp) / 60.0f * a.getHeight()); };
            path.startNewSubPath (toPoint (0.0f, 0.0f));
            path.lineTo (toPoint (1.0f, 0.55f + 0.45f * params.snap));

            for (int x = 4; x <= 420; x += 4)
            {
                const float tm = (float) x / 420.0f * T;
                const float ex = std::exp (-4.0f * tm / ms);
                const float r = tm / ms;
                const float gate = tm < ms ? 1.0f - std::pow (r, 6.0f) : 0.0f;
                path.lineTo (toPoint ((float) x, juce::jmax (0.0f, (1.0f - params.shape) * ex + params.shape * gate)));
            }

            return path;
        };

        g.setColour (juce::Colour (0xff262626));
        g.fillRect (envelopeArea.getX(), envelopeArea.getBottom(), envelopeArea.getWidth(), 1.0f);

        g.setColour (LF::white());
        g.strokePath (curve (params.openMs), juce::PathStrokeType (1.2f));

        juce::Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, curve (params.closedMs), dashes, 2);
        g.setColour (juce::Colour (0xff9a9a9a));
        g.fillPath (dashed);
    }

    drawLabel (g, "TYPE", label9, LF::label(), { 712.0f, (float) footerKnobY - 14.0f, 174.0f, 12.0f }, juce::Justification::centredLeft);
    drawLabel (g, "START FROM", label9, LF::dim(), { rx, 550.0f, rw, 14.0f }, juce::Justification::centredLeft);

    // Pied
    for (const Knob* k : { &widthKnob, &variation, &crush, &level })
    {
        const auto b = k->slider.getBounds().toFloat();
        drawLabel (g, k->name, label9, LF::label(), { b.getRight() + 12.0f, b.getY() + 6.0f, 110.0f, 14.0f }, juce::Justification::centredLeft);
        drawLabel (g, valueText (k->id), value10, LF::ink(), { b.getRight() + 12.0f, b.getY() + 22.0f, 110.0f, 14.0f }, juce::Justification::centredLeft);
    }
}

//==============================================================================
HyperHatsAudioProcessorEditor::HyperHatsAudioProcessorEditor (HyperHatsAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), facade (p)
{
    addAndMakeVisible (facade);

    const int savedWidth = (int) audioProcessor.apvts.state.getProperty ("editorWidth", HatsFacade::width);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (HatsFacade::width * 0.6), juce::roundToInt (HatsFacade::height * 0.6),
                     juce::roundToInt (HatsFacade::width * 1.5), juce::roundToInt (HatsFacade::height * 1.5));
    getConstrainer()->setFixedAspectRatio ((double) HatsFacade::width / (double) HatsFacade::height);

    const int shownWidth = juce::jlimit (juce::roundToInt (HatsFacade::width * 0.6), juce::roundToInt (HatsFacade::width * 1.5), savedWidth);
    setSize (shownWidth, juce::roundToInt ((double) shownWidth * HatsFacade::height / HatsFacade::width));
    sized = true;
    resized();
}

HyperHatsAudioProcessorEditor::~HyperHatsAudioProcessorEditor() = default;

void HyperHatsAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) HatsFacade::width;
    facade.setTransform (juce::AffineTransform::scale (scale));

    if (sized)
        audioProcessor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}
