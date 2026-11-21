#include "GateEditor.h"

using namespace GateColours;

// =========================== LookAndFeel ===========================

GateLookAndFeel::GateLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, text);
    setColour(juce::ComboBox::backgroundColourId, panelEdge);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, panelEdge.brighter(0.15f));
    setColour(juce::ComboBox::arrowColourId, dimText);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::ToggleButton::textColourId, text);
}

void GateLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float track = 3.5f;
    const auto colour = s.findColour(juce::Slider::rotarySliderFillColourId);

    // knob body
    const float bodyR = radius - 7.0f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2e333c), centre.x, centre.y - bodyR,
                                           juce::Colour(0xff1a1d22), centre.x, centre.y + bodyR, false));
    g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2, bodyR * 2);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2, bodyR * 2, 1.0f);

    // track + value arc
    juce::Path bg, val;
    bg.addCentredArc(centre.x, centre.y, radius - 2, radius - 2, 0, startAngle, endAngle, true);
    val.addCentredArc(centre.x, centre.y, radius - 2, radius - 2, 0, startAngle, angle, true);
    g.setColour(panelEdge.brighter(0.1f));
    g.strokePath(bg, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour);
    g.strokePath(val, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    juce::Path p;
    p.addRoundedRectangle(-1.5f, -bodyR + 3.0f, 3.0f, bodyR * 0.45f, 1.5f);
    g.setColour(text);
    g.fillPath(p, juce::AffineTransform::rotation(angle).translated(centre));
}

void GateLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    const auto onColour = b.findColour(juce::ToggleButton::tickColourId);
    g.setColour(on ? onColour.withAlpha(0.22f) : panelEdge);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(on ? onColour : (highlighted ? dimText : panelEdge.brighter(0.2f)));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);

    auto led = r.removeFromLeft(r.getHeight()).reduced(r.getHeight() * 0.33f);
    g.setColour(on ? onColour : juce::Colour(0xff444a54));
    g.fillEllipse(led);

    g.setColour(on ? juce::Colours::white : text);
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(b.getButtonText(), r.withTrimmedRight(6.0f), juce::Justification::centred);
}

void GateLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int>(0, 0, w, h).toFloat().reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = float(w) - 14.0f, ay = float(h) * 0.5f;
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(dimText);
    g.fillPath(arrow);
}

juce::Label* GateLookAndFeel::createSliderTextBox(juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(juce::FontOptions(12.5f));
    l->setColour(juce::Label::outlineWhenEditingColourId, accent);
    l->setColour(juce::Label::backgroundWhenEditingColourId, panelEdge);
    return l;
}

// =========================== Knob ===========================

GateKnob::GateKnob(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour(juce::Slider::textBoxOutlineColourId, panelEdge);
    slider.setPopupDisplayEnabled(false, false, nullptr);
    addAndMakeVisible(slider);

    label.setText(title, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, dimText);
    addAndMakeVisible(label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramId, slider);

    // Show units in the value box ("-24.0 dB")
    if (auto* p = state.getParameter(paramId))
    {
        const auto unit = p->getLabel();
        slider.textFromValueFunction = [p, unit](double v)
        {
            auto t = p->getText(p->convertTo0to1(float(v)), 8);
            return unit.isEmpty() ? t : t + " " + unit;
        };
        slider.updateText();
    }
}

void GateKnob::resized()
{
    auto r = getLocalBounds();
    label.setBounds(r.removeFromTop(16));
    slider.setBounds(r);
}

// =========================== Display ===========================

GateDisplay::GateDisplay(GHSDrumGateProcessor& p) : proc(p)
{
    history.assign(2048, { 0.0f, -120.0f, 1.0f, false, false });
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    startTimerHz(60);
}

GateDisplay::~GateDisplay() { stopTimer(); }

juce::Rectangle<float> GateDisplay::meterArea() const
{
    return getLocalBounds().toFloat().removeFromRight(46.0f).reduced(8.0f, 10.0f);
}

juce::Rectangle<float> GateDisplay::plotArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromRight(46.0f);
    return r.reduced(8.0f, 10.0f).withTrimmedLeft(26.0f);
}

float GateDisplay::dbToY(float db, juce::Rectangle<float> a) const
{
    const float t = juce::jlimit(0.0f, 1.0f, (db - minDb) / (0.0f - minDb));
    return a.getBottom() - t * a.getHeight();
}

