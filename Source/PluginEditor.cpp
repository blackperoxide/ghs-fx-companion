#include "PluginEditor.h"
#include "PluginScanning.h"
#include "PluginDatabase.h"
#include <algorithm>

namespace
{
    constexpr int kTitleBarHeight = 22;
    constexpr int kTopBarHeight = 30;
    constexpr int kPresetBarHeight = 28;
    constexpr int kSearchBarHeight = 24;
    constexpr int kRowGap = 6;
    constexpr int kSlotRowHeight = 28;
    constexpr int kSlotRowGap = 2;
    constexpr int kListBoxWidth = 220;
    constexpr int kRackHeight = GHSFXCompanionProcessor::maxChainSlots * (kSlotRowHeight + kSlotRowGap);
    constexpr int kDefaultWidth = 660;
    constexpr int kDefaultHeight = kTitleBarHeight + kTopBarHeight + kRowGap + kPresetBarHeight + kRowGap
                                    + kSearchBarHeight + kRowGap + kRackHeight + 32;
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

void GHSFXCompanionEditor::SlotRow::paint(juce::Graphics& g)
{
    // A recessed channel-strip bay behind each row's controls - the console's
    // module-slot look, distinct from the raised/lit controls sitting on top of it.
    auto bounds = getLocalBounds().toFloat();

    g.setColour(VintageLookAndFeel::bezelDark.withAlpha(0.6f));
    g.fillRoundedRectangle(bounds, 2.0f);

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawLine(bounds.getX(), bounds.getY() + 0.5f, bounds.getRight(), bounds.getY() + 0.5f, 1.0f);
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
    setLookAndFeel(&vintageLookAndFeel);

    searchBox.setTextToShowWhenEmpty("Search plugins...", juce::Colours::grey);
    searchBox.onTextChange = [this] { applySearchFilter(); };
    addAndMakeVisible(searchBox);

    addAndMakeVisible(pluginListBox);

    scanButton.onClick = [this] { refreshPluginList(); };
    addAndMakeVisible(scanButton);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    presetComboBox.setTextWhenNothingSelected("Load Preset...");
    presetComboBox.setTextWhenNoChoicesAvailable("No saved presets");
    presetComboBox.onChange = [this] { presetSelected(); };
    addAndMakeVisible(presetComboBox);

    savePresetButton.onClick = [this] { savePresetClicked(); };
    addAndMakeVisible(savePresetButton);

    deletePresetButton.onClick = [this] { deletePresetClicked(); };
    addAndMakeVisible(deletePresetButton);

    importRecipeButton.onClick = [this] { importRecipeClicked(); };
    addAndMakeVisible(importRecipeButton);

    for (int i = 0; i < GHSFXCompanionProcessor::maxChainSlots; ++i)
    {
        auto* row = slotRows.add(new SlotRow(*this, i));
        addAndMakeVisible(row);
    }

    setResizable(true, true);
    setSize(kDefaultWidth, kDefaultHeight);

    refreshPluginList();
    refreshAllSlotRows();
    refreshPresetList();
}

GHSFXCompanionEditor::~GHSFXCompanionEditor()
{
    closeHostedPluginEditorWindow();
    setLookAndFeel(nullptr);
}

void GHSFXCompanionEditor::paint(juce::Graphics& g)
{
    VintageLookAndFeel::drawConsolePanel(g, getLocalBounds().toFloat());

    auto titleBar = getLocalBounds().reduced(8).removeFromTop(kTitleBarHeight);
    g.setColour(VintageLookAndFeel::cream.withAlpha(0.75f));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText("GHS FX COMPANION", titleBar, juce::Justification::centredLeft);
}

void GHSFXCompanionEditor::resized()
{
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(kTitleBarHeight);

    auto topBar = area.removeFromTop(kTopBarHeight);
    scanButton.setBounds(topBar.removeFromLeft(160));
    topBar.removeFromLeft(8);
    statusLabel.setBounds(topBar);

    area.removeFromTop(kRowGap);

    auto presetBar = area.removeFromTop(kPresetBarHeight);
    presetComboBox.setBounds(presetBar.removeFromLeft(180));
    presetBar.removeFromLeft(6);
    savePresetButton.setBounds(presetBar.removeFromLeft(100));
    presetBar.removeFromLeft(6);
    deletePresetButton.setBounds(presetBar.removeFromLeft(100));
    presetBar.removeFromLeft(14);
    importRecipeButton.setBounds(presetBar.removeFromLeft(120));

    area.removeFromTop(kRowGap);

    auto searchBar = area.removeFromTop(kSearchBarHeight);
    searchBox.setBounds(searchBar.removeFromLeft(kListBoxWidth));

    area.removeFromTop(kRowGap);

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
        auto text = desc.name + "  (" + desc.pluginFormatName + ")";

        if (auto* dbEntry = GHSPluginDatabase::lookupPlugin(desc.name))
            text << "  \xe2\x80\x94  " << dbEntry->category; // em dash

        g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
    }
}

