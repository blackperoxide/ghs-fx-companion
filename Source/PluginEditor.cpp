#include "PluginEditor.h"
#include "PluginScanning.h"
#include "PluginDatabase.h"
#include "BinaryData.h"
#include <algorithm>
#include <cstring>

namespace
{
    std::optional<juce::WebBrowserComponent::Resource> makeResource(const char* data, int size, const char* mime)
    {
        std::vector<std::byte> bytes((size_t) size);
        std::memcpy(bytes.data(), data, (size_t) size);
        return juce::WebBrowserComponent::Resource { std::move(bytes), juce::String(mime) };
    }

    juce::var toVarArray(const juce::StringArray& strings)
    {
        juce::Array<juce::var> result;
        for (auto& s : strings)
            result.add(s);
        return result;
    }
}

// ============================================================================

GHSFXCompanionEditor::GHSFXCompanionEditor(GHSFXCompanionProcessor& p)
    : juce::AudioProcessorEditor(&p), ghsProcessor(p)
{
    allScannedPlugins = ghsProcessor.loadKnownPlugins();
    std::sort(allScannedPlugins.begin(), allScannedPlugins.end(),
              [](const juce::PluginDescription& a, const juce::PluginDescription& b)
              {
                  return a.name.compareIgnoreCase(b.name) < 0;
              });

    auto options = juce::WebBrowserComponent::Options{}
        .withNativeIntegrationEnabled()
        .withResourceProvider([this](const juce::String& url) { return getResource(url); })
        .withNativeFunction("getState",
            [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                completion(handleGetState());
            })
        .withNativeFunction("searchPlugins",
            [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                completion(handleSearchPlugins(args));
            })
        .withNativeFunction("loadPluginIntoSlot",
            [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                handleLoadPluginIntoSlot(args, completion);
            })
        .withNativeFunction("unloadSlot",
            [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                completion(handleUnloadSlot(args));
            })
        .withNativeFunction("moveSlot",
            [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                completion(handleMoveSlot(args));
            })
        .withNativeFunction("toggleBypass",
            [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                completion(handleToggleBypass(args));
            })
        .withNativeFunction("refreshPluginScan",
            [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                completion(handleRefreshPluginScan());
            })
        .withNativeFunction("savePreset",
            [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                handleSavePreset(args, completion);
            })
        .withNativeFunction("loadPreset",
            [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                handleLoadPreset(args, completion);
            })
        .withNativeFunction("deletePreset",
            [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                completion(handleDeletePreset(args));
            })
        .withNativeFunction("importRecipe",
            [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                handleImportRecipe();
                completion({});
            })
        .withNativeFunction("startToneCapture",
            [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                handleStartToneCapture();
                completion({});
            })
        .withNativeFunction("stopToneCaptureAndAnalyze",
            [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                handleStopToneCaptureAndAnalyze(completion);
            })
        .withNativeFunction("openHostedEditor",
            [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
            {
                handleOpenHostedEditor(args);
                completion({});
            });

    webView = std::make_unique<SinglePageBrowser>(options);
    addAndMakeVisible(*webView);
    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable(true, true);
    setSize(920, 640);
}

GHSFXCompanionEditor::~GHSFXCompanionEditor()
{
    hostedEditorWindow.reset();
}

void GHSFXCompanionEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void GHSFXCompanionEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds(getLocalBounds());
}

void GHSFXCompanionEditor::timerCallback()
{
    if (!ghsProcessor.isCapturingTone())
    {
        stopTimer();
        return;
    }

    juce::DynamicObject::Ptr payload = new juce::DynamicObject();
    payload->setProperty("seconds", ghsProcessor.getToneCaptureSeconds());
    webView->emitEventIfBrowserIsVisible("toneCaptureTick", juce::var(payload.get()));
}

// ============================== Resources ===================================

