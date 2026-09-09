#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "RecipeImport.h"
#include "ToneRecommendation.h"

/**
 * Web-based UI: the actual interface (plugin browser, rack, presets, record &
 * suggest) is HTML/CSS/JS served from the Resources/webui folder (embedded
 * via BinaryData, no network access, no build step) and rendered in a
 * juce::WebBrowserComponent - real interactive web UI, but the plugin ships
 * as one self-contained binary with no external process/server.
 *
 * Everything the frontend can do goes through a small set of native
 * functions (withNativeFunction) that call straight into
 * GHSFXCompanionProcessor; async results and out-of-band events (a live
 * recording timer, toasts, import/analysis completion) go back to the page
 * via emitEventIfBrowserIsVisible(). See Resources/webui/app.js for the
 * matching frontend-side protocol.
 *
 * A hosted third-party plugin's own editor is a native juce::Component and
 * can't live inside the webview, so "Edit" opens it in its own native
 * juce::DocumentWindow instead.
 */
class GHSFXCompanionEditor : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit GHSFXCompanionEditor(GHSFXCompanionProcessor&);
    ~GHSFXCompanionEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override; // emits "toneCaptureTick" while recording

    std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String& url);

    juce::var handleGetState();
    juce::var handleSearchPlugins(const juce::Array<juce::var>& args);
    void handleLoadPluginIntoSlot(const juce::Array<juce::var>& args,
                                   juce::WebBrowserComponent::NativeFunctionCompletion completion);
    juce::var handleUnloadSlot(const juce::Array<juce::var>& args);
    juce::var handleMoveSlot(const juce::Array<juce::var>& args);
    juce::var handleToggleBypass(const juce::Array<juce::var>& args);
    juce::var handleRefreshPluginScan();
    void handleSavePreset(const juce::Array<juce::var>& args,
                           juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void handleLoadPreset(const juce::Array<juce::var>& args,
                           juce::WebBrowserComponent::NativeFunctionCompletion completion);
    juce::var handleDeletePreset(const juce::Array<juce::var>& args);
    void handleImportRecipe();
    void handleStartToneCapture();
    void handleStopToneCaptureAndAnalyze(juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void handleOpenHostedEditor(const juce::Array<juce::var>& args);

    const juce::PluginDescription* findKnownPluginByIdentifier(const juce::String& identifier);
    void emitToast(const juce::String& text, const juce::String& tone = "info");

    GHSFXCompanionProcessor& ghsProcessor;

    struct SinglePageBrowser : juce::WebBrowserComponent
    {
        using WebBrowserComponent::WebBrowserComponent;

        // We only ever navigate to our own embedded resource root - refuse
        // anything else (there are no external links in this UI, but a
        // right-click "reload"/history action on some backends could try).
        bool pageAboutToLoad(const juce::String& newURL) override
        {
            return newURL == getResourceProviderRoot();
        }
    };

    /** A DocumentWindow whose close button just runs a callback - used for a hosted plugin's own editor. */
    struct HostedEditorWindow : juce::DocumentWindow
    {
        HostedEditorWindow(const juce::String& name, std::function<void()> onClose)
            : juce::DocumentWindow(name, juce::Colours::black, juce::DocumentWindow::closeButton),
              closeCallback(std::move(onClose))
        {
        }

        void closeButtonPressed() override { closeCallback(); }

        std::function<void()> closeCallback;
    };

    // Built in the constructor body (not as a default member initialiser) since
    // its Options need lambdas capturing `this`, which isn't valid yet in an
    // in-class initialiser.
    std::unique_ptr<SinglePageBrowser> webView;

    std::unique_ptr<HostedEditorWindow> hostedEditorWindow;

    /** Refreshed on construction and whenever the frontend asks for a rescan. */
    juce::Array<juce::PluginDescription> allScannedPlugins;

    std::unique_ptr<juce::FileChooser> recipeFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHSFXCompanionEditor)
};
