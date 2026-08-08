#include "PluginEditor.h"
#include "PluginScanning.h"

namespace
{
    constexpr int kTopBarHeight = 36;
    constexpr int kSlotRowHeight = 28;
    constexpr int kSlotRowGap = 2;
    constexpr int kListBoxWidth = 220;
    constexpr int kRackHeight = GHSFXCompanionProcessor::maxChainSlots * (kSlotRowHeight + kSlotRowGap);
    constexpr int kDefaultWidth = 660;
    constexpr int kDefaultHeight = kTopBarHeight + kRackHeight + 32;
}

// ============================== SlotRow ====================================

GHSFXCompanionEditor::SlotRow::SlotRow(GHSFXCompanionEditor& ownerEditor, int slotIndex)
    : editor(ownerEditor), index(slotIndex)
{
    slotLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(slotLabel);

    bypassButton.onClick = [this]
    {
        auto* param = editor.ghsProcessor.getSlotBypassParameter(index);
        *param = !param->get();
        refresh();
    };
    addAndMakeVisible(bypassButton);

    loadButton.onClick = [this] { editor.loadSelectedPluginIntoSlot(index); };
    addAndMakeVisible(loadButton);

    removeButton.onClick = [this] { editor.removeSlot(index); };
    addAndMakeVisible(removeButton);

    editButton.onClick = [this] { editor.toggleSlotEditor(index); };
    addAndMakeVisible(editButton);

    upButton.onClick = [this] { editor.moveSlot(index, -1); };
    addAndMakeVisible(upButton);

    downButton.onClick = [this] { editor.moveSlot(index, 1); };
    addAndMakeVisible(downButton);
}

void GHSFXCompanionEditor::SlotRow::resized()
{
    auto area = getLocalBounds();

    auto reorderCol = area.removeFromLeft(36);
    upButton.setBounds(reorderCol.removeFromTop(getHeight() / 2));
    downButton.setBounds(reorderCol);

    area.removeFromLeft(4);
    removeButton.setBounds(area.removeFromRight(60));
    area.removeFromRight(4);
    editButton.setBounds(area.removeFromRight(52));
    area.removeFromRight(4);
    loadButton.setBounds(area.removeFromRight(104));
    area.removeFromRight(4);
    bypassButton.setBounds(area.removeFromRight(68));
    area.removeFromRight(6);

    slotLabel.setBounds(area);
}

void GHSFXCompanionEditor::SlotRow::refresh()
{
    auto* plugin = editor.ghsProcessor.getPluginInSlot(index);
    auto* bypassParam = editor.ghsProcessor.getSlotBypassParameter(index);

    slotLabel.setText(juce::String(index + 1) + ". " + (plugin != nullptr ? plugin->getName() : juce::String("Empty")),
                       juce::dontSendNotification);

    bypassButton.setToggleState(bypassParam->get(), juce::dontSendNotification);
    bypassButton.setEnabled(plugin != nullptr);
    removeButton.setEnabled(plugin != nullptr);
    editButton.setEnabled(plugin != nullptr);
    editButton.setButtonText(editor.openEditorSlot == index ? "Close" : "Edit");

    upButton.setEnabled(index > 0);
    downButton.setEnabled(index < GHSFXCompanionProcessor::maxChainSlots - 1);
}

// ============================ Editor ========================================

GHSFXCompanionEditor::GHSFXCompanionEditor(GHSFXCompanionProcessor& p)
    : juce::AudioProcessorEditor(&p), ghsProcessor(p)
{
    addAndMakeVisible(pluginListBox);

    scanButton.onClick = [this] { refreshPluginList(); };
    addAndMakeVisible(scanButton);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    for (int i = 0; i < GHSFXCompanionProcessor::maxChainSlots; ++i)
    {
        auto* row = slotRows.add(new SlotRow(*this, i));
        addAndMakeVisible(row);
    }

    setResizable(true, true);
    setSize(kDefaultWidth, kDefaultHeight);

    refreshPluginList();
    refreshAllSlotRows();
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
    scanButton.setBounds(topBar.removeFromLeft(160));
    topBar.removeFromLeft(8);
    statusLabel.setBounds(topBar);

    area.removeFromTop(8);

    auto middle = area.removeFromTop(kRackHeight);
    pluginListBox.setBounds(middle.removeFromLeft(kListBoxWidth));
    middle.removeFromLeft(8);

    auto rackArea = middle;
    for (auto* row : slotRows)
    {
        row->setBounds(rackArea.removeFromTop(kSlotRowHeight));
        rackArea.removeFromTop(kSlotRowGap);
    }

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

    statusLabel.setText(juce::String(foundPlugins.size()) + " plugin(s) available" + scanInfo
                             + ". Select one, then \"Load Selected\" on a chain slot below.",
                         juce::dontSendNotification);
}