std::optional<juce::WebBrowserComponent::Resource> GHSFXCompanionEditor::getResource(const juce::String& url)
{
    auto path = url == "/" ? juce::String("index.html") : url.fromFirstOccurrenceOf("/", false, false);

    if (path == "index.html")
        return makeResource(BinaryData::index_html, BinaryData::index_htmlSize, "text/html");
    if (path == "style.css")
        return makeResource(BinaryData::style_css, BinaryData::style_cssSize, "text/css");
    if (path == "app.js")
        return makeResource(BinaryData::app_js, BinaryData::app_jsSize, "text/javascript");

    return std::nullopt;
}

// ============================== Native functions ============================

const juce::PluginDescription* GHSFXCompanionEditor::findKnownPluginByIdentifier(const juce::String& identifier)
{
    for (auto& desc : allScannedPlugins)
        if (desc.createIdentifierString() == identifier)
            return &desc;
    return nullptr;
}

void GHSFXCompanionEditor::emitToast(const juce::String& text, const juce::String& tone)
{
    juce::DynamicObject::Ptr payload = new juce::DynamicObject();
    payload->setProperty("text", text);
    payload->setProperty("tone", tone);
    webView->emitEventIfBrowserIsVisible("toast", juce::var(payload.get()));
}

juce::var GHSFXCompanionEditor::handleGetState()
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();

    juce::Array<juce::var> slots;
    for (int i = 0; i < GHSFXCompanionProcessor::maxChainSlots; ++i)
    {
        juce::DynamicObject::Ptr slot = new juce::DynamicObject();
        slot->setProperty("index", i);

        auto* plugin = ghsProcessor.getPluginInSlot(i);
        slot->setProperty("name", plugin != nullptr ? juce::var(plugin->getName()) : juce::var());
        slot->setProperty("format", plugin != nullptr
                                         ? juce::var(plugin->getPluginDescription().pluginFormatName)
                                         : juce::var());
        slot->setProperty("bypassed", ghsProcessor.getSlotBypassParameter(i)->get());

        slots.add(juce::var(slot.get()));
    }
    obj->setProperty("slots", slots);

    obj->setProperty("presets", toVarArray(ghsProcessor.getChainPresetNames()));

    obj->setProperty("scanCount", allScannedPlugins.size());
    auto lastScan = GHSPluginScanning::getLastScanTime();
    obj->setProperty("scanAgo", lastScan != juce::Time()
                                     ? juce::var((juce::Time::getCurrentTime() - lastScan).getApproximateDescription())
                                     : juce::var(juce::String()));

    obj->setProperty("capturing", ghsProcessor.isCapturingTone());
    obj->setProperty("captureSeconds", ghsProcessor.getToneCaptureSeconds());

    return juce::var(obj.get());
}

juce::var GHSFXCompanionEditor::handleSearchPlugins(const juce::Array<juce::var>& args)
{
    const juce::String query = args.size() > 0 ? args[0].toString() : juce::String();

    juce::Array<juce::var> results;
    for (auto& desc : allScannedPlugins)
    {
        bool matches = query.isEmpty()
                       || desc.name.containsIgnoreCase(query)
                       || desc.manufacturerName.containsIgnoreCase(query);

        auto* dbEntry = GHSPluginDatabase::lookupPlugin(desc.name);
        if (!matches && dbEntry != nullptr)
        {
            matches = dbEntry->category.containsIgnoreCase(query)
                      || dbEntry->subcategory.containsIgnoreCase(query)
                      || dbEntry->tags.containsIgnoreCase(query);
        }

        if (!matches)
            continue;

        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("identifier", desc.createIdentifierString());
        obj->setProperty("name", desc.name);
        obj->setProperty("manufacturer", desc.manufacturerName);
        obj->setProperty("format", desc.pluginFormatName);
        obj->setProperty("category", dbEntry != nullptr ? juce::var(dbEntry->category) : juce::var(juce::String()));
        results.add(juce::var(obj.get()));

        if (results.size() >= 200) // bound the payload for a very large scanned library
            break;
    }

    return juce::var(results);
}