void GHSFXCompanionEditor::refreshPluginList()
{
    allScannedPlugins = ghsProcessor.loadKnownPlugins();
    std::sort(allScannedPlugins.begin(), allScannedPlugins.end(),
              [](const juce::PluginDescription& a, const juce::PluginDescription& b)
              {
                  return a.name.compareIgnoreCase(b.name) < 0;
              });

    applySearchFilter();

    if (allScannedPlugins.isEmpty())
    {
        statusLabel.setText("No plugin list yet - run \"GHS FX Companion Scanner\" once (outside Logic), then click Refresh.",
                             juce::dontSendNotification);
        return;
    }

    auto lastScan = GHSPluginScanning::getLastScanTime();
    juce::String scanInfo;
    if (lastScan != juce::Time())
        scanInfo = " - last scanned " + (juce::Time::getCurrentTime() - lastScan).getApproximateDescription() + " ago";

    statusLabel.setText(juce::String(allScannedPlugins.size()) + " plugin(s) available" + scanInfo
                             + ". Select one, then \"Load Selected\" on a chain slot below.",
                         juce::dontSendNotification);
}

void GHSFXCompanionEditor::applySearchFilter()
{
    auto query = searchBox.getText().trim();

    foundPlugins.clear();
    for (auto& desc : allScannedPlugins)
    {
        bool matches = query.isEmpty()
                       || desc.name.containsIgnoreCase(query)
                       || desc.manufacturerName.containsIgnoreCase(query);

        // Also match category/subcategory/tags from the real plugin database, so
        // e.g. searching "compressor" surfaces every compressor even if that word
        // isn't literally in the product name.
        if (!matches)
        {
            if (auto* dbEntry = GHSPluginDatabase::lookupPlugin(desc.name))
            {
                matches = dbEntry->category.containsIgnoreCase(query)
                          || dbEntry->subcategory.containsIgnoreCase(query)
                          || dbEntry->tags.containsIgnoreCase(query);
            }
        }

        if (matches)
            foundPlugins.add(desc);
    }

    pluginListBox.deselectAllRows();
    pluginListBox.updateContent();
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

void GHSFXCompanionEditor::refreshPresetList()
{
    presetComboBox.clear(juce::dontSendNotification);

    int itemId = 1;
    for (auto& name : ghsProcessor.getChainPresetNames())
        presetComboBox.addItem(name, itemId++);
}

void GHSFXCompanionEditor::presetSelected()
{
    const auto itemIndex = presetComboBox.getSelectedItemIndex();
    if (itemIndex < 0)
        return;

    auto name = presetComboBox.getItemText(itemIndex);

    // Any slot's editor currently open may be about to be replaced or removed.
    closeHostedPluginEditorWindow();

    statusLabel.setText("Loading preset \"" + name + "\"...", juce::dontSendNotification);

    ghsProcessor.loadChainPreset(name, [this, name]
    {
        refreshAllSlotRows();
        statusLabel.setText("Loaded preset \"" + name + "\".", juce::dontSendNotification);
    });
}

void GHSFXCompanionEditor::savePresetClicked()
{
    auto* window = new juce::AlertWindow("Save Preset", "Name this chain preset:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", "", "Preset name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true, juce::ModalCallbackFunction::create([this, window](int result)
    {
        std::unique_ptr<juce::AlertWindow> owned(window);
        if (result != 1)
            return;

        auto name = owned->getTextEditorContents("name").trim();
        if (name.isEmpty())
            return;

        if (ghsProcessor.saveChainPreset(name))
        {
            statusLabel.setText("Saved preset \"" + name + "\".", juce::dontSendNotification);
            refreshPresetList();
        }
        else
        {
            statusLabel.setText("Failed to save preset \"" + name + "\".", juce::dontSendNotification);
        }
    }), false);
}

