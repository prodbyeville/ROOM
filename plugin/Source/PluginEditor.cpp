#include "PluginEditor.h"

#include "UiColours.h"

//==============================================================================
// Drop zone

DropZone::DropZone (RoomAudioProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool DropZone::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (proc.formatManager.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void DropZone::filesDropped (const juce::StringArray& files, int, int)
{
    hovering = false;
    for (auto& f : files)
    {
        const juce::File file (f);
        if (proc.formatManager.findFormatForFileExtension (file.getFileExtension()) != nullptr)
        {
            proc.loadRoomFile (file);
            break;
        }
    }
    repaint();
}

void DropZone::mouseUp (const juce::MouseEvent&)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sound to build a room from",
                                                   juce::File(),
                                                   proc.formatManager.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  proc.loadRoomFile (fc.getResult());
                          });
}

void DropZone::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (hovering ? Colours::panel.brighter (0.15f) : Colours::panel);
    g.fillRoundedRectangle (r, 8.0f);

    juce::Path border;
    border.addRoundedRectangle (r, 8.0f);
    const float dashes[] = { 6.0f, 4.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (border, border, dashes, 2);
    g.setColour (hovering ? Colours::accent : Colours::outline);
    g.fillPath (border);

    const auto status = proc.getStatus();
    juce::String title, sub;
    switch (status)
    {
        case RoomAudioProcessor::Status::empty:
            title = "Drop any sound here";
            sub = "or click to browse. A clap, room tone, a one-shot, found sound...";
            break;
        case RoomAudioProcessor::Status::analyzing:
            title = proc.getStatusMessage();
            sub = "Listening to the room";
            break;
        case RoomAudioProcessor::Status::ready:
        {
            title = proc.getStatusMessage();
            // Short summary of what was detected (the full details are on ADVANCED).
            const auto a = proc.getAnalysis();
            const auto lean = a.balance > 0.6f ? "spatial" : a.balance < 0.4f ? "tonal" : "in between";
            sub << "Balance " << juce::String (a.balance, 2) << " " << lean
                << "   \xc2\xb7   " << (a.steady ? juce::String ("steady sound") : "decay " + juce::String (a.medianT60, 2) + " s")
                << "   \xc2\xb7   " << (int) a.peaks.size() << " resonance" << (a.peaks.size() == 1 ? "" : "s")
                << "        drop or click to replace";
            break;
        }
        case RoomAudioProcessor::Status::failed:
            title = proc.getStatusMessage();
            sub = "Try another file (WAV, AIFF, FLAC, OGG or MP3)";
            break;
    }

    auto textArea = getLocalBounds().reduced (16, 0).withTrimmedRight (70);
    g.setColour (status == RoomAudioProcessor::Status::failed ? juce::Colours::indianred : Colours::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawFittedText (title, textArea.withTrimmedBottom (getHeight() / 2 - 2), juce::Justification::centredBottom, 1);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (sub, textArea.withTrimmedTop (getHeight() / 2 + 4), juce::Justification::centredTop, 1);
}

//==============================================================================
// Room displays

void RoomDisplay::setAnalysis (const room::RoomAnalysis& a, float newIntensity, float newSize, float newDamping)
{
    analysis = a;
    intensity = newIntensity;
    size = newSize;
    damping = newDamping;
    eqCurve = room::targetEqCurveDb (analysis, intensity);
    repaint();
}

void RoomDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (area, 8.0f);

    area = area.reduced (14.0f, 10.0f);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (mode == Mode::decay ? (analysis.valid && analysis.steady ? "DECAY  (steady sound: estimated)" : "DECAY PER BAND")
                                    : "TONE  (EQ + RESONANCES)",
                area.removeFromTop (16.0f), juce::Justification::centredLeft);
    area.removeFromTop (4.0f);

    if (! analysis.valid)
    {
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("No room loaded", area, juce::Justification::centred);
        return;
    }

    if (mode == Mode::decay)
    {
        const auto nb = analysis.numBands();
        std::vector<float> t ((size_t) nb);
        float maxT = 0.5f;
        for (int b = 0; b < nb; ++b)
        {
            t[(size_t) b] = room::bandDecay (analysis.bandT60s[(size_t) b], analysis.bandCentersHz[(size_t) b],
                                             intensity, size, damping);
            maxT = std::max (maxT, t[(size_t) b]);
        }
        const auto bw = area.getWidth() / (float) nb;
        for (int b = 0; b < nb; ++b)
        {
            const auto h = area.getHeight() * t[(size_t) b] / maxT;
            g.setColour (analysis.bandConfident[(size_t) b] ? Colours::accent : Colours::outline);
            g.fillRoundedRectangle (area.getX() + b * bw + 1.5f, area.getBottom() - h, bw - 3.0f, h, 2.0f);
        }
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (maxT, 1) + " s", area.withHeight (14.0f).translated (0.0f, -20.0f), juce::Justification::topRight);
        return;
    }

    const auto fMin = 60.0f, fMax = 16000.0f, dbRange = 15.0f;
    auto xFor = [&] (float f) { return area.getX() + area.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yFor = [&] (float db) { return area.getCentreY() - area.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / dbRange); };

    g.setColour (Colours::outline);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) xFor (f), area.getY(), area.getBottom());

    juce::Path p;
    for (size_t i = 0; i < eqCurve.size(); ++i)
    {
        const auto x = xFor (analysis.toneFreqsHz[i]), y = yFor (eqCurve[i]);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (Colours::accent2);
    g.strokePath (p, juce::PathStrokeType (2.0f));

    for (auto& pk : analysis.peaks)
    {
        const auto x = xFor (pk.freqHz);
        const auto h = area.getHeight() * 0.5f * juce::jlimit (0.0f, 1.0f, std::min (pk.gainDb, 18.0f) * intensity / 24.0f);
        g.setColour (Colours::accent2.withAlpha (0.8f));
        g.drawLine (x, area.getCentreY(), x, area.getCentreY() - h, 2.0f);
        g.fillEllipse (x - 3.0f, area.getCentreY() - h - 3.0f, 6.0f, 6.0f);
    }

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    for (auto [f, t] : { std::pair { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
        g.drawText (t, juce::Rectangle<float> (xFor (f) + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f),
                    juce::Justification::centredLeft);
}

//==============================================================================
// Icon buttons

IconButton::IconButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i)
{
    setTooltip (tip);
    if (icon != Icon::randomize)
        setClickingTogglesState (true);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto active = icon == Icon::power ? on : (icon == Icon::advanced && on);
    g.setColour (active ? (icon == Icon::power ? Colours::reduction.withAlpha (0.25f) : Colours::accent.withAlpha (0.2f))
                        : (over ? Colours::panel.brighter (0.1f) : Colours::panel));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (active ? (icon == Icon::power ? Colours::reduction : Colours::accent) : Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    const auto c = active ? (icon == Icon::power ? Colours::reduction : Colours::accent)
                          : (over || down ? Colours::text : Colours::dim.brighter (0.2f));
    g.setColour (c);
    const auto s = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    const auto cx = r.getCentreX(), cy = r.getCentreY();
    juce::PathStrokeType stroke (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path p;
    switch (icon)
    {
        case Icon::power:
        {
            p.addCentredArc (cx, cy + 1.0f, s * 0.62f, s * 0.62f, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
            p.startNewSubPath (cx, cy - s * 0.75f);
            p.lineTo (cx, cy - s * 0.05f);
            break;
        }
        case Icon::randomize:
        {
            // two crossing arrows (shuffle)
            const auto x0 = cx - s * 0.7f, x1 = cx + s * 0.7f, y0 = cy - s * 0.45f, y1 = cy + s * 0.45f;
            p.startNewSubPath (x0, y1);
            p.cubicTo (cx, y1, cx, y0, x1, y0);
            p.startNewSubPath (x0, y0);
            p.cubicTo (cx, y0, cx, y1, x1, y1);
            for (auto y : { y0, y1 })
            {
                p.startNewSubPath (x1 - s * 0.25f, y - s * 0.22f);
                p.lineTo (x1, y);
                p.lineTo (x1 - s * 0.25f, y + s * 0.22f);
            }
            break;
        }
        case Icon::advanced:
        {
            // three sliders ("settings")
            for (int k = 0; k < 3; ++k)
            {
                const auto y = cy + (k - 1) * s * 0.5f;
                p.startNewSubPath (cx - s * 0.7f, y);
                p.lineTo (cx + s * 0.7f, y);
                const auto kx = cx + (k == 1 ? 0.3f : -0.3f) * s;
                p.addEllipse (kx - s * 0.13f, y - s * 0.13f, s * 0.26f, s * 0.26f);
            }
            break;
        }
    }
    g.strokePath (p, stroke);
}

//==============================================================================
// Editor

RoomAudioProcessorEditor::RoomAudioProcessorEditor (RoomAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), dropZone (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, Colours::outline);
    lnf.setColour (juce::Slider::thumbColourId, Colours::text);
    lnf.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    lnf.setColour (juce::Label::textColourId, Colours::text);
    lnf.setColour (juce::ToggleButton::textColourId, Colours::text);
    lnf.setColour (juce::ToggleButton::tickColourId, Colours::accent);
    lnf.setColour (juce::TextButton::buttonColourId, Colours::panel);
    lnf.setColour (juce::TextButton::textColourOffId, Colours::dim);
    lnf.setColour (juce::BubbleComponent::backgroundColourId, Colours::panel);
    lnf.setColour (juce::BubbleComponent::outlineColourId, Colours::outline);
    lnf.setColour (juce::TooltipWindow::backgroundColourId, Colours::panel);
    lnf.setColour (juce::TooltipWindow::textColourId, Colours::text);
    lnf.setColour (juce::TooltipWindow::outlineColourId, Colours::outline);
    setLookAndFeel (&lnf);

    addAndMakeVisible (content);
    content.painter = [this] (juce::Graphics& g) { paintContent (g); };
    content.layout = [this] { layoutContent(); };

    // --- Top bar ---
    for (auto* b : { &advancedButton, &randomButton, &bypassButton })
        content.addAndMakeVisible (b);
    advancedButton.onClick = [this] { setAdvanced (advancedButton.getToggleState()); };
    randomButton.onClick = [this] { proc.randomizeKnobs (random); };
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::bypass, bypassButton);
    setupKnob (mix, ParamIDs::mix, "DRY/WET", true);
    setupKnob (output, ParamIDs::output, "OUT", true);

    // --- Tier 1 ---
    content.addAndMakeVisible (dropZone);
    content.addAndMakeVisible (clearButton);
    clearButton.onClick = [this] { proc.clearRoom(); };

    // --- Tier 2 ---
    content.addAndMakeVisible (decayDisplay);
    content.addAndMakeVisible (toneDisplay);
    setupKnob (intensity, ParamIDs::intensity, "INTENSITY");
    setupKnob (balance, ParamIDs::balance, "BALANCE");
    setupKnob (tail, ParamIDs::tail, "TAIL");
    setupKnob (space, ParamIDs::space, "SPACE");
    balance.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent2);
    content.addAndMakeVisible (autoBalance);
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, ParamIDs::balanceAuto, autoBalance);
    // With Auto on, the Balance knob shows what was detected, not its own value.
    balance.slider.textFromValueFunction = [this] (double v)
    {
        const auto isAuto = autoBalance.getToggleState();
        const auto b = isAuto ? proc.getDetectedBalance() : (float) v;
        return (isAuto ? "Auto: " : "") + juce::String (juce::roundToInt ((1.0f - b) * 100.0f)) + " tone / "
               + juce::String (juce::roundToInt (b * 100.0f)) + " space";
    };
    balance.slider.updateText();

    // --- Tier 3 ---
    content.addAndMakeVisible (eqPanel);
    setupKnob (tape, ParamIDs::tape, "TAPE");
    setupKnob (autoEq, ParamIDs::autoEq, "AUTO EQ");
    autoEq.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::reduction);
    content.addChildComponent (advancedPanel);

    // --- ROOMS presets ---
    content.addAndMakeVisible (presetBar);
    presetBar.onOpen = [this] { browser.setVisible (true); browser.toFront (true); };
    presetBar.onPrev = [this] { stepPreset (-1); };
    presetBar.onNext = [this] { stepPreset (1); };
    presetBar.onSave = [this] { openSaveDialog(); };
    presetBar.onToggleFavorite = [this]
    {
        auto& lib = proc.getLibrary();
        if (auto* e = lib.find (proc.getCurrentPresetKey()))
            lib.setFavorite (e->key, ! e->favorite);
    };

    content.addChildComponent (browser);
    browser.onClose = [this] { browser.setVisible (false); };
    browser.onLoad = [this] (const juce::String& key) { loadPreset (key); };
    browser.onEdit = [this] (const juce::String& key, const room::presets::PresetMeta& meta, const juce::String& pack)
    {
        editingKey = key;
        saveDialog.open (room::presets::SaveDialog::Mode::edit, meta, proc.getLibrary().getPacks(), pack);
    };

    content.addChildComponent (saveDialog);
    saveDialog.onSubmit = [this] (auto mode, const auto& meta, const auto& pack, bool overwrite)
    {
        return submitSaveDialog (mode, meta, pack, overwrite);
    };

    content.setBounds (0, 0, baseWidth, baseHeight);
    setAdvanced (proc.advancedOpen);

    // Resizable, keeping the shape: everything scales together.
    // (Size first: the limits/aspect constrainer would otherwise start from 0 x 0.)
    const auto startScale = proc.editorScale;
    setSize (juce::roundToInt (baseWidth * startScale), juce::roundToInt (baseHeight * startScale));
    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 7 / 4, baseHeight * 7 / 4);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (juce::roundToInt (baseWidth * startScale), juce::roundToInt (baseHeight * startScale));

    refreshAnalysis();
    startTimerHz (15);
}

