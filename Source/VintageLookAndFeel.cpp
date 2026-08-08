#include "VintageLookAndFeel.h"

const juce::Colour VintageLookAndFeel::bodyDark      { 0xff26241f };
const juce::Colour VintageLookAndFeel::panelMetalHi  { 0xff726a58 };
const juce::Colour VintageLookAndFeel::panelMetalLo  { 0xff2e2b23 };
const juce::Colour VintageLookAndFeel::bezelDark     { 0xff100f0c };
const juce::Colour VintageLookAndFeel::cream         { 0xffe9dfc4 };
const juce::Colour VintageLookAndFeel::creamDim      { 0xffab9f80 };
const juce::Colour VintageLookAndFeel::amber         { 0xffe8a33d };
const juce::Colour VintageLookAndFeel::amberBright   { 0xffffc35c };
const juce::Colour VintageLookAndFeel::redLED        { 0xffd8432c };

VintageLookAndFeel::VintageLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, bodyDark);

    setColour(juce::TextButton::buttonColourId, panelMetalLo);
    setColour(juce::TextButton::buttonOnColourId, amber.darker(0.6f));
    setColour(juce::TextButton::textColourOffId, cream);
    setColour(juce::TextButton::textColourOnId, amberBright);

    setColour(juce::ToggleButton::textColourId, cream);

    setColour(juce::Label::textColourId, cream);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextEditor::backgroundColourId, bezelDark);
    setColour(juce::TextEditor::textColourId, cream);
    setColour(juce::TextEditor::outlineColourId, panelMetalLo);
    setColour(juce::TextEditor::focusedOutlineColourId, amber);
    setColour(juce::TextEditor::highlightColourId, amber.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
    setColour(juce::CaretComponent::caretColourId, amber);

    setColour(juce::ComboBox::backgroundColourId, panelMetalLo);
    setColour(juce::ComboBox::textColourId, cream);
    setColour(juce::ComboBox::outlineColourId, bezelDark);
    setColour(juce::ComboBox::arrowColourId, amber);

    setColour(juce::ListBox::backgroundColourId, bezelDark);
    setColour(juce::ListBox::textColourId, cream);
    setColour(juce::ListBox::outlineColourId, panelMetalLo);

    setColour(juce::AlertWindow::backgroundColourId, bodyDark);
    setColour(juce::AlertWindow::textColourId, cream);
    setColour(juce::AlertWindow::outlineColourId, panelMetalLo);
}

void VintageLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const float corner = 3.0f;
    const bool pressed = shouldDrawButtonAsDown && button.isEnabled();

    // Dark bezel, then a raised (or, if pressed, inset) metal face inside it -
    // the "popup button" look classic hardware-style plugins use.
    g.setColour(bezelDark);
    g.fillRoundedRectangle(bounds, corner);

    auto face = bounds.reduced(1.5f);
    juce::ColourGradient grad(pressed ? panelMetalLo : panelMetalHi, face.getX(), face.getY(),
                               pressed ? panelMetalHi : panelMetalLo, face.getX(), face.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(face, corner - 0.5f);

    if (!pressed)
    {
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawLine(face.getX() + 2.0f, face.getY() + 1.0f, face.getRight() - 2.0f, face.getY() + 1.0f, 1.0f);
    }

    if (shouldDrawButtonAsHighlighted && button.isEnabled())
    {
        g.setColour(amber.withAlpha(0.18f));
        g.fillRoundedRectangle(face, corner - 0.5f);
    }

    if (!button.isEnabled())
    {
        g.setColour(bodyDark.withAlpha(0.55f));
        g.fillRoundedRectangle(bounds, corner);
    }
}

void VintageLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsDown);

    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    const float corner = 3.0f;
    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();

    g.setColour(bezelDark);
    g.fillRoundedRectangle(bounds, corner);

    auto face = bounds.reduced(1.5f);
    auto base = on ? redLED : panelMetalLo;
    juce::ColourGradient grad(base.brighter(on ? 0.35f : 0.05f), face.getX(), face.getY(),
                               base.darker(on ? 0.1f : 0.3f), face.getX(), face.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(face, corner - 0.5f);

    if (on)
    {
        // A soft glow ring around a lit switch - unmistakably "engaged" at a glance.
        g.setColour(redLED.withAlpha(0.45f));
        g.drawRoundedRectangle(face.expanded(1.0f), corner, 2.0f);
    }
    else if (shouldDrawButtonAsHighlighted && enabled)
    {
        g.setColour(amber.withAlpha(0.15f));
        g.fillRoundedRectangle(face, corner - 0.5f);
    }

    g.setColour((on ? juce::Colours::white : cream).withAlpha(enabled ? 1.0f : 0.4f));
    g.setFont(juce::Font(juce::FontOptions(juce::jmin(13.0f, bounds.getHeight() * 0.55f), juce::Font::bold)));
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void VintageLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                       int, int, int, int, juce::ComboBox& box)
{
    juce::ignoreUnused(isButtonDown);

    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(1.0f);
    const float corner = 3.0f;

    g.setColour(bezelDark);
    g.fillRoundedRectangle(bounds, corner);

    auto face = bounds.reduced(1.5f);
    juce::ColourGradient grad(panelMetalHi, face.getX(), face.getY(), panelMetalLo, face.getX(), face.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(face, corner - 0.5f);

    // Small triangular arrow in the amber legend colour, in the right-hand notch Label leaves clear.
    auto arrowZone = juce::Rectangle<float>((float) width - 20.0f, 0.0f, 20.0f, (float) height);
    auto cx = arrowZone.getCentreX();
    auto cy = arrowZone.getCentreY();

    juce::Path arrow;
    arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour(box.isEnabled() ? amber : creamDim);
    g.fillPath(arrow);
}

juce::Font VintageLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::Font(juce::FontOptions((float) juce::jmin(15, buttonHeight - 6), juce::Font::bold));
}

void VintageLookAndFeel::drawConsolePanel(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    juce::ColourGradient body(bodyDark.brighter(0.08f), bounds.getX(), bounds.getY(),
                               bodyDark.darker(0.25f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(body);
    g.fillRect(bounds);

    // Faint brushed-metal grain - deterministic (fixed seed) so it doesn't
    // shimmer differently on every repaint.
    juce::Random rng(1);
    for (float y = bounds.getY(); y < bounds.getBottom(); y += 2.0f)
    {
        g.setColour(juce::Colours::white.withAlpha(0.015f + rng.nextFloat() * 0.02f));
        g.drawLine(bounds.getX(), y, bounds.getRight(), y, 1.0f);
    }

    const float inset = 10.0f;
    const float r = 4.0f;
    const juce::Point<float> corners[] = {
        { bounds.getX() + inset, bounds.getY() + inset },
        { bounds.getRight() - inset, bounds.getY() + inset },
        { bounds.getX() + inset, bounds.getBottom() - inset },
        { bounds.getRight() - inset, bounds.getBottom() - inset },
    };

    for (auto& c : corners)
    {
        juce::ColourGradient screwGrad(panelMetalHi, c.x - r * 0.4f, c.y - r * 0.4f,
                                        panelMetalLo, c.x + r, c.y + r, true);
        g.setGradientFill(screwGrad);
        g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        g.setColour(bezelDark);
        g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.6f);
        g.drawLine(c.x - r * 0.6f, c.y - r * 0.6f, c.x + r * 0.6f, c.y + r * 0.6f, 1.0f);
    }
}
