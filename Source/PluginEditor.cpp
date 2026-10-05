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
    addRiffHouseFunctions(options);
    addVisualsFunctions(options);

    webView = std::make_unique<SinglePageBrowser>(options);
    addAndMakeVisible(*webView);
    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable(true, true);
    setSize(1100, 720);
    startTimerHz(30);
}

GHSFXCompanionEditor::~GHSFXCompanionEditor()
{
    hostedEditorWindow.reset();
    if (visualsWindow != nullptr)
        visualsWindow->clearContentComponent(); // non-owned - don't let its destructor touch webView
    visualsWindow.reset();
}

void GHSFXCompanionEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void GHSFXCompanionEditor::resized()
{
    // Fullscreen owns the webview's bounds while active - don't fight it.
    if (webView != nullptr && !visualsFullscreen)
        webView->setBounds(getLocalBounds());
}

void GHSFXCompanionEditor::timerCallback()
{
    webView->emitEventIfBrowserIsVisible("rhLive", ghsProcessor.getRiffHouse().getLiveState());

    // A MIDI-learned scene trigger lands here (never written from the audio thread).
    if (const int pending = ghsProcessor.getVisuals().consumePendingSceneIndex(); pending >= 0)
        *ghsProcessor.getVisualSceneParameter() = pending;
    webView->emitEventIfBrowserIsVisible("vzLive", ghsProcessor.getVisuals().getLiveState());

    if (!ghsProcessor.isCapturingTone() || ++toneTickDivider % 6 != 0)
        return;

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
    if (path == "riffhouse.js")
        return makeResource(BinaryData::riffhouse_js, BinaryData::riffhouse_jsSize, "text/javascript");
    if (path == "riffhouse.css")
        return makeResource(BinaryData::riffhouse_css, BinaryData::riffhouse_cssSize, "text/css");
    if (path == "visuals.js")
        return makeResource(BinaryData::visuals_js, BinaryData::visuals_jsSize, "text/javascript");
    if (path == "visuals.css")
        return makeResource(BinaryData::visuals_css, BinaryData::visuals_cssSize, "text/css");

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
}