RoomAudioProcessorEditor::~RoomAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void RoomAudioProcessorEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& name, bool small)
{
    if (small)
    {
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.slider.setPopupDisplayEnabled (true, true, &content);
        k.label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    }
    else
    {
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 150, 20);
        k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    }
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, Colours::dim);
    content.addAndMakeVisible (k.slider);
    content.addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

void RoomAudioProcessorEditor::setAdvanced (bool adv)
{
    proc.advancedOpen = adv;
    advancedButton.setToggleState (adv, juce::dontSendNotification);
    advancedPanel.setVisible (adv);
    eqPanel.setVisible (! adv);
    for (auto* k : { &tape, &autoEq })
    {
        k->slider.setVisible (! adv);
        k->label.setVisible (! adv);
    }
    content.repaint();
}

void RoomAudioProcessorEditor::timerCallback()
{
    {
        auto& lib = proc.getLibrary();
        const auto* e = lib.find (proc.getCurrentPresetKey());
        presetBar.setPreset (proc.getCurrentPresetName(), proc.isPresetModified(), e != nullptr && e->favorite, e != nullptr);
    }

    const auto version = proc.getAnalysisVersion();
    const auto inten = proc.apvts.getRawParameterValue (ParamIDs::intensity)->load();
    const auto sz = proc.apvts.getRawParameterValue (ParamIDs::size)->load();
    const auto dmp = proc.apvts.getRawParameterValue (ParamIDs::damping)->load();
    if (version != shownVersion || std::abs (inten - shownIntensity) > 1e-4f
        || std::abs (sz - shownSize) > 1e-4f || std::abs (dmp - shownDamping) > 1e-4f)
        refreshAnalysis();

    const auto isAuto = autoBalance.getToggleState();
    if (balance.slider.isEnabled() == isAuto)
    {
        balance.slider.setEnabled (! isAuto);
        balance.slider.setAlpha (isAuto ? 0.55f : 1.0f);
    }
    balance.slider.updateText();
}