void GHSFXCompanionEditor::loadSelectedPluginIntoSlot(int slotIndex)
{
    auto row = pluginListBox.getSelectedRow();
    if (!juce::isPositiveAndBelow(row, foundPlugins.size()))
    {
        statusLabel.setText("Select a plugin from the list first.", juce::dontSendNotification);
        return;
    }

    auto description = foundPlugins.getReference(row);
    statusLabel.setText("Loading " + description.name + " into slot " + juce::String(slotIndex + 1) + "...",
                         juce::dontSendNotification);

    ghsProcessor.loadPluginIntoSlot(slotIndex, description, [this, slotIndex, description](const juce::String& error)
    {
        if (error.isNotEmpty())
        {
            statusLabel.setText("Failed to load " + description.name + ": " + error, juce::dontSendNotification);
            slotRows[slotIndex]->refresh();
            return;
        }

        statusLabel.setText("Loaded " + description.name + " into slot " + juce::String(slotIndex + 1) + ".",
                             juce::dontSendNotification);
        slotRows[slotIndex]->refresh();

        if (openEditorSlot == slotIndex)
            showHostedPluginEditor(slotIndex);
    });
}

void GHSFXCompanionEditor::removeSlot(int slotIndex)
{
    if (openEditorSlot == slotIndex)
        closeHostedPluginEditorWindow();

    ghsProcessor.unloadSlot(slotIndex);
    slotRows[slotIndex]->refresh();
    statusLabel.setText("Removed slot " + juce::String(slotIndex + 1) + ".", juce::dontSendNotification);
}

void GHSFXCompanionEditor::moveSlot(int slotIndex, int direction)
{
    const int target = slotIndex + direction;
    if (!juce::isPositiveAndBelow(target, GHSFXCompanionProcessor::maxChainSlots))
        return;

    ghsProcessor.moveSlot(slotIndex, target);

    if (openEditorSlot == slotIndex)
        openEditorSlot = target;
    else if (openEditorSlot == target)
        openEditorSlot = slotIndex;

    refreshAllSlotRows();
}

void GHSFXCompanionEditor::toggleSlotEditor(int slotIndex)
{
    if (openEditorSlot == slotIndex)
        closeHostedPluginEditorWindow();
    else
        showHostedPluginEditor(slotIndex);
}

void GHSFXCompanionEditor::showHostedPluginEditor(int slotIndex)
{
    closeHostedPluginEditorWindow();

    auto* hosted = ghsProcessor.getPluginInSlot(slotIndex);
    if (hosted == nullptr)
        return;

    hostedEditor.reset(hosted->createEditorIfNeeded());
    if (hostedEditor == nullptr)
        return;

    openEditorSlot = slotIndex;
    slotRows[slotIndex]->refresh();

    hostedEditorHolder = std::make_unique<juce::Component>();
    hostedEditorHolder->addAndMakeVisible(*hostedEditor);
    addAndMakeVisible(*hostedEditorHolder);

    // Grow our own window to fit the hosted plugin's real editor size.
    auto hostedBounds = hostedEditor->getLocalBounds();
    setSize(juce::jmax(kDefaultWidth, hostedBounds.getWidth() + 16),
             kDefaultHeight + 8 + juce::jmax(120, hostedBounds.getHeight()));

    resized();
}

void GHSFXCompanionEditor::closeHostedPluginEditorWindow()
{
    const int previouslyOpen = openEditorSlot;

    hostedEditor.reset();
    hostedEditorHolder.reset();
    openEditorSlot = -1;

    if (juce::isPositiveAndBelow(previouslyOpen, slotRows.size()))
        slotRows[previouslyOpen]->refresh();
}

void GHSFXCompanionEditor::refreshAllSlotRows()
{
    for (auto* row : slotRows)
        row->refresh();
}