float GateDisplay::yToDb(float y, juce::Rectangle<float> a) const
{
    const float t = juce::jlimit(0.0f, 1.0f, (a.getBottom() - y) / a.getHeight());
    return minDb + t * (0.0f - minDb);
}

void GateDisplay::timerCallback()
{
    const int got = proc.popDisplayFrames(scratch.data(), int(scratch.size()));
    for (int i = 0; i < got; ++i)
    {
        history[size_t(head)] = scratch[size_t(i)];
        head = (head + 1) % int(history.size());
    }

    // meter ballistics: instant down, ~300 ms back up
    const float target = proc.meterGainDb.load();
    meterDb = target < meterDb ? target : meterDb + (target - meterDb) * 0.12f;
    repaint();
}

void GateDisplay::resized() {}

void GateDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0c0e11));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    const auto a = plotArea();
    auto& params = proc.apvts;
    const float thr = params.getRawParameterValue(GateParamIDs::threshold)->load();
    const float hyst = params.getRawParameterValue(GateParamIDs::hysteresis)->load();
    const bool duck = params.getRawParameterValue(GateParamIDs::mode)->load() > 0.5f;

    // grid
    g.setFont(juce::FontOptions(10.0f));
    for (float db = 0.0f; db >= minDb; db -= 12.0f)
    {
        const float y = dbToY(db, a);
        g.setColour(panelEdge.withAlpha(0.7f));
        g.drawHorizontalLine(int(y), a.getX(), a.getRight());
        g.setColour(dimText.withAlpha(0.8f));
        g.drawText(juce::String(int(db)), juce::Rectangle<float>(a.getX() - 28.0f, y - 6.0f, 24.0f, 12.0f),
                   juce::Justification::centredRight);
    }

    // level history: newest at the right edge
    const int columns = int(a.getWidth());
    const int size = int(history.size());
    juce::Path keyPath;
    bool keyStarted = false;

    for (int c = 0; c < columns; ++c)
    {
        const int idx = (head - columns + c + size * 4) % size;
        const auto& f = history[size_t(idx)];
        const float x = a.getX() + float(c);

        const float inDb = GHSGate::gainToDb(f.inputPeak);
        const float outDb = GHSGate::gainToDb(f.inputPeak * f.minGain);
        const float yIn = dbToY(inDb, a), yOut = dbToY(outDb, a);

        if (inDb > minDb)
        {
            g.setColour(closed);
            g.drawVerticalLine(int(x), yIn, a.getBottom());
        }
        if (outDb > minDb)
        {
            g.setColour(accent.withAlpha(0.85f));
            g.drawVerticalLine(int(x), yOut, a.getBottom());
        }
        if (f.hit)
        {
            g.setColour(open);
            g.fillRect(x - 1.0f, a.getY(), 2.0f, 6.0f);
        }
        if (f.rejected)
        {
            g.setColour(reject);
            g.fillRect(x - 1.0f, a.getY(), 2.0f, 6.0f);
        }

        const float yKey = dbToY(f.keyDb, a);
        if (! keyStarted) { keyPath.startNewSubPath(x, yKey); keyStarted = true; }
        else keyPath.lineTo(x, yKey);
    }
    g.setColour(key.withAlpha(0.9f));
    g.strokePath(keyPath, juce::PathStrokeType(1.2f));

    // thresholds
    const float yOpen = dbToY(thr, a), yClose = dbToY(thr - hyst, a);
    g.setColour(accent.withAlpha(0.08f));
    g.fillRect(juce::Rectangle<float>(a.getX(), yOpen, a.getWidth(), std::max(0.0f, yClose - yOpen)));
    g.setColour(accent);
    g.drawLine(a.getX(), yOpen, a.getRight(), yOpen, dragging ? 2.0f : 1.5f);
    if (hyst > 0.05f)
    {
        const float dashes[] = { 4.0f, 4.0f };
        g.setColour(accent.withAlpha(0.6f));
        g.drawDashedLine(juce::Line<float>(a.getX(), yClose, a.getRight(), yClose), dashes, 2, 1.0f);
    }
    g.setColour(accent);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("OPEN " + juce::String(thr, 1) + " dB", juce::Rectangle<float>(a.getRight() - 120.0f, yOpen - 15.0f, 116.0f, 13.0f),
               juce::Justification::centredRight);
    if (hyst > 0.05f)
    {
        g.setColour(accent.withAlpha(0.6f));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText("CLOSE " + juce::String(thr - hyst, 1), juce::Rectangle<float>(a.getRight() - 120.0f, yClose + 2.0f, 116.0f, 12.0f),
                   juce::Justification::centredRight);
    }

    // legend
    g.setFont(juce::FontOptions(10.5f));
    auto legend = juce::Rectangle<float>(a.getX() + 6.0f, a.getY() + 8.0f, 430.0f, 13.0f);
    g.setColour(juce::Colour(0xff0c0e11).withAlpha(0.85f));
    g.fillRoundedRectangle(legend.expanded(4.0f, 2.0f), 3.0f);
    auto swatch = [&](juce::Colour c, const juce::String& t)
    {
        g.setColour(c); g.fillRect(legend.removeFromLeft(9.0f).reduced(0, 2));
        legend.removeFromLeft(4.0f);
        g.setColour(dimText); g.drawText(t, legend.removeFromLeft(72.0f), juce::Justification::centredLeft);
    };
    swatch(accent, duck ? "Ducked out" : "Passed");
    swatch(closed, "Removed");
    swatch(key, "Key level");
    swatch(open, "Hit");
    swatch(reject, "Rejected");

    // gain meter
    const auto m = meterArea();
    g.setColour(juce::Colour(0xff1a1d22));
    g.fillRoundedRectangle(m, 3.0f);
    const float meterRange = 60.0f;
    const float frac = juce::jlimit(0.0f, 1.0f, -meterDb / meterRange);
    auto fill = m.withHeight(m.getHeight() * frac);
    g.setColour(accent);
    g.fillRoundedRectangle(fill, 3.0f);
    g.setColour(dimText);
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("GAIN", m.withY(m.getBottom() - 14.0f).withHeight(12.0f), juce::Justification::centred);

    const bool isOpen = proc.meterOpen.load();
    const bool showOpen = duck ? ! isOpen : isOpen;
    auto ledR = juce::Rectangle<float>(m.getCentreX() - 5.0f, m.getBottom() - 30.0f, 10.0f, 10.0f);
    g.setColour(showOpen ? open : juce::Colour(0xff3a3f48));
    g.fillEllipse(ledR);
}

void GateDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (auto* p = proc.apvts.getParameter(GateParamIDs::threshold))
    {
        dragging = true;
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(yToDb(float(e.y), plotArea())));
    }
}

void GateDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (auto* p = proc.apvts.getParameter(GateParamIDs::threshold))
        p->setValueNotifyingHost(p->convertTo0to1(yToDb(float(e.y), plotArea())));
}

void GateDisplay::mouseUp(const juce::MouseEvent&)
{
    if (auto* p = proc.apvts.getParameter(GateParamIDs::threshold))
        p->endChangeGesture();
    dragging = false;
}

// =========================== Editor ===========================

namespace
{
juce::String noteName(double v) { return juce::MidiMessage::getMidiNoteName(juce::roundToInt(v), true, true, 3) + " (" + juce::String(juce::roundToInt(v)) + ")"; }
}

GHSDrumGateEditor::GHSDrumGateEditor(GHSDrumGateProcessor& p)
    : AudioProcessorEditor(&p), proc(p), display(p),
      threshold(p.apvts, GateParamIDs::threshold, "THRESHOLD"),
      hysteresis(p.apvts, GateParamIDs::hysteresis, "HYSTERESIS"),
      sensitivity(p.apvts, GateParamIDs::sens, "TRANSIENT"),
      retrigger(p.apvts, GateParamIDs::retrigger, "RETRIGGER"),
      matchTol(p.apvts, GateParamIDs::matchTol, "TOLERANCE"),
      hpf(p.apvts, GateParamIDs::keyHpf, "KEY HPF"),
      lpf(p.apvts, GateParamIDs::keyLpf, "KEY LPF"),
      lookahead(p.apvts, GateParamIDs::lookahead, "LOOKAHEAD"),
      midiInNote(p.apvts, GateParamIDs::midiInNote, "TRIG NOTE"),
      attack(p.apvts, GateParamIDs::attack, "ATTACK"),
      hold(p.apvts, GateParamIDs::hold, "HOLD"),
      release(p.apvts, GateParamIDs::release, "RELEASE"),
      adaptive(p.apvts, GateParamIDs::adaptive, "ADAPTIVE"),
      punch(p.apvts, GateParamIDs::punch, "PUNCH"),
      range(p.apvts, GateParamIDs::range, "RANGE / LOW"),
      rangeHigh(p.apvts, GateParamIDs::rangeHigh, "HIGH RANGE"),
      crossover(p.apvts, GateParamIDs::crossover, "CROSSOVER"),
      level(p.apvts, GateParamIDs::level, "LEVELLER"),
      levelTarget(p.apvts, GateParamIDs::levelTarget, "TARGET"),
      clip(p.apvts, GateParamIDs::clip, "HIT CLIP"),
      mix(p.apvts, GateParamIDs::mix, "MIX"),
      output(p.apvts, GateParamIDs::output, "OUTPUT"),
      midiNote(p.apvts, GateParamIDs::midiNote, "NOTE"),
      midiVel(p.apvts, GateParamIDs::midiVelSens, "VEL SENS")
{
    setLookAndFeel(&lnf);

    addAndMakeVisible(display);

    for (auto* k : { &hpf, &lpf, &lookahead, &midiInNote, &matchTol })
        k->slider.setColour(juce::Slider::rotarySliderFillColourId, key);
    for (auto* k : { &level, &levelTarget, &clip })
        k->slider.setColour(juce::Slider::rotarySliderFillColourId, dynamics);
    for (auto* k : { &midiNote, &midiVel })
        k->slider.setColour(juce::Slider::rotarySliderFillColourId, open);

    midiNote.slider.textFromValueFunction = noteName;
    midiNote.slider.updateText();

    for (auto* k : { &threshold, &hysteresis, &sensitivity, &retrigger, &matchTol, &hpf, &lpf, &lookahead, &midiInNote,
                     &attack, &hold, &release, &adaptive, &punch, &range, &rangeHigh, &crossover,
                     &level, &levelTarget, &clip, &mix, &output, &midiNote, &midiVel })
        addAndMakeVisible(*k);

    // presets
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem(proc.getProgramName(i), i + 1);
    presetBox.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram(presetBox.getSelectedId() - 1); };
    addAndMakeVisible(presetBox);

    addChoice(modeBox, modeAtt, GateParamIDs::mode);
    addChoice(detectBox, detectAtt, GateParamIDs::detect);
    addChoice(keySourceBox, keySourceAtt, GateParamIDs::keySource);

    keyListenButton.setColour(juce::ToggleButton::tickColourId, key);
    midiButton.setColour(juce::ToggleButton::tickColourId, open);
    addAndMakeVisible(keyListenButton);
    addAndMakeVisible(midiButton);
    keyListenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, GateParamIDs::keyListen, keyListenButton);
    midiAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, GateParamIDs::midiOn, midiButton);

    splitButton.setColour(juce::ToggleButton::tickColourId, accent);
    matchButton.setColour(juce::ToggleButton::tickColourId, key);
    learnHitsButton.setColour(juce::ToggleButton::tickColourId, open);
    learnSpillButton.setColour(juce::ToggleButton::tickColourId, reject);
    for (auto* b : { &splitButton, &matchButton, &learnHitsButton, &learnSpillButton })
        addAndMakeVisible(*b);
    splitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, GateParamIDs::bandSplit, splitButton);
    matchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, GateParamIDs::hitMatch, matchButton);

    learnHitsButton.onClick = [this] { setLearnMode(learnHitsButton.getToggleState() ? 1 : 0); };
    learnSpillButton.onClick = [this] { setLearnMode(learnSpillButton.getToggleState() ? 2 : 0); };
    learnHitsButton.setTooltip("Play (or loop) a section with only the drum you want. Every hit it hears becomes the reference sound.");
    learnSpillButton.setTooltip("Play a section with only the bleed you want gone (e.g. kick spill on the snare mic).");
    clearButton.setColour(juce::TextButton::buttonColourId, panelEdge);
    clearButton.setColour(juce::TextButton::textColourOffId, text);
    clearButton.onClick = [this] { setLearnMode(0); proc.clearRequest = 3; };
    addAndMakeVisible(clearButton);

    matchStatus.setFont(juce::FontOptions(11.5f));
    matchStatus.setJustificationType(juce::Justification::centredLeft);
    matchStatus.setColour(juce::Label::textColourId, dimText);
    addAndMakeVisible(matchStatus);

    scStatus.setFont(juce::FontOptions(11.0f));
    scStatus.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(scStatus);

    setResizable(true, true);
    setResizeLimits(1000, 700, 1700, 1100);
    setSize(1100, 780);
    startTimerHz(10);
}

