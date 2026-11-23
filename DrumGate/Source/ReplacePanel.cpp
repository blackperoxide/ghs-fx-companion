#include "GateEditor.h"

using namespace GateColours;

ReplacePanel::ReplacePanel(GHSDrumGateProcessor& p)
    : proc(p),
      sampleLevel(p.apvts, GateParamIDs::sampleLevel, "SAMPLE"),
      originalLevel(p.apvts, GateParamIDs::originalLevel, "ORIGINAL"),
      dynamics(p.apvts, GateParamIDs::sampleDyn, "DYNAMICS"),
      pitch(p.apvts, GateParamIDs::samplePitch, "PITCH"),
      length(p.apvts, GateParamIDs::sampleLength, "LENGTH"),
      offset(p.apvts, GateParamIDs::sampleOffset, "OFFSET")
{
    replaceButton.setColour(juce::ToggleButton::tickColourId, accent);
    chokeButton.setColour(juce::ToggleButton::tickColourId, key);
    addAndMakeVisible(replaceButton);
    addAndMakeVisible(chokeButton);
    replaceAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, GateParamIDs::replaceOn, replaceButton);
    chokeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, GateParamIDs::sampleChoke, chokeButton);
    chokeButton.setTooltip("A new hit fades out the previous sample (like a real drum)");

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(GateParamIDs::kit)))
        kitBox.addItemList(choice->choices, 1);
    addAndMakeVisible(kitBox);
    kitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, GateParamIDs::kit, kitBox);

    loadButton.setColour(juce::TextButton::buttonColourId, accent.withAlpha(0.85f));
    loadButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Choose one-shot samples (or a folder of them)", juce::File(),
                                                      "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                             [this](const juce::FileChooser& fc) { if (! fc.getResults().isEmpty()) load(fc.getResults()); });
    };
    clearButton.setColour(juce::TextButton::buttonColourId, panelEdge);
    clearButton.setColour(juce::TextButton::textColourOffId, text);
    clearButton.onClick = [this] { proc.clearUserSamples(); message = "User samples cleared"; };
    addAndMakeVisible(loadButton);
    addAndMakeVisible(clearButton);

    for (auto* k : { &sampleLevel, &dynamics, &pitch, &length })
        k->slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    originalLevel.slider.setColour(juce::Slider::rotarySliderFillColourId, dimText);
    offset.slider.setColour(juce::Slider::rotarySliderFillColourId, key);
    offset.slider.setTooltip("Nudge the samples against the original. Negative = earlier (uses the gate's Lookahead).");
    for (auto* k : { &sampleLevel, &originalLevel, &dynamics, &pitch, &length, &offset })
        addAndMakeVisible(*k);

    startTimerHz(30);
}

ReplacePanel::~ReplacePanel() { stopTimer(); }

void ReplacePanel::load(const juce::Array<juce::File>& files)
{
    const auto err = proc.loadUserSamples(files);
    message = err.isEmpty() ? "Loaded " + juce::String(proc.getUserSamplePaths().size()) + " samples" : err;
    repaint();
}

void ReplacePanel::filesDropped(const juce::StringArray& files, int, int)
{
    dragging = false;
    juce::Array<juce::File> fs;
    for (auto& f : files) fs.add(juce::File(f));
    load(fs);
}

void ReplacePanel::timerCallback()
{
    if (! isVisible()) return;
    layers = proc.describeCurrentKit();
    const int hits = proc.hitCounter.load();
    if (hits != lastHitCount)
    {
        lastHitCount = hits;
        flash = 1.0f;
        flashLayer = proc.lastHitLayer.load();
        flashVelocity = proc.lastHitVelocity.load();
    }
    else
        flash *= 0.85f;

    const bool on = replaceButton.getToggleState();
    for (auto* k : { &sampleLevel, &originalLevel, &dynamics, &pitch, &length, &offset })
        k->setAlpha(on ? 1.0f : 0.45f);
    clearButton.setEnabled(! proc.getUserSamplePaths().isEmpty());
    repaint();
}

