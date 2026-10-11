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
            title = proc.getStatusMessage();
            sub = "Room loaded. Drop another sound or click to replace it";
            break;
        case RoomAudioProcessor::Status::failed:
            title = proc.getStatusMessage();
            sub = "Try another file (WAV, AIFF, FLAC, OGG or MP3)";
            break;
    }

    g.setColour (status == RoomAudioProcessor::Status::failed ? juce::Colours::indianred : Colours::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawFittedText (title, getLocalBounds().reduced (16, 0).withTrimmedBottom (getHeight() / 2 - 4),
                      juce::Justification::centredBottom, 1);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (sub, getLocalBounds().reduced (16, 0).withTrimmedTop (getHeight() / 2 + 4),
                      juce::Justification::centredTop, 1);
}

//==============================================================================
// Display

void RoomDisplay::setAnalysis (const room::RoomAnalysis& a, float newIntensity)
{
    analysis = a;
    intensity = newIntensity;
    eqCurve = room::targetEqCurveDb (analysis, intensity);
    repaint();
}

void RoomDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (area, 8.0f);

    if (! analysis.valid)
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("No room loaded", area, juce::Justification::centred);
        return;
    }

    area = area.reduced (12.0f, 10.0f);
    auto left = area.removeFromLeft (area.getWidth() * 0.45f).reduced (4.0f, 0.0f);
    area.removeFromLeft (16.0f);
    auto right = area;

    auto drawTitle = [&] (juce::Rectangle<float>& r, const juce::String& t)
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (t, r.removeFromTop (16.0f), juce::Justification::centredLeft);
        r.removeFromTop (4.0f);
    };

    // --- decay per band ---------------------------------------------------
    drawTitle (left, analysis.steady ? "DECAY  (steady sound: fallback)" : "DECAY PER BAND");
    const auto nb = analysis.numBands();
    float maxT = 0.5f;
    for (auto t : analysis.bandT60s) maxT = std::max (maxT, t);
    const auto bw = left.getWidth() / (float) nb;
    for (int b = 0; b < nb; ++b)
    {
        const auto h = left.getHeight() * analysis.bandT60s[(size_t) b] / maxT;
        g.setColour (analysis.bandConfident[(size_t) b] ? Colours::accent : Colours::outline);
        g.fillRoundedRectangle (left.getX() + b * bw + 1.5f, left.getBottom() - h, bw - 3.0f, h, 2.0f);
    }
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String (maxT, 1) + " s", left.withHeight (14.0f), juce::Justification::topRight);

    // --- tone curve + resonances --------------------------------------------
    drawTitle (right, "TONE (EQ + RESONANCES)");
    const auto fMin = 60.0f, fMax = 16000.0f, dbRange = 15.0f;
    auto xFor = [&] (float f) { return right.getX() + right.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yFor = [&] (float db) { return right.getCentreY() - right.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / dbRange); };

    g.setColour (Colours::outline);
    g.drawHorizontalLine ((int) right.getCentreY(), right.getX(), right.getRight());
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) xFor (f), right.getY(), right.getBottom());

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
        const auto h = right.getHeight() * 0.5f * juce::jlimit (0.0f, 1.0f, std::min (pk.gainDb, 18.0f) * intensity / 24.0f);
        g.setColour (Colours::accent2.withAlpha (0.8f));
        g.drawLine (x, right.getCentreY(), x, right.getCentreY() - h, 2.0f);
        g.fillEllipse (x - 3.0f, right.getCentreY() - h - 3.0f, 6.0f, 6.0f);
    }

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    for (auto [f, t] : { std::pair { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
        g.drawText (t, juce::Rectangle<float> (xFor (f) + 2.0f, right.getBottom() - 12.0f, 30.0f, 12.0f),
                    juce::Justification::centredLeft);
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
    setLookAndFeel (&lnf);

    addAndMakeVisible (dropZone);
    addAndMakeVisible (display);
    addAndMakeVisible (readout);
    addAndMakeVisible (clearButton);
    readout.setFont (juce::FontOptions (13.0f));
    readout.setColour (juce::Label::textColourId, Colours::dim);
    clearButton.onClick = [this] { proc.clearRoom(); };

    setupKnob (intensity, ParamIDs::intensity, "INTENSITY");
    setupKnob (mix, ParamIDs::mix, "MIX");
    setupKnob (balance, ParamIDs::balance, "BALANCE");
    setupKnob (tail, ParamIDs::tail, "TAIL");
    setupKnob (space, ParamIDs::space, "SPACE");
    setupKnob (output, ParamIDs::output, "OUTPUT");
    setupKnob (tape, ParamIDs::tape, "TAPE");
    setupKnob (autoEq, ParamIDs::autoEq, "AUTO EQ");
    autoEq.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::reduction);
    addAndMakeVisible (eqPanel);
    balance.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent2);

    addAndMakeVisible (autoBalance);
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

    // --- ROOMS presets ---
    addAndMakeVisible (presetBar);
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

    addChildComponent (browser);
    browser.onClose = [this] { browser.setVisible (false); };
    browser.onLoad = [this] (const juce::String& key) { loadPreset (key); };
    browser.onEdit = [this] (const juce::String& key, const room::presets::PresetMeta& meta, const juce::String& pack)
    {
        editingKey = key;
        saveDialog.open (room::presets::SaveDialog::Mode::edit, meta, proc.getLibrary().getPacks(), pack);
    };

    addChildComponent (saveDialog);
    saveDialog.onSubmit = [this] (auto mode, const auto& meta, const auto& pack, bool overwrite)
    {
        return submitSaveDialog (mode, meta, pack, overwrite);
    };

    setSize (900, 760);
    refreshAnalysis();
    startTimerHz (15);
}

