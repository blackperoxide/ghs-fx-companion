#include "PluginEditor.h"
#include "PluginScanning.h"

namespace
{
    constexpr int kTopBarHeight = 110;
    constexpr int kDefaultWidth = 560;
    constexpr int kDefaultHeight = 420;
}

GHSFXCompanionEditor::GHSFXCompanionEditor(GHSFXCompanionProcessor& p)
    : juce::AudioProcessorEditor(&p), ghsProcessor(p)
{
    addAndMakeVisible(pluginListBox);

    scanButton.onClick = [this] { refreshPluginList(); };
    addAndMakeVisible(scanButton);

    loadButton.onClick = [this] { loadSelectedPlugin(); };
    addAndMakeVisible(loadButton);

    bypassButton.setToggleState(ghsProcessor.getHostedBypassParameter()->get(), juce::dontSendNotification);
    bypassButton.onClick = [this] { bypassButtonClicked(); };
    addAndMakeVisible(bypassButton);

    statusLabel.setText("Run \"GHS FX Companion Scanner\" once (outside Logic), then click Refresh.",
                         juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    setResizable(true, true);
    setSize(kDefaultWidth, kDefaultHeight);

    refreshPluginList();
}

GHSFXCompanionEditor::~GHSFXCompanionEditor()
{
    closeHostedPluginEditorWindow();
}

void GHSFXCompanionEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void GHSFXCompanionEditor::resized()
{
    auto area = getLocalBounds().reduced(8);

    auto topBar = area.removeFromTop(kTopBarHeight);
    auto buttonRow = topBar.removeFromTop(28);
    scanButton.setBounds(buttonRow.removeFromLeft(160));
    buttonRow.removeFromLeft(8);
    loadButton.setBounds(buttonRow.removeFromLeft(160));
    buttonRow.removeFromLeft(8);
    bypassButton.setBounds(buttonRow.removeFromLeft(160));

    topBar.removeFromTop(4);
    statusLabel.setBounds(topBar.removeFromTop(20));

    topBar.removeFromTop(4);
    pluginListBox.setBounds(topBar);

    area.removeFromTop(8);

    if (hostedEditorHolder != nullptr)
        hostedEditorHolder->setBounds(area);
}

int GHSFXCompanionEditor::getNumRows()
{
    return foundPlugins.size();
}

void GHSFXCompanionEditor::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(getLookAndFeel().findColour(juce::TextEditor::highlightColourId));

    g.setColour(getLookAndFeel().findColour(juce::ListBox::textColourId));
    g.setFont((float) height * 0.6f);

    if (juce::isPositiveAndBelow(rowNumber, foundPlugins.size()))
    {
        auto& desc = foundPlugins.getReference(rowNumber);
        g.drawText(desc.name + "  (" + desc.pluginFormatName + ")",
                    4, 0, width - 8, height, juce::Justification::centredLeft, true);
    }
}

void GHSFXCompanionEditor::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    juce::ignoreUnused(row);
    loadSelectedPlugin();
}

void GHSFXCompanionEditor::refreshPluginList()
{
    foundPlugins = ghsProcessor.loadKnownPlugins();
    pluginListBox.updateContent();

    if (foundPlugins.isEmpty())
    {
        statusLabel.setText("No plugin list yet - run \"GHS FX Companion Scanner\" once (outside Logic), then click Refresh.",
                             juce::dontSendNotification);
        return;
    }

    auto lastScan = GHSPluginScanning::getLastScanTime();
    juce::String scanInfo;
    if (lastScan != juce::Time())
        scanInfo = " - last scanned " + (juce::Time::getCurrentTime() - lastScan).getApproximateDescription() + " ago";

    statusLabel.setText(juce::String(foundPlugins.size()) + " plugin(s) available" + scanInfo + ".",
                         juce::dontSendNotification);
}

void GHSFXCompanionEditor::loadSelectedPlugin()
{
    auto row = pluginListBox.getSelectedRow();
    if (!juce::isPositiveAndBelow(row, foundPlugins.size()))
    {
        statusLabel.setText("Select a plugin from the list first.", juce::dontSendNotification);
        return;
    }

    auto description = foundPlugins.getReference(row);
    statusLabel.setText("Loading " + description.name + "...", juce::dontSendNotification);

    ghsProcessor.loadHostedPlugin(description, [this, description](const juce::String& error)
    {
        if (error.isNotEmpty())
        {
            statusLabel.setText("Failed to load " + description.name + ": " + error, juce::dontSendNotification);
            return;
        }

        statusLabel.setText("Loaded: " + description.name, juce::dontSendNotification);
        showHostedPluginEditor();
    });
}

void GHSFXCompanionEditor::showHostedPluginEditor()
{
    closeHostedPluginEditorWindow();

    auto* hosted = ghsProcessor.getHostedPlugin();
    if (hosted == nullptr)
        return;

    hostedEditor.reset(hosted->createEditorIfNeeded());
    if (hostedEditor == nullptr)
        return;

    hostedEditorHolder = std::make_unique<juce::Component>();
    hostedEditorHolder->addAndMakeVisible(*hostedEditor);
    addAndMakeVisible(*hostedEditorHolder);

    // Grow our own window to fit the hosted plugin's real editor size.
    auto hostedBounds = hostedEditor->getLocalBounds();
    setSize(juce::jmax(kDefaultWidth, hostedBounds.getWidth() + 16),
             kTopBarHeight + 16 + juce::jmax(120, hostedBounds.getHeight()));

    resized();
}

void GHSFXCompanionEditor::closeHostedPluginEditorWindow()
{
    hostedEditor.reset();
    hostedEditorHolder.reset();
}

void GHSFXCompanionEditor::bypassButtonClicked()
{
    auto* param = ghsProcessor.getHostedBypassParameter();
    *param = !param->get();
    bypassButton.setToggleState(param->get(), juce::dontSendNotification);
}