void RoomAudioProcessorEditor::refreshAnalysis()
{
    shownVersion = proc.getAnalysisVersion();
    shownIntensity = proc.apvts.getRawParameterValue (ParamIDs::intensity)->load();
    shownSize = proc.apvts.getRawParameterValue (ParamIDs::size)->load();
    shownDamping = proc.apvts.getRawParameterValue (ParamIDs::damping)->load();

    const auto a = proc.getAnalysis();
    decayDisplay.setAnalysis (a, shownIntensity, shownSize, shownDamping);
    toneDisplay.setAnalysis (a, shownIntensity, shownSize, shownDamping);
    advancedPanel.setAnalysis (a);
    dropZone.repaint();
    clearButton.setVisible (a.valid);
}

void RoomAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    auto header = juce::Rectangle<int> (20, 0, 140, 56);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("ROOM", header, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("v" JucePlugin_VersionString, header.withTrimmedLeft (78), juce::Justification::centredLeft);

    // Divider under the top bar
    g.setColour (Colours::outline);
    g.drawHorizontalLine (56, 0.0f, (float) baseWidth);

    // Tier numbers down the left edge: 1 load, 2 shape, 3 finish / advanced
    const juce::String names[3] = { "1", "2", proc.advancedOpen ? "A" : "3" };
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (Colours::outline.brighter (0.25f));
        g.drawText (names[i], tierLabels[i], juce::Justification::centredTop);
    }
}