void GHSFXCompanionEditor::handleLoadPluginIntoSlot(const juce::Array<juce::var>& args,
                                                      juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    if (args.size() < 2)
    {
        completion(false);
        return;
    }

    const int slotIndex = (int) args[0];
    const auto identifier = args[1].toString();

    auto* desc = findKnownPluginByIdentifier(identifier);
    if (desc == nullptr)
    {
        juce::DynamicObject::Ptr result = new juce::DynamicObject();
        result->setProperty("success", false);
        result->setProperty("error", "Plugin not found in the scanned list.");
        completion(juce::var(result.get()));
        return;
    }

    ghsProcessor.loadPluginIntoSlot(slotIndex, *desc, [completion](const juce::String& error)
    {
        juce::DynamicObject::Ptr result = new juce::DynamicObject();
        result->setProperty("success", error.isEmpty());
        result->setProperty("error", error);
        completion(juce::var(result.get()));
    });
}

juce::var GHSFXCompanionEditor::handleUnloadSlot(const juce::Array<juce::var>& args)
{
    if (args.size() < 1)
        return {};

    ghsProcessor.unloadSlot((int) args[0]);
    return {};
}

juce::var GHSFXCompanionEditor::handleMoveSlot(const juce::Array<juce::var>& args)
{
    if (args.size() < 2)
        return {};

    ghsProcessor.moveSlot((int) args[0], (int) args[1]);
    return {};
}

juce::var GHSFXCompanionEditor::handleToggleBypass(const juce::Array<juce::var>& args)
{
    if (args.size() < 1)
        return {};

    auto* param = ghsProcessor.getSlotBypassParameter((int) args[0]);
    *param = !param->get();
    return {};
}

juce::var GHSFXCompanionEditor::handleRefreshPluginScan()
{
    allScannedPlugins = ghsProcessor.loadKnownPlugins();
    std::sort(allScannedPlugins.begin(), allScannedPlugins.end(),
              [](const juce::PluginDescription& a, const juce::PluginDescription& b)
              {
                  return a.name.compareIgnoreCase(b.name) < 0;
              });

    juce::DynamicObject::Ptr result = new juce::DynamicObject();
    result->setProperty("count", allScannedPlugins.size());
    return juce::var(result.get());
}

void GHSFXCompanionEditor::handleSavePreset(const juce::Array<juce::var>& args,
                                             juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    const bool success = args.size() > 0 && ghsProcessor.saveChainPreset(args[0].toString());

    juce::DynamicObject::Ptr result = new juce::DynamicObject();
    result->setProperty("success", success);
    completion(juce::var(result.get()));
}

void GHSFXCompanionEditor::handleLoadPreset(const juce::Array<juce::var>& args,
                                             juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    if (args.size() < 1)
    {
        completion(false);
        return;
    }

    ghsProcessor.loadChainPreset(args[0].toString(), [completion]
    {
        juce::DynamicObject::Ptr result = new juce::DynamicObject();
        result->setProperty("success", true);
        completion(juce::var(result.get()));
    });
}

juce::var GHSFXCompanionEditor::handleDeletePreset(const juce::Array<juce::var>& args)
{
    const bool success = args.size() > 0 && ghsProcessor.deleteChainPreset(args[0].toString());

    juce::DynamicObject::Ptr result = new juce::DynamicObject();
    result->setProperty("success", success);
    return juce::var(result.get());
}

