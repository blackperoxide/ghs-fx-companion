#include "GateEditor.h"

using namespace GateColours;
using GHSAlign::Registry;

AlignPanel::AlignPanel(GHSDrumGateProcessor& p) : proc(p)
{
    nameEditor.setText(proc.getUserAlignName(), juce::dontSendNotification);
    nameEditor.setTextToShowWhenEmpty(proc.getAlignName(), dimText);
    nameEditor.setFont(juce::FontOptions(14.0f));
    nameEditor.setColour(juce::TextEditor::backgroundColourId, panelEdge);
    nameEditor.setColour(juce::TextEditor::textColourId, text);
    nameEditor.setColour(juce::TextEditor::outlineColourId, panelEdge.brighter(0.15f));
    nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, accent);
    nameEditor.onTextChange = [this] { proc.setUserAlignName(nameEditor.getText()); };
    addAndMakeVisible(nameEditor);

    alignButton.setColour(juce::ToggleButton::tickColourId, open);
    refButton.setColour(juce::ToggleButton::tickColourId, accent);
    alignOnlyButton.setColour(juce::ToggleButton::tickColourId, key);
    for (auto* b : { &alignButton, &refButton, &alignOnlyButton }) addAndMakeVisible(*b);
    alignAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, GateParamIDs::alignOn, alignButton);
    refAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, GateParamIDs::alignRef, refButton);

    // "Align only" is Key Source = Align Only (no gating) - a shortcut for overheads / rooms
    alignOnlyButton.onClick = [this]
    {
        if (auto* ks = proc.apvts.getParameter(GateParamIDs::keySource))
            ks->setValueNotifyingHost(ks->convertTo0to1(alignOnlyButton.getToggleState() ? 3.0f : 0.0f));
    };

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(GateParamIDs::alignGroup)))
        for (int i = 0; i < choice->choices.size(); ++i)
            groupBox.addItem("Group " + choice->choices[i], i + 1);
    addAndMakeVisible(groupBox);
    groupAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, GateParamIDs::alignGroup, groupBox);

    modeBox.addItem("Time + Polarity", 1);
    modeBox.addItem("Time + Phase", 2);
    modeBox.onChange = [this]
    {
        if (auto* ph = proc.apvts.getParameter(GateParamIDs::alignPhase))
            ph->setValueNotifyingHost(modeBox.getSelectedId() == 2 ? 1.0f : 0.0f);
    };
    addAndMakeVisible(modeBox);

    analyseButton.setColour(juce::TextButton::buttonColourId, accent.withAlpha(0.85f));
    analyseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    analyseButton.onClick = [this]
    {
        const int group = proc.getAlignGroup();
        if (proc.getAlignRegistry().getStatus(group).phase == Registry::Phase::Capturing)
            proc.getAlignRegistry().cancel(group);
        else
            proc.getAlignRegistry().startAnalysis(group, proc.apvts.getRawParameterValue(GateParamIDs::alignPhase)->load() > 0.5f);
        refresh();
    };
    resetButton.setColour(juce::TextButton::buttonColourId, panelEdge);
    resetButton.setColour(juce::TextButton::textColourOffId, text);
    resetButton.onClick = [this] { proc.getAlignRegistry().resetGroup(proc.getAlignGroup()); refresh(); };
    addAndMakeVisible(analyseButton);
    addAndMakeVisible(resetButton);

    refresh();
    startTimerHz(8);
}

AlignPanel::~AlignPanel() { stopTimer(); }

void AlignPanel::timerCallback() { if (isVisible()) refresh(); }

void AlignPanel::refresh()
{
    const int group = proc.getAlignGroup();
    status = proc.getAlignRegistry().getStatus(group);
    rows = proc.getAlignRegistry().listMembers(group);

    alignOnlyButton.setToggleState(juce::roundToInt(proc.apvts.getRawParameterValue(GateParamIDs::keySource)->load()) == 3,
                                   juce::dontSendNotification);
    modeBox.setSelectedId(proc.apvts.getRawParameterValue(GateParamIDs::alignPhase)->load() > 0.5f ? 2 : 1, juce::dontSendNotification);
    if (! nameEditor.hasKeyboardFocus(true))
        nameEditor.setTextToShowWhenEmpty(proc.getAlignName(), dimText);

    const bool capturing = status.phase == Registry::Phase::Capturing;
    analyseButton.setButtonText(capturing ? "Cancel" : status.phase == Registry::Phase::Analysing ? "Analysing..." : "Analyse");
    analyseButton.setEnabled(status.phase != Registry::Phase::Analysing);
    repaint();
}