void RoomAudioProcessorEditor::resized()
{
    const auto scale = (float) getWidth() / (float) baseWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    proc.editorScale = scale;
}

void RoomAudioProcessorEditor::layoutContent()
{
    const auto full = juce::Rectangle<int> (0, 0, baseWidth, baseHeight);
    browser.setBounds (full);
    saveDialog.setBounds (full);

    // --- Top bar ---
    auto bar = juce::Rectangle<int> (20, 0, baseWidth - 40, 56);
    bar.removeFromLeft (122);
    auto right = bar.removeFromRight (250);
    presetBar.setBounds (bar.withTrimmedRight (16).reduced (0, 11));
    {
        auto r = right;
        bypassButton.setBounds (r.removeFromRight (36).withSizeKeepingCentre (36, 34));
        r.removeFromRight (8);
        for (auto* k : { &output, &mix })
        {
            auto col = r.removeFromRight (52);
            k->slider.setBounds (col.withTrimmedBottom (14).withSizeKeepingCentre (38, 38).translated (0, 2));
            k->label.setBounds (col.removeFromBottom (16).translated (0, -2));
            r.removeFromRight (2);
        }
        r.removeFromRight (6);
        randomButton.setBounds (r.removeFromRight (36).withSizeKeepingCentre (36, 34));
        r.removeFromRight (8);
        advancedButton.setBounds (r.removeFromRight (36).withSizeKeepingCentre (36, 34));
    }

    // Tier column on the left for the 1 / 2 / 3 markers
    auto area = juce::Rectangle<int> (0, 56, baseWidth, baseHeight - 56).reduced (20, 0);
    auto gutterX = 8;

    // --- Tier 1: load ---
    area.removeFromTop (12);
    auto t1 = area.removeFromTop (64);
    tierLabels[0] = { gutterX, t1.getY() + 4, 12, 20 };
    dropZone.setBounds (t1);
    clearButton.setBounds (t1.removeFromRight (70).reduced (10, 18));

    // --- Tier 2: shape the room ---
    area.removeFromTop (12);
    auto displays = area.removeFromTop (140);
    tierLabels[1] = { gutterX, displays.getY() + 4, 12, 20 };
    auto leftD = displays.removeFromLeft ((displays.getWidth() - 12) / 2);
    displays.removeFromLeft (12);
    decayDisplay.setBounds (leftD);
    toneDisplay.setBounds (displays);

    area.removeFromTop (10);
    auto knobs = area.removeFromTop (150);
    const auto w = knobs.getWidth() / 4;
    for (auto* k : { &intensity, &balance, &tail, &space })
    {
        auto col = knobs.removeFromLeft (w).reduced (10, 0);
        k->label.setBounds (col.removeFromTop (18));
        auto toggleRow = col.removeFromBottom (24);   // same size knobs in every column
        if (k == &balance)
            autoBalance.setBounds (toggleRow.withSizeKeepingCentre (64, 22));
        k->slider.setBounds (col);
    }

    // --- Tier 3: finish (EQ + Tape / Auto EQ)  or  ADVANCED ---
    area.removeFromTop (10);
    area.removeFromBottom (16);
    tierLabels[2] = { gutterX, area.getY() + 4, 12, 20 };
    advancedPanel.setBounds (area);
    auto side = area.removeFromRight (140);
    area.removeFromRight (12);
    eqPanel.setBounds (area);
    for (auto* k : { &tape, &autoEq })
    {
        auto col = side.removeFromTop (side.getHeight() / (k == &tape ? 2 : 1)).reduced (0, 4);
        k->label.setBounds (col.removeFromTop (18));
        k->slider.setBounds (col);
    }
}