void GHSFXCompanionEditor::deletePresetClicked()
{
    const auto itemIndex = presetComboBox.getSelectedItemIndex();
    if (itemIndex < 0)
    {
        statusLabel.setText("Select a preset from the dropdown first.", juce::dontSendNotification);
        return;
    }

    auto name = presetComboBox.getItemText(itemIndex);

    if (ghsProcessor.deleteChainPreset(name))
    {
        statusLabel.setText("Deleted preset \"" + name + "\".", juce::dontSendNotification);
        refreshPresetList();
    }
    else
    {
        statusLabel.setText("Failed to delete preset \"" + name + "\".", juce::dontSendNotification);
    }
}

void GHSFXCompanionEditor::importRecipeClicked()
{
    recipeFileChooser = std::make_unique<juce::FileChooser>(
        "Import Chain Recipe (.ghsrecipe.json, from the GHS FX Chain Builder website)",
        juce::File(), "*.json");

    recipeFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;

            auto known = ghsProcessor.loadKnownPlugins();
            auto matches = GHSRecipeImport::loadAndMatch(file, known, GHSFXCompanionProcessor::maxChainSlots);

            if (matches.isEmpty())
            {
                statusLabel.setText("Couldn't read a recipe from \"" + file.getFileName() + "\".",
                                     juce::dontSendNotification);
                return;
            }

            auto unmatchedNames = std::make_shared<juce::StringArray>();
            int matchedCount = 0;
            for (auto& m : matches)
            {
                if (m.matched)
                    ++matchedCount;
                else
                    unmatchedNames->add(m.stageName);
            }

            if (matchedCount == 0)
            {
                statusLabel.setText("None of \"" + file.getFileName() + "\"'s plugins were found in your library.",
                                     juce::dontSendNotification);
                return;
            }

            // Any slot's editor currently open may be about to be replaced.
            closeHostedPluginEditorWindow();

            statusLabel.setText("Importing recipe \"" + file.getFileNameWithoutExtension() + "\"...",
                                 juce::dontSendNotification);

            auto remaining = std::make_shared<int>(matchedCount);
            for (int slotIndex = 0; slotIndex < matches.size(); ++slotIndex)
            {
                auto& m = matches.getReference(slotIndex);
                if (!m.matched)
                    continue;

                ghsProcessor.loadPluginIntoSlot(slotIndex, m.description,
                    [this, slotIndex, remaining, unmatchedNames](const juce::String&)
                    {
                        if (juce::isPositiveAndBelow(slotIndex, slotRows.size()))
                            slotRows[slotIndex]->refresh();

                        if (--(*remaining) <= 0)
                        {
                            auto msg = juce::String("Recipe imported.");
                            if (!unmatchedNames->isEmpty())
                                msg += " Not found in your library: " + unmatchedNames->joinIntoString(", ");
                            statusLabel.setText(msg, juce::dontSendNotification);
                        }
                    });
            }
        });
}