void AlignPanel::resized()
{
    auto r = getLocalBounds();
    leftArea = r.removeFromLeft(330);
    r.removeFromLeft(10);
    rightArea = r;

    // left: this track
    auto l = leftArea.reduced(14, 0).withTrimmedTop(34);
    l.removeFromTop(18); // "Name in the kit" label
    nameEditor.setBounds(l.removeFromTop(30));
    l.removeFromTop(12);
    auto row = l.removeFromTop(28);
    alignButton.setBounds(row.removeFromLeft(row.getWidth() / 2 - 4));
    row.removeFromLeft(8);
    refButton.setBounds(row);
    l.removeFromTop(8);
    alignOnlyButton.setBounds(l.removeFromTop(28));
    l.removeFromTop(8);
    groupBox.setBounds(l.removeFromTop(28));

    // right: group controls + table
    auto rr = rightArea.reduced(14, 0).withTrimmedTop(34);
    auto controls = rr.removeFromTop(30);
    analyseButton.setBounds(controls.removeFromLeft(120));
    controls.removeFromLeft(8);
    resetButton.setBounds(controls.removeFromLeft(80));
    controls.removeFromLeft(12);
    modeBox.setBounds(controls.removeFromLeft(160));
    rr.removeFromTop(8);
    statusArea = rr.removeFromTop(22);
    rr.removeFromTop(6);
    helpArea = rr.removeFromBottom(54);
    tableArea = rr;
}