//==============================================================================
// ROOMS presets

void RoomAudioProcessorEditor::loadPreset (const juce::String& key)
{
    auto& lib = proc.getLibrary();
    const auto* e = lib.find (key);
    if (e == nullptr)
        return;
    const auto tree = lib.loadPreset (*e);
    if (! tree.isValid())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("ROOMS")
                                          .withMessage ("\"" + e->meta.name + "\" couldn't be read.").withButton ("OK"), nullptr);
        return;
    }
    proc.applyPreset (tree, key);
    browser.repaint();
}

void RoomAudioProcessorEditor::stepPreset (int delta)
{
    auto& lib = proc.getLibrary();
    const auto list = browser.currentList();
    if (list.empty())
        return;

    int index = -1;
    for (int i = 0; i < (int) list.size(); ++i)
        if (lib.getEntries()[(size_t) list[(size_t) i]].key == proc.getCurrentPresetKey())
            index = i;

    const auto n = (int) list.size();
    const auto next = index < 0 ? (delta > 0 ? 0 : n - 1) : ((index + delta) % n + n) % n;
    loadPreset (lib.getEntries()[(size_t) list[(size_t) next]].key);
}

void RoomAudioProcessorEditor::openSaveDialog()
{
    auto& lib = proc.getLibrary();
    room::presets::PresetMeta meta;
    juce::String pack = room::presets::PresetLibrary::userPack;
    if (const auto* e = lib.find (proc.getCurrentPresetKey()))
    {
        meta = e->meta;
        meta.created = {};
        if (e->factory)
            meta.author = {};   // a copy of a factory ROOM becomes yours
        else
            pack = e->pack;
    }
    else
        meta.name = proc.getCurrentPresetName();

    editingKey.clear();
    saveDialog.open (room::presets::SaveDialog::Mode::save, meta, lib.getPacks(), pack);
}

