#pragma once

#include "PluginProcessor.h"
#include "Randomizer.h"
#include "EvilleLookAndFeel.h"
#include "UiColours.h"

// The Randomize page (WARP's, in ROOM's dress): over the whole window, opened and
// closed by right-clicking the randomize key. Tick what may change, set how wild
// (basic / moderate / extreme), randomize from here or from the key.
class RandomizePage : public juce::Component
{
public:
    RandomizePage (RoomAudioProcessor& p, juce::Random& r) : proc (p), rng (r)
    {
        for (const auto& t : room::Randomizer::targets())
        {
            auto* box = boxes.add (new juce::ToggleButton (t.label));
            box->getProperties().set ("style", "switch");
            box->onClick = [this, key = t.key, box] { room::Randomizer::setSelected (proc, key, box->getToggleState()); };
            addAndMakeVisible (box);
        }
        for (auto* b : { &allButton, &noneButton, &randomizeButton, &closeButton })
            addAndMakeVisible (*b);
        allButton.onClick = [this] { setAll (true); };
        noneButton.onClick = [this] { setAll (false); };
        randomizeButton.onClick = [this] { room::Randomizer::randomizeSelected (proc, rng); };
        closeButton.onClick = [this] { setVisible (false); };

        intensity.setSliderStyle (juce::Slider::LinearHorizontal);
        intensity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        intensity.setRange (0.0, 1.0);
        intensity.setDoubleClickReturnValue (true, 0.5);
        intensity.setTooltip ("how wild a randomize is: basic stays musical, moderate goes further, extreme uses every knob's whole range");
        intensity.onValueChange = [this] { room::Randomizer::setIntensity (proc, intensity.getValue()); repaint (intensityLabels); };
        addAndMakeVisible (intensity);
    }

    void refresh()
    {
        const auto& ts = room::Randomizer::targets();
        for (int i = 0; i < boxes.size(); ++i)
            boxes[i]->setToggleState (room::Randomizer::isSelected (proc, ts[(size_t) i].key), juce::dontSendNotification);
        intensity.setValue (room::Randomizer::getIntensity (proc), juce::dontSendNotification);
        repaint();
    }

    void visibilityChanged() override { if (isVisible()) refresh(); }
    void mouseDown (const juce::MouseEvent&) override {}   // swallow clicks: nothing under the page is touched

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::window.withAlpha (0.97f));
        auto head = getLocalBounds().reduced (28, 0).removeFromTop (88);
        g.setColour (Colours::dustWhite);
        g.setFont (Fonts::sans (24.0f, Fonts::bold));
        g.drawText ("randomize", head.removeFromTop (60).withTrimmedTop (24), juce::Justification::centredLeft);
        g.setFont (Fonts::sans (13.0f));
        g.setColour (Colours::textDim);
        g.drawFittedText ("tick what the randomizer may change. click the randomize key to randomize;\nright-click it to open or close this page.",
                          head.withTrimmedRight (std::min (400, getWidth() / 3) + 20), juce::Justification::topLeft, 2, 1.0f);

        EvilleLookAndFeel::drawCaps (g, "intensity", intensityCaption.toFloat(), 11.0f, Colours::dustWhite.withAlpha (0.85f),
                                     juce::Justification::centredLeft, Fonts::bold);
        const auto level = juce::roundToInt (intensity.getValue() * 2.0);
        const char* names[] { "basic", "moderate", "extreme" };
        const juce::Justification placements[] { juce::Justification::centredLeft, juce::Justification::centred, juce::Justification::centredRight };
        for (int i = 0; i < 3; ++i)
            EvilleLookAndFeel::drawCaps (g, names[i], intensityLabels.toFloat(), 9.5f,
                                         i == level ? Colours::redLight : Colours::dustWhite.withAlpha (0.5f), placements[i],
                                         i == level ? Fonts::bold : Fonts::medium);

        for (const auto& [group, area] : groupHeaders)
        {
            EvilleLookAndFeel::drawCaps (g, group, area.toFloat().withTrimmedBottom (6.0f), 11.5f, Colours::dustWhite.withAlpha (0.9f),
                                         juce::Justification::centredLeft, Fonts::bold);
            g.setColour (Colours::moduleEdge);
            g.fillRect (area.getX(), area.getBottom() - 4, area.getWidth(), 1);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (28, 0).withTrimmedTop (24).withTrimmedBottom (24);

        // intensity, top right: caption, slider, then basic / moderate / extreme
        auto top = area.withHeight (60).removeFromRight (std::min (360, area.getWidth() / 3));
        intensityCaption = top.removeFromLeft (84).withTrimmedBottom (18);
        intensity.setBounds (top.removeFromTop (34).withTrimmedTop (6));
        intensityLabels = top.removeFromTop (16).reduced (6, 0);
        area.removeFromTop (88);

        auto buttons = area.removeFromBottom (36);
        closeButton.setBounds (buttons.removeFromRight (96));
        buttons.removeFromRight (10);
        randomizeButton.setBounds (buttons.removeFromRight (130));
        allButton.setBounds (buttons.removeFromLeft (76));
        buttons.removeFromLeft (10);
        noneButton.setBounds (buttons.removeFromLeft (76));
        area.removeFromBottom (16);

        // groups in columns, in the order the targets are listed
        groupHeaders.clear();
        int groups = 0;
        {
            juce::String last;
            for (const auto& t : room::Randomizer::targets())
                if (juce::String (t.group) != last) { last = t.group; ++groups; }
        }
        const auto columnWidth = area.getWidth() / std::max (1, groups);
        constexpr int rowHeight = 30;
        juce::String current;
        juce::Rectangle<int> column;
        int columnIndex = -1;
        for (int i = 0; i < boxes.size(); ++i)
        {
            const juce::String group (room::Randomizer::targets()[(size_t) i].group);
            if (group != current)
            {
                current = group;
                column = area.withX (area.getX() + (++columnIndex) * columnWidth).withWidth (columnWidth - 14);
                groupHeaders.push_back ({ group, column.removeFromTop (rowHeight + 4) });
                column.removeFromTop (4);
            }
            boxes[i]->setBounds (column.removeFromTop (rowHeight));
        }
    }

private:
    void setAll (bool on)
    {
        for (const auto& t : room::Randomizer::targets())
            room::Randomizer::setSelected (proc, t.key, on);
        refresh();
    }

    RoomAudioProcessor& proc;
    juce::Random& rng;
    juce::OwnedArray<juce::ToggleButton> boxes;
    juce::TextButton allButton { "all" }, noneButton { "none" }, randomizeButton { "randomize" }, closeButton { "close" };
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> groupHeaders;
    juce::Slider intensity;
    juce::Rectangle<int> intensityCaption, intensityLabels;
};