void AlignPanel::paint(juce::Graphics& g)
{
    auto panelBox = [&](juce::Rectangle<int> b, const juce::String& title)
    {
        g.setColour(panel);
        g.fillRoundedRectangle(b.toFloat(), 6.0f);
        g.setColour(panelEdge);
        g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 6.0f, 1.0f);
        g.setColour(dimText);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(title, b.reduced(12, 0).removeFromTop(26), juce::Justification::centredLeft);
    };
    panelBox(leftArea, "THIS TRACK");
    panelBox(rightArea, "KIT ALIGNMENT  -  GROUP " + juce::String::charToString(juce::juce_wchar('A' + proc.getAlignGroup())));

    // left: labels + this track's result
    {
        auto l = leftArea.reduced(14, 0).withTrimmedTop(34);
        g.setColour(dimText);
        g.setFont(juce::FontOptions(11.5f));
        g.drawText("Name in the kit", l.removeFromTop(16), juce::Justification::centredLeft);

        auto res = leftArea.reduced(14, 12).withTrimmedTop(240);
        const auto al = proc.getAlignment();
        const bool on = proc.apvts.getRawParameterValue(GateParamIDs::alignOn)->load() > 0.5f;
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.setColour(dimText);
        g.drawText("THIS TRACK'S CORRECTION", res.removeFromTop(18), juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        if (! al.valid)
        {
            g.setColour(dimText);
            g.drawText("Not analysed yet", res.removeFromTop(30), juce::Justification::centredLeft);
        }
        else
        {
            const double ms = -al.offsetSeconds * 1000.0; // what we do to this track
            g.setColour(on ? open : dimText);
            g.drawText((std::abs(ms) < 0.005 ? juce::String("0.00 ms") : (ms > 0 ? "+" : "") + juce::String(ms, 2) + " ms"),
                       res.removeFromTop(30), juce::Justification::centredLeft);
            g.setFont(juce::FontOptions(13.0f));
            g.setColour(text);
            const double deg = al.phaseRad * 180.0 / M_PI;
            juce::String ph = al.usePhase ? "Phase rotated " + juce::String(juce::roundToInt(deg)) + juce::String(juce::CharPointer_UTF8("\xc2\xb0"))
                                          : (std::cos(al.phaseRad) < 0 ? "Polarity flipped" : "Polarity normal");
            g.drawText(ph, res.removeFromTop(20), juce::Justification::centredLeft);
            g.setColour(dimText);
            g.setFont(juce::FontOptions(11.5f));
            g.drawText(on ? (ms >= 0 ? "Delayed to meet the reference" : "Moved earlier to meet the reference")
                          : "Align is off - not applied", res.removeFromTop(18), juce::Justification::centredLeft);
        }
    }

    // status line + progress
    {
        auto st = statusArea;
        juce::Colour c = dimText;
        switch (status.phase)
        {
            case Registry::Phase::Capturing: c = accent; break;
            case Registry::Phase::Analysing: c = key; break;
            case Registry::Phase::Done:      c = open; break;
            case Registry::Phase::Error:     c = reject; break;
            case Registry::Phase::Idle:      break;
        }
        juce::String msg = status.message;
        if (msg.isEmpty())
            msg = rows.size() < 2 ? "Waiting for other instances in this group..."
                                  : juce::String(int(rows.size())) + " tracks in this group - press Analyse, then play a busy part of the song";
        if (status.phase == Registry::Phase::Capturing)
        {
            auto bar = st.removeFromRight(160).reduced(0, 7).toFloat();
            g.setColour(panelEdge);
            g.fillRoundedRectangle(bar, 3.0f);
            g.setColour(accent);
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, status.progress)), 3.0f);
            st.removeFromRight(10);
        }
        g.setColour(c);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(msg, st, juce::Justification::centredLeft, true);
    }

    // table
    {
        auto t = tableArea;
        const int cols[] = { 0, 200, 300, 400, 530 }; // name, role, offset, phase, match
        auto header = t.removeFromTop(22);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.setColour(dimText);
        const char* titles[] = { "TRACK", "ROLE", "OFFSET", "POLARITY / PHASE", "MATCH  (BEFORE > AFTER)" };
        for (int i = 0; i < 5; ++i)
            g.drawText(titles[i], header.withTrimmedLeft(cols[i] + 6).withWidth(i < 4 ? cols[i + 1] - cols[i] : header.getWidth() - cols[i]),
                       juce::Justification::centredLeft);
        g.setColour(panelEdge);
        g.drawHorizontalLine(t.getY(), float(t.getX()), float(t.getRight()));

        const int rowH = 26;
        for (size_t i = 0; i < rows.size() && t.getHeight() >= rowH; ++i)
        {
            const auto& r = rows[i];
            auto row = t.removeFromTop(rowH);
            const bool self = r.memberId == proc.alignMemberId;
            if (self)
            {
                g.setColour(accent.withAlpha(0.07f));
                g.fillRect(row);
            }
            auto cell = [&](int c) { return row.withTrimmedLeft(cols[c] + 6).withWidth(c < 4 ? cols[c + 1] - cols[c] - 6 : row.getWidth() - cols[c] - 12); };

            g.setFont(juce::FontOptions(13.0f, self ? juce::Font::bold : juce::Font::plain));
            g.setColour(text);
            g.drawText(r.name + (self ? "  (this)" : ""), cell(0), juce::Justification::centredLeft, true);

            g.setFont(juce::FontOptions(12.0f));
            g.setColour(r.isReference ? accent : dimText);
            g.drawText(r.isReference ? "Reference" : "Aligned", cell(1), juce::Justification::centredLeft);

            if (! r.analysed)
            {
                g.setColour(dimText);
                g.drawText("-", cell(2), juce::Justification::centredLeft);
                g.drawText("-", cell(3), juce::Justification::centredLeft);
            }
            else if (r.outOfRange)
            {
                g.setColour(reject);
                g.drawText("Out of range (> 10 ms) - left as is", row.withTrimmedLeft(cols[2] + 6), juce::Justification::centredLeft);
            }
            else
            {
                const double ms = -r.lagMs;
                g.setColour(text);
                g.drawText(r.isReference ? "0.00 ms" : (ms > 0 ? "+" : "") + juce::String(ms, 2) + " ms", cell(2), juce::Justification::centredLeft);
                juce::String ph;
                const bool usePh = proc.apvts.getRawParameterValue(GateParamIDs::alignPhase)->load() > 0.5f;
                if (r.isReference) ph = "-";
                else if (std::abs(std::abs(r.phaseDeg) - 180.0) < 0.5) ph = "Flipped";
                else if (std::abs(r.phaseDeg) < 0.5) ph = "Normal";
                else ph = juce::String(juce::roundToInt(r.phaseDeg)) + juce::String(juce::CharPointer_UTF8("\xc2\xb0")) + (usePh ? "" : "");
                g.setColour(ph == "Flipped" ? key : text);
                g.drawText(ph, cell(3), juce::Justification::centredLeft);

                if (! r.isReference)
                {
                    // before -> after bars (0..1 correlation)
                    auto m = cell(4).reduced(0, 8);
                    const int bw = std::min(110, (m.getWidth() - 70) / 2);
                    auto drawBar = [&](juce::Rectangle<int> b, double v, juce::Colour col)
                    {
                        g.setColour(panelEdge);
                        g.fillRoundedRectangle(b.toFloat(), 2.0f);
                        g.setColour(col);
                        g.fillRoundedRectangle(b.toFloat().withWidth(float(b.getWidth()) * float(juce::jlimit(0.0, 1.0, std::abs(v)))), 2.0f);
                    };
                    drawBar(m.removeFromLeft(bw), r.corrBefore, closed.brighter(0.4f));
                    m.removeFromLeft(6);
                    drawBar(m.removeFromLeft(bw), r.corrAfter, open);
                    m.removeFromLeft(8);
                    g.setColour(dimText);
                    g.setFont(juce::FontOptions(11.0f));
                    g.drawText(juce::String(juce::roundToInt(std::abs(r.corrBefore) * 100)) + " > " + juce::String(juce::roundToInt(std::abs(r.corrAfter) * 100)) + "%",
                               m.expanded(0, 8), juce::Justification::centredLeft);
                }
            }
            g.setColour(panelEdge.withAlpha(0.6f));
            g.drawHorizontalLine(row.getBottom(), float(row.getX()), float(row.getRight()));
        }
    }

    // help
    g.setColour(dimText);
    g.setFont(juce::FontOptions(11.5f));
    g.drawFittedText("Put GHS Drum Gate on every drum mic (overheads/rooms: tick Align only). Mark the overheads as Reference, or the latest-"
                     "arriving mic is used. Press Analyse once in any instance and play 6 s where the whole kit plays - every track "
                     "in the group gets its own correction. Keep Drum Gate early in each track's chain.",
                     helpArea, juce::Justification::topLeft, 3);
}
