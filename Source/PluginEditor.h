#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
 * Milestone 2 UI: the scanned-plugin list on the left feeds a fixed rack of
 * GHSFXCompanionProcessor::maxChainSlots slot rows on the right - each row
 * loads/removes/bypasses/reorders one link in the chain. Only one hosted
 * plugin's own editor is shown at a time (click "Edit" on a row to swap it
 * in below), same window-management approach as Milestone 1 had for its
 * single hosted plugin.
 */
class GHSFXCompanionEditor : public juce::AudioProcessorEditor,
                              private juce::ListBoxModel
{
public:
    explicit GHSFXCompanionEditor(GHSFXCompanionProcessor&);
    ~GHSFXCompanionEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // --- juce::ListBoxModel ---
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;

private:
    /** One row in the chain rack: load/remove/bypass/reorder/edit for a single slot index. */
    class SlotRow : public juce::Component
    {
    public:
        SlotRow(GHSFXCompanionEditor& ownerEditor, int slotIndex);

        void resized() override;

        /** Pulls current plugin name / bypass state from the processor and repaints. */
        void refresh();

    private:
        GHSFXCompanionEditor& editor;
        int index;

        juce::Label slotLabel;
        juce::ToggleButton bypassButton { "Bypass" };
        juce::TextButton loadButton { "Load Selected" };
        juce::TextButton removeButton { "Remove" };
        juce::TextButton editButton { "Edit" };
        juce::TextButton upButton { "Up" };
        juce::TextButton downButton { "Down" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotRow)
    };

    void refreshPluginList();
    void loadSelectedPluginIntoSlot(int slotIndex);
    void removeSlot(int slotIndex);
    void moveSlot(int slotIndex, int direction);
    void toggleSlotEditor(int slotIndex);
    void showHostedPluginEditor(int slotIndex);
    void closeHostedPluginEditorWindow();
    void refreshAllSlotRows();

    GHSFXCompanionProcessor& ghsProcessor;

    juce::Array<juce::PluginDescription> foundPlugins;

    juce::ListBox pluginListBox { "Available Plugins", this };
    juce::TextButton scanButton { "Refresh Plugin List" };
    juce::Label statusLabel;

    juce::OwnedArray<SlotRow> slotRows;

    int openEditorSlot = -1;
    std::unique_ptr<juce::AudioProcessorEditor> hostedEditor;
    std::unique_ptr<juce::Component> hostedEditorHolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHSFXCompanionEditor)
};