RoomAudioProcessorEditor::~RoomAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void RoomAudioProcessorEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& name)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 150, 20);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
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
    if (version != shownVersion || std::abs (inten - shownIntensity) > 1e-4f)
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

    const auto a = proc.getAnalysis();
    display.setAnalysis (a, shownIntensity);
    dropZone.repaint();
    clearButton.setVisible (a.valid);

    if (! a.valid)
    {
        readout.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto lean = a.balance > 0.6f ? "spatial" : a.balance < 0.4f ? "tonal" : "in between";
    juce::String s;
    s << "Detected balance " << juce::String (a.balance, 2) << " (" << lean << ")"
      << "   |   Decay " << (a.steady ? juce::String ("none (steady sound)") : juce::String (a.medianT60, 2) + " s median")
      << "   |   " << (int) a.peaks.size() << " resonance" << (a.peaks.size() == 1 ? "" : "s");
    readout.setText (s, juce::dontSendNotification);
}

void RoomAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    auto header = getLocalBounds().removeFromTop (48).reduced (20, 0);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("ROOM", header, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("v" JucePlugin_VersionString, header.withTrimmedLeft (78), juce::Justification::centredLeft);

    // Section label above the EQ
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("EQ  +  TONE", eqLabelArea, juce::Justification::centredLeft);
}

void RoomAudioProcessorEditor::resized()
{
    browser.setBounds (getLocalBounds());
    saveDialog.setBounds (getLocalBounds());

    auto area = getLocalBounds().reduced (20, 0);
    presetBar.setBounds (area.withHeight (48).withTrimmedLeft (150).reduced (0, 9));
    area.removeFromTop (48);

    dropZone.setBounds (area.removeFromTop (62));
    area.removeFromTop (10);
    display.setBounds (area.removeFromTop (140));
    area.removeFromTop (4);

    auto readoutRow = area.removeFromTop (24);
    clearButton.setBounds (readoutRow.removeFromRight (60).reduced (0, 2));
    readout.setBounds (readoutRow);
    area.removeFromTop (10);

    auto knobs = area.removeFromTop (150);
    const auto w = knobs.getWidth() / 6;
    for (auto* k : { &intensity, &mix, &balance, &tail, &space, &output })
    {
        auto col = knobs.removeFromLeft (w).reduced (6, 0);
        k->label.setBounds (col.removeFromTop (18));
        auto toggleRow = col.removeFromBottom (24);   // same size knobs in every column
        if (k == &balance)
            autoBalance.setBounds (toggleRow.withSizeKeepingCentre (64, 22));
        k->slider.setBounds (col);
    }

    // --- EQ section: display on the left, TAPE and AUTO EQ stacked on the right
    area.removeFromTop (8);
    eqLabelArea = area.removeFromTop (20);
    area.removeFromBottom (16);
    auto side = area.removeFromRight (150);
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
