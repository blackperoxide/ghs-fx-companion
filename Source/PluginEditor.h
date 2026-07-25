#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
 * Milestone 1 UI: a plain list of scanned plugins + Scan/Load buttons, and a
 * space below that embeds the hosted plugin's own editor once one is loaded.
 * No chain UI, no AI matching, no icons yet - just prove you can pick a real
 * plugin from this list and see/hear it running inside our own plugin.
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
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

private:
    void refreshPluginList();
    void loadSelectedPlugin();
    void showHostedPluginEditor();
    void closeHostedPluginEditorWindow();

    GHSFXCompanionProcessor& processor;

    juce::Array<juce::PluginDescription> foundPlugins;

    juce::ListBox pluginListBox { "Available Plugins", this };
    juce::TextButton scanButton { "Refresh Plugin List" };
    juce::TextButton loadButton { "Load Selected" };
    juce::Label statusLabel;

    std::unique_ptr<juce::AudioProcessorEditor> hostedEditor;
    std::unique_ptr<juce::Component> hostedEditorHolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHSFXCompanionEditor)
};