void GHSFXCompanionEditor::handleImportRecipe()
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

            auto matches = GHSRecipeImport::loadAndMatch(file, allScannedPlugins, GHSFXCompanionProcessor::maxChainSlots);

            if (matches.isEmpty())
            {
                emitToast("Couldn't read a recipe from \"" + file.getFileName() + "\".", "error");
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
                emitToast("None of that recipe's plugins were found in your library.", "error");
                return;
            }

            auto remaining = std::make_shared<int>(matchedCount);
            for (int slotIndex = 0; slotIndex < matches.size(); ++slotIndex)
            {
                auto& m = matches.getReference(slotIndex);
                if (!m.matched)
                    continue;

                ghsProcessor.loadPluginIntoSlot(slotIndex, m.description,
                    [this, remaining, unmatchedNames, matchedCount](const juce::String&)
                    {
                        if (--(*remaining) <= 0)
                        {
                            juce::DynamicObject::Ptr payload = new juce::DynamicObject();
                            payload->setProperty("matchedCount", matchedCount);
                            payload->setProperty("unmatchedNames", toVarArray(*unmatchedNames));
                            webView->emitEventIfBrowserIsVisible("recipeImported", juce::var(payload.get()));
                        }
                    });
            }
        });
}

void GHSFXCompanionEditor::handleStartToneCapture()
{
    ghsProcessor.startToneCapture();
    startTimer(200);
}

void GHSFXCompanionEditor::handleStopToneCaptureAndAnalyze(juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    stopTimer();

    ghsProcessor.stopToneCaptureAndAnalyze(
        [this, completion](std::vector<GHSToneRecommendation::SuggestedStage> stages, juce::String nearestVibeLabel)
        {
            // Same "next empty slot in order" assignment the old auto-load used -
            // computed here so the frontend's Load/Load All buttons can just pass
            // the target slot straight back into loadPluginIntoSlot.
            juce::Array<int> emptySlots;
            for (int i = 0; i < GHSFXCompanionProcessor::maxChainSlots; ++i)
                if (ghsProcessor.getPluginInSlot(i) == nullptr)
                    emptySlots.add(i);

            int slotCursor = 0;
            juce::Array<juce::var> stagesVar;

            for (auto& stage : stages)
            {
                juce::DynamicObject::Ptr stageObj = new juce::DynamicObject();
                stageObj->setProperty("name", stage.name);
                stageObj->setProperty("role", stage.role);
                stageObj->setProperty("note", stage.note);

                juce::Array<juce::var> optionsVar;
                bool hasOwned = false;
                for (auto& opt : stage.options)
                {
                    juce::DynamicObject::Ptr optObj = new juce::DynamicObject();
                    optObj->setProperty("brand", opt.brand);
                    optObj->setProperty("plugin", opt.plugin);
                    optObj->setProperty("tip", opt.tip);
                    optObj->setProperty("owned", opt.owned);
                    if (opt.owned)
                    {
                        optObj->setProperty("identifier", opt.description.createIdentifierString());
                        hasOwned = true;
                    }
                    optionsVar.add(juce::var(optObj.get()));
                }
                stageObj->setProperty("options", optionsVar);
                stageObj->setProperty("targetSlotIndex", (hasOwned && slotCursor < emptySlots.size())
                                                              ? juce::var(emptySlots[slotCursor++])
                                                              : juce::var());

                stagesVar.add(juce::var(stageObj.get()));
            }

            juce::DynamicObject::Ptr payload = new juce::DynamicObject();
            payload->setProperty("nearestVibe", nearestVibeLabel);
            payload->setProperty("stages", stagesVar);
            completion(juce::var(payload.get()));
        });
}

void GHSFXCompanionEditor::handleOpenHostedEditor(const juce::Array<juce::var>& args)
{
    if (args.size() < 1)
        return;

    const int slotIndex = (int) args[0];
    auto* hosted = ghsProcessor.getPluginInSlot(slotIndex);
    if (hosted == nullptr)
        return;

    auto* editorComponent = hosted->createEditorIfNeeded();
    if (editorComponent == nullptr)
        return;

    hostedEditorWindow = std::make_unique<HostedEditorWindow>(hosted->getName(), [this] { hostedEditorWindow.reset(); });
    hostedEditorWindow->setUsingNativeTitleBar(true);
    hostedEditorWindow->setContentOwned(editorComponent, true);
    hostedEditorWindow->setResizable(false, false);
    hostedEditorWindow->setVisible(true);
    hostedEditorWindow->centreAroundComponent(this, editorComponent->getWidth(), editorComponent->getHeight());
}