juce::String RoomAudioProcessorEditor::submitSaveDialog (room::presets::SaveDialog::Mode mode,
                                                         const room::presets::PresetMeta& metaIn,
                                                         const juce::String& pack, bool overwrite)
{
    using room::presets::PresetLibrary;
    auto& lib = proc.getLibrary();
    auto meta = metaIn;

    if (meta.name.isEmpty())
        return "Give the ROOM a name.";
    if (meta.type.isEmpty())
        return "Pick a type.";
    if (! PresetLibrary::isValidPackName (pack))
        return "Choose or name a pack (not Factory).";

    juce::String err;
    if (! lib.getPacks().contains (pack) && ! lib.createPack (pack, err))
        return err;

    juce::String key;
    if (mode == room::presets::SaveDialog::Mode::save)
    {
        if (meta.author.isEmpty())
            meta.author = "User";
        meta.created = juce::Time::getCurrentTime();
        if (! lib.save (proc.capturePreset (meta), pack, overwrite, key, err))
            return err;
        proc.setCurrentPreset (key, meta.name);
    }
    else
    {
        if (! lib.updateMeta (editingKey, meta, pack, key, err))
            return err;
        if (editingKey == proc.getCurrentPresetKey())
            proc.relabelCurrentPreset (key, meta.name);
    }

    if (browser.isVisible())
        browser.refresh();
    return {};
}