GHSDrumGateEditor::~GHSDrumGateEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void GHSDrumGateEditor::setLearnMode(int mode)
{
    // Starting a learn pass starts that profile fresh
    if (mode == 1) proc.clearRequest = proc.clearRequest.load() | 1;
    if (mode == 2) proc.clearRequest = proc.clearRequest.load() | 2;
    proc.learnMode = mode;
    learnHitsButton.setToggleState(mode == 1, juce::dontSendNotification);
    learnSpillButton.setToggleState(mode == 2, juce::dontSendNotification);
}

void GHSDrumGateEditor::addChoice(juce::ComboBox& box, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att,
                                  const char* paramId)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(paramId)))
        box.addItemList(choice->choices, 1);
    addAndMakeVisible(box);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, paramId, box);
}

void GHSDrumGateEditor::timerCallback()
{
    const int source = juce::roundToInt(proc.apvts.getRawParameterValue(GateParamIDs::keySource)->load());
    const bool wantSc = source == 1;
    midiInNote.setAlpha(source == 2 ? 1.0f : 0.4f);

    const bool split = splitButton.getToggleState();
    rangeHigh.setAlpha(split ? 1.0f : 0.4f);
    crossover.setAlpha(split ? 1.0f : 0.4f);

    // Hit Match status
    {
        const int hits = proc.learnedHitCount.load(), spill = proc.learnedSpillCount.load();
        const int mode = proc.learnMode.load();
        juce::String t;
        if (mode == 1)      t = "Learning hits... " + juce::String(hits) + " so far";
        else if (mode == 2) t = "Learning spill... " + juce::String(spill) + " so far";
        else if (hits == 0 && spill == 0) t = "Nothing learned yet";
        else
        {
            t = "Hits: " + juce::String(hits) + "   Spill: " + juce::String(spill);
            const float d = proc.lastMatchDistance.load();
            if (d >= 0.0f) t << "   Last: " << juce::String(d, 1) << " dB " << (proc.lastHitRejected.load() ? "(rejected)" : "(kept)");
        }
        matchStatus.setText(t, juce::dontSendNotification);
        matchStatus.setColour(juce::Label::textColourId, mode == 1 ? open : mode == 2 ? reject : dimText);
        matchTol.setAlpha(matchButton.getToggleState() ? 1.0f : 0.4f);
        if (source == 2 && matchButton.getToggleState())
            matchStatus.setText("Not used with MIDI triggering", juce::dontSendNotification);
    }

    const bool haveSc = proc.sidechainConnected.load();
    if (! wantSc)
        scStatus.setText({}, juce::dontSendNotification);
    else if (haveSc)
    {
        scStatus.setText("Sidechain active", juce::dontSendNotification);
        scStatus.setColour(juce::Label::textColourId, open);
    }
    else
    {
        scStatus.setText("No sidechain routed - using input", juce::dontSendNotification);
        scStatus.setColour(juce::Label::textColourId, accent);
    }

    // The transient knob only does anything in Transient mode
    const bool transient = juce::roundToInt(proc.apvts.getRawParameterValue(GateParamIDs::detect)->load()) == 2;
    sensitivity.setAlpha(transient ? 1.0f : 0.4f);

    // Punch only applies in Gate mode
    const bool duck = proc.apvts.getRawParameterValue(GateParamIDs::mode)->load() > 0.5f;
    for (auto* k : { &punch, &level, &levelTarget, &clip })
        k->setAlpha(duck ? 0.4f : 1.0f);
    levelTarget.setAlpha(duck || proc.apvts.getRawParameterValue(GateParamIDs::level)->load() < 0.5f ? 0.4f : 1.0f);

    const bool midiOn = midiButton.getToggleState();
    midiNote.setAlpha(midiOn ? 1.0f : 0.4f);
    midiVel.setAlpha(midiOn ? 1.0f : 0.4f);

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

void GHSDrumGateEditor::drawSection(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour(panel);
    g.fillRoundedRectangle(r.toFloat(), 6.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 1.0f);
    g.setColour(dimText);
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.drawText(title, r.reduced(12, 0).removeFromTop(26), juce::Justification::centredLeft);
}

void GHSDrumGateEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);

    auto header = getLocalBounds().removeFromTop(48).reduced(16, 0);
    g.setColour(text);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("GHS", header.removeFromLeft(48), juce::Justification::centredLeft);
    g.setColour(accent);
    g.drawText("DRUM GATE", header.removeFromLeft(130), juce::Justification::centredLeft);

    drawSection(g, detectSection, "DETECTION");
    drawSection(g, matchSection, "HIT MATCH");
    drawSection(g, keySection, "KEY");
    drawSection(g, envSection, "ENVELOPE");
    drawSection(g, bandSection, "BANDS");
    drawSection(g, dynSection, "HIT DYNAMICS");
    drawSection(g, outSection, "OUTPUT  /  MIDI");
}

void GHSDrumGateEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop(48).reduced(16, 10);
    header.removeFromLeft(190);
    presetBox.setBounds(header.removeFromLeft(230));
    modeBox.setBounds(header.removeFromRight(100));

    r.reduce(16, 0);
    r.removeFromBottom(14);

    const int gap = 10;
    const int sectionH = 146;
    auto rows = r.removeFromBottom(sectionH * 3 + gap * 2);
    display.setBounds(r.reduced(0, 4));

    auto row1 = rows.removeFromTop(sectionH); rows.removeFromTop(gap);
    auto row2 = rows.removeFromTop(sectionH); rows.removeFromTop(gap);
    auto row3 = rows;

    // split a row into sections proportional to how many knob-widths each needs
    auto split = [gap](juce::Rectangle<int> row, std::initializer_list<std::pair<juce::Rectangle<int>*, int>> parts)
    {
        int units = 0;
        for (auto& p : parts) units += p.second;
        const int avail = row.getWidth() - gap * (int(parts.size()) - 1);
        int i = 0;
        for (auto& p : parts)
        {
            const bool last = ++i == int(parts.size());
            *p.first = last ? row : row.removeFromLeft(avail * p.second / units);
            if (! last) row.removeFromLeft(gap);
        }
    };
    split(row1, { { &detectSection, 4 }, { &matchSection, 3 }, { &keySection, 4 } });
    split(row2, { { &envSection, 5 }, { &bandSection, 3 } });
    split(row3, { { &dynSection, 3 }, { &outSection, 4 } });

    auto knobArea = [](juce::Rectangle<int> s) { return s.withTrimmedTop(26).reduced(6, 4); };
    auto layoutKnobs = [&](juce::Rectangle<int> area, std::initializer_list<GateKnob*> knobs)
    {
        const int w = area.getWidth() / int(knobs.size());
        for (auto* k : knobs)
            k->setBounds(area.removeFromLeft(w).reduced(2, 0));
    };
    auto titleBar = [](juce::Rectangle<int> s) { return s.removeFromTop(26).reduced(8, 3); };

    { auto t = titleBar(detectSection); detectBox.setBounds(t.removeFromRight(110)); }
    {
        auto t = titleBar(keySection);
        keySourceBox.setBounds(t.removeFromRight(100));
        t.removeFromRight(6);
        keyListenButton.setBounds(t.removeFromRight(96));
        t.removeFromLeft(36);
        scStatus.setBounds(t);
    }
    { auto t = titleBar(matchSection); matchButton.setBounds(t.removeFromRight(80)); }
    { auto t = titleBar(bandSection); splitButton.setBounds(t.removeFromRight(100)); }
    { auto t = titleBar(outSection); midiButton.setBounds(t.removeFromRight(96)); }

    layoutKnobs(knobArea(detectSection), { &threshold, &hysteresis, &sensitivity, &retrigger });
    layoutKnobs(knobArea(keySection), { &hpf, &lpf, &lookahead, &midiInNote });
    layoutKnobs(knobArea(envSection), { &attack, &hold, &release, &adaptive, &punch });
    layoutKnobs(knobArea(bandSection), { &range, &rangeHigh, &crossover });
    layoutKnobs(knobArea(dynSection), { &level, &levelTarget, &clip });
    layoutKnobs(knobArea(outSection), { &mix, &output, &midiNote, &midiVel });

    // Hit Match: tolerance knob + learn buttons + status
    {
        auto a = knobArea(matchSection);
        matchTol.setBounds(a.removeFromLeft(a.getWidth() / 3).reduced(2, 0));
        a.removeFromLeft(8);
        auto status = a.removeFromBottom(22);
        matchStatus.setBounds(status);
        a.removeFromTop(10);
        const int bh = 26;
        learnHitsButton.setBounds(a.removeFromTop(bh).reduced(0, 1));
        a.removeFromTop(6);
        auto rowB = a.removeFromTop(bh);
        learnSpillButton.setBounds(rowB.removeFromLeft(rowB.getWidth() * 2 / 3).reduced(0, 1));
        rowB.removeFromLeft(6);
        clearButton.setBounds(rowB.reduced(0, 1));
    }
}