void GHSFXCompanionEditor::handleStopToneCaptureAndAnalyze(juce::WebBrowserComponent::NativeFunctionCompletion completion)
{

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

// ============================== Riff House ==================================

juce::WebBrowserComponent::Options& GHSFXCompanionEditor::addRiffHouseFunctions(juce::WebBrowserComponent::Options& options)
{
    using Args = const juce::Array<juce::var>&;
    using Done = juce::WebBrowserComponent::NativeFunctionCompletion;
    auto& rh = ghsProcessor.getRiffHouse();
    auto arg = [](Args a, int i, juce::var fallback) { return i < a.size() ? a[i] : fallback; };

    options = options
        .withNativeFunction("rhSaveThat", [&rh, arg](Args a, Done done) { done(rh.saveThat((double) arg(a, 0, 60.0), (double) arg(a, 1, 90.0))); })
        .withNativeFunction("rhCaptureWave", [&rh, arg](Args a, Done done) { done(rh.getCaptureWave((int) arg(a, 0, 800))); })
        .withNativeFunction("rhExportLoop", [&rh, arg](Args a, Done done) { done(rh.exportLoop(arg(a, 0, 0.0), arg(a, 1, 0.0), arg(a, 2, 90.0))); })
        .withNativeFunction("rhExportSlices", [&rh, arg](Args a, Done done) { done(rh.exportSlices(arg(a, 0, 0.0), arg(a, 1, 0.0), arg(a, 2, 5.0), arg(a, 3, 90.0))); })
        .withNativeFunction("rhListCharts", [&rh](Args, Done done) { done(rh.listCharts()); })
        .withNativeFunction("rhLoadChart", [&rh, arg](Args a, Done done) { rh.loadChartAsync(arg(a, 0, "").toString(), (bool) arg(a, 1, false), (double) arg(a, 2, 1.0), done); })
        .withNativeFunction("rhTransport", [&rh, arg](Args a, Done done) { rh.setTransport((bool) arg(a, 0, false), (double) arg(a, 1, 0.0), (float) (double) arg(a, 2, 0.8)); done({}); })
        .withNativeFunction("rhSetRate", [&rh, arg](Args a, Done done) { rh.setRateAsync((double) arg(a, 0, 1.0), [done] { done({}); }); })
        .withNativeFunction("rhMonitorExternally", [&rh, arg](Args a, Done done) { rh.setMonitorExternally((bool) arg(a, 0, false)); done({}); })
        .withNativeFunction("rhSetParam", [this, arg](Args a, Done done)
        {
            // hardware knob -> hosted plugin parameter (MIDI learn lives in the UI)
            if (auto* p = ghsProcessor.getPluginInSlot((int) arg(a, 0, 0)))
            {
                auto params = p->getParameters();
                const int idx = (int) arg(a, 1, 0);
                if (juce::isPositiveAndBelow(idx, params.size()))
                    params[idx]->setValueNotifyingHost((float) (double) arg(a, 2, 0.0));
            }
            done({});
        })
        .withNativeFunction("rhParamList", [this, arg](Args a, Done done)
        {
            juce::Array<juce::var> names;
            if (auto* p = ghsProcessor.getPluginInSlot((int) arg(a, 0, 0)))
                for (auto* prm : p->getParameters()) names.add(prm->getName(40));
            done(names);
        })
        .withNativeFunction("rhRevealInbox", [](Args, Done done) { RiffHouse::inboxFolder().startAsProcess(); done({}); })
        .withNativeFunction("rhImport", [this](Args a, Done done)
        {
            // kind: "file" (audio or MIDI) or "folder" (song folder from tools/riffhouse_import.py)
            const auto kind = a.size() > 0 ? a[0].toString() : juce::String("file");
            const auto instrument = a.size() > 1 ? a[1].toString() : juce::String("guitar");
            const bool folder = kind == "folder";
            riffFileChooser = std::make_unique<juce::FileChooser>(
                folder ? "Choose a Riff House song folder" : "Choose a song, stem or MIDI file",
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                folder ? juce::String() : juce::String("*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg;*.mid;*.midi"));
            const int flags = juce::FileBrowserComponent::openMode
                              | (folder ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles);
            riffFileChooser->launchAsync(flags, [this, done, folder, instrument](const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                auto& engine = ghsProcessor.getRiffHouse();
                if (f == juce::File()) { done({}); return; }
                if (folder) engine.importSongFolderAsync(f, done);
                else if (f.hasFileExtension("mid;midi")) done(engine.importMidi(f, instrument));
                else engine.importAudioAsync(f, instrument, done);
            });
        });
    return options;
}

// ============================== Visuals ======================================

juce::WebBrowserComponent::Options& GHSFXCompanionEditor::addVisualsFunctions(juce::WebBrowserComponent::Options& options)
{
    using Args = const juce::Array<juce::var>&;
    using Done = juce::WebBrowserComponent::NativeFunctionCompletion;

    options = options
        .withNativeFunction("vzGetParams", [this](Args, Done done) { done(handleVzGetParams()); })
        .withNativeFunction("vzSetParam", [this](Args a, Done done) { handleVzSetParam(a); done({}); })
        .withNativeFunction("vzMidiLearn", [this](Args a, Done done) { handleVzMidiLearn(a); done({}); })
        .withNativeFunction("vzClearBinding", [this](Args a, Done done) { handleVzClearBinding(a); done({}); })
        .withNativeFunction("vzEnterFullscreen", [this](Args, Done done) { handleVzEnterFullscreen(); done({}); })
        .withNativeFunction("vzExitFullscreen", [this](Args, Done done) { handleVzExitFullscreen(); done({}); });
    return options;
}

juce::var GHSFXCompanionEditor::handleVzGetParams()
{
    auto* o = new juce::DynamicObject();
    o->setProperty("intensity", (double) ghsProcessor.getVisualIntensityParameter()->get());
    o->setProperty("palette", ghsProcessor.getVisualPaletteParameter()->getIndex());
    o->setProperty("scene", ghsProcessor.getVisualSceneParameter()->getIndex());

    juce::Array<juce::var> paletteNames, sceneNames;
    for (auto& s : ghsProcessor.getVisualPaletteParameter()->choices) paletteNames.add(s);
    for (auto& s : ghsProcessor.getVisualSceneParameter()->choices) sceneNames.add(s);
    o->setProperty("paletteNames", paletteNames);
    o->setProperty("sceneNames", sceneNames);
    return juce::var(o);
}

void GHSFXCompanionEditor::handleVzSetParam(const juce::Array<juce::var>& args)
{
    if (args.size() < 2) return;
    const auto name = args[0].toString();
    if (name == "intensity") *ghsProcessor.getVisualIntensityParameter() = (float) (double) args[1];
    else if (name == "palette") *ghsProcessor.getVisualPaletteParameter() = (int) args[1];
    else if (name == "scene") *ghsProcessor.getVisualSceneParameter() = (int) args[1];
}

void GHSFXCompanionEditor::handleVzMidiLearn(const juce::Array<juce::var>& args)
{
    ghsProcessor.getVisuals().armMidiLearn(args.size() > 0 ? (int) args[0] : -1);
}

void GHSFXCompanionEditor::handleVzClearBinding(const juce::Array<juce::var>& args)
{
    if (args.size() > 0)
        ghsProcessor.getVisuals().clearBinding((int) args[0]);
}

void GHSFXCompanionEditor::handleVzEnterFullscreen()
{
    if (visualsFullscreen || webView == nullptr) return;
    visualsFullscreen = true;

    visualsWindow = std::make_unique<VisualsWindow>([this] { handleVzExitFullscreen(); });
    removeChildComponent(webView.get());
    visualsWindow->setContentNonOwned(webView.get(), false);
    visualsWindow->setUsingNativeTitleBar(false);
    visualsWindow->setResizable(false, false);

    // Prefer a non-primary display (the projector/second-monitor use case).
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* primary = displays.getPrimaryDisplay();
    const auto* target = primary;
    for (auto& d : displays.displays)
    {
        if (&d != primary) { target = &d; break; }
    }

    if (target != nullptr)
        visualsWindow->setBounds(target->totalArea);
    visualsWindow->setVisible(true);
    visualsWindow->setFullScreen(true);
}

void GHSFXCompanionEditor::handleVzExitFullscreen()
{
    if (!visualsFullscreen) return;
    visualsFullscreen = false;

    if (visualsWindow != nullptr)
        visualsWindow->clearContentComponent(); // non-owned - webView survives
    visualsWindow.reset();

    addAndMakeVisible(*webView);
    resized();
}