void ReplacePanel::resized()
{
    auto r = getLocalBounds();
    left = r.removeFromLeft(r.getWidth() * 5 / 11);
    r.removeFromLeft(10);
    right = r;

    auto l = left.reduced(14, 0).withTrimmedTop(34);
    auto row = l.removeFromTop(28);
    replaceButton.setBounds(row.removeFromLeft(210));
    row.removeFromLeft(10);
    kitBox.setBounds(row);
    l.removeFromTop(10);
    row = l.removeFromTop(28);
    loadButton.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(8);
    clearButton.setBounds(row.removeFromLeft(80));
    l.removeFromTop(10);
    listArea = l.withTrimmedBottom(10);

    auto k = right.reduced(10, 0).withTrimmedTop(34);
    chokeButton.setBounds(right.reduced(12, 0).removeFromTop(26).removeFromRight(90).withY(right.getY() + 4));
    k.removeFromBottom(44); // hint text
    auto knobs = k.removeFromTop(std::min(k.getHeight(), 150));
    const int w = knobs.getWidth() / 3;
    auto row1 = knobs;
    // two rows of three when there's room, otherwise one row of six
    if (k.getHeight() >= 140)
    {
        auto row2 = k.removeFromTop(150);
        sampleLevel.setBounds(row1.removeFromLeft(w).reduced(4, 2));
        originalLevel.setBounds(row1.removeFromLeft(w).reduced(4, 2));
        dynamics.setBounds(row1.reduced(4, 2));
        pitch.setBounds(row2.removeFromLeft(w).reduced(4, 2));
        length.setBounds(row2.removeFromLeft(w).reduced(4, 2));
        offset.setBounds(row2.reduced(4, 2));
    }
    else
    {
        const int w6 = row1.getWidth() / 6;
        for (auto* kn : { &sampleLevel, &originalLevel, &dynamics, &pitch, &length, &offset })
            kn->setBounds(row1.removeFromLeft(w6).reduced(2, 2));
    }
}

void ReplacePanel::paint(juce::Graphics& g)
{
    auto box = [&](juce::Rectangle<int> b, const juce::String& title)
    {
        g.setColour(panel);
        g.fillRoundedRectangle(b.toFloat(), 6.0f);
        g.setColour(dragging && b == left ? accent : panelEdge);
        g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 6.0f, dragging && b == left ? 2.0f : 1.0f);
        g.setColour(dimText);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(title, b.reduced(12, 0).removeFromTop(26), juce::Justification::centredLeft);
    };
    box(left, "SAMPLES");
    box(right, "SOUND");

    // layer list with the last hit flashing
    auto a = listArea;
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.setColour(dimText);
    g.drawText("VELOCITY LAYERS  (soft > hard; samples in a layer play round-robin)", a.removeFromTop(18), juce::Justification::centredLeft);
    a.removeFromTop(4);
    if (layers.isEmpty())
    {
        g.setColour(dimText);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText("No samples yet. Drop one-shots (or a folder) here, or press Load Samples. They're sorted into velocity "
                         "layers by loudness automatically - files at the same loudness become round-robins.",
                         a.removeFromTop(60), juce::Justification::topLeft, 3);
    }
    else
    {
        const int rowH = std::min(26, a.getHeight() / std::max(1, layers.size()));
        for (int i = 0; i < layers.size(); ++i)
        {
            auto row = a.removeFromTop(rowH).reduced(0, 2);
            const bool lit = i == flashLayer && flash > 0.05f;
            g.setColour(lit ? accent.withAlpha(0.15f + 0.5f * flash) : panelEdge.withAlpha(0.5f));
            g.fillRoundedRectangle(row.toFloat(), 3.0f);
            g.setColour(lit ? juce::Colours::white : text);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(layers[i], row.reduced(8, 0), juce::Justification::centredLeft, true);
        }
    }

    // velocity of the last hit
    auto v = left.reduced(14, 10).removeFromBottom(16);
    g.setColour(dimText);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("LAST HIT", v.removeFromLeft(62), juce::Justification::centredLeft);
    auto bar = v.removeFromLeft(160).reduced(0, 3).toFloat();
    g.setColour(panelEdge);
    g.fillRoundedRectangle(bar, 2.0f);
    g.setColour(accent.withAlpha(0.4f + 0.6f * flash));
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * flashVelocity), 2.0f);
    v.removeFromLeft(8);
    g.drawText("vel " + juce::String(juce::roundToInt(flashVelocity * 127)) + (message.isNotEmpty() ? "     " + message : juce::String()),
               v, juce::Justification::centredLeft, true);

    // hint under the knobs
    g.setColour(dimText);
    g.setFont(juce::FontOptions(11.0f));
    g.drawFittedText("Samples start where each original hit really begins. Original: the gated track under the samples (-inf = full replace). "
                     "Offset nudges timing; negative values use the gate's Lookahead.",
                     right.reduced(14, 6).removeFromBottom(30), juce::Justification::bottomLeft, 2);
}
