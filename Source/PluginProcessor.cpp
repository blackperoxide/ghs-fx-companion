#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginScanning.h"

namespace
{
    bool isValidSlot(int slotIndex)
    {
        return juce::isPositiveAndBelow(slotIndex, GHSFXCompanionProcessor::maxChainSlots);
    }
}

GHSFXCompanionProcessor::GHSFXCompanionProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.addDefaultFormats();

    for (int i = 0; i < maxChainSlots; ++i)
    {
        auto* param = new juce::AudioParameterBool(
            { "slotBypass" + juce::String(i), 1 },
            "Slot " + juce::String(i + 1) + " Bypass",
            false);
        addParameter(param);
        chain[(size_t) i].bypassParam = param;
    }
}

GHSFXCompanionProcessor::~GHSFXCompanionProcessor()
{
    for (int i = 0; i < maxChainSlots; ++i)
        unloadSlot(i);
}

void GHSFXCompanionProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    for (auto& slot : chain)
    {
        if (slot.plugin != nullptr)
        {
            slot.plugin->setRateAndBufferSizeDetails(sampleRate, samplesPerBlock);
            slot.plugin->prepareToPlay(sampleRate, samplesPerBlock);
        }
    }
}

void GHSFXCompanionProcessor::releaseResources()
{
    for (auto& slot : chain)
        if (slot.plugin != nullptr)
            slot.plugin->releaseResources();
}

bool GHSFXCompanionProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    // Milestone 1: mono or stereo, input matching output. Covers DI/vocal/mono
    // tracks as well as stereo busses. Hosted plugins that change channel count
    // (e.g. mono-in/stereo-out widening) are a later problem.
    const bool isMonoToMono = mainIn == juce::AudioChannelSet::mono() && mainOut == juce::AudioChannelSet::mono();
    const bool isStereoToStereo = mainIn == juce::AudioChannelSet::stereo() && mainOut == juce::AudioChannelSet::stereo();

    return isMonoToMono || isStereoToStereo;
}

void GHSFXCompanionProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Same buffer threaded through every non-empty, non-bypassed slot in order -
    // that's the whole chain. Empty slots and bypassed slots are transparent.
    for (auto& slot : chain)
    {
        if (slot.plugin != nullptr && !slot.bypassParam->get())
            slot.plugin->processBlock(buffer, midiMessages);
    }
}

juce::AudioProcessorEditor* GHSFXCompanionProcessor::createEditor()
{
    return new GHSFXCompanionEditor(*this);
}

juce::Array<juce::PluginDescription> GHSFXCompanionProcessor::loadKnownPlugins()
{
    juce::Array<juce::PluginDescription> found;

    knownPlugins.clear();
    if (GHSPluginScanning::loadCachedList(knownPlugins))
        found = knownPlugins.getTypes();

    return found;
}

void GHSFXCompanionProcessor::loadPluginIntoSlot(int slotIndex,
                                                  const juce::PluginDescription& description,
                                                  std::function<void(const juce::String&)> onComplete)
{
    // Must run on the message thread - createPluginInstanceAsync requires it,
    // and the callback below is guaranteed to also land back on it.
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    jassert(isValidSlot(slotIndex));

    formatManager.createPluginInstanceAsync(
        description,
        currentSampleRate,
        currentBlockSize,
        [this, slotIndex, onComplete, pluginName = description.name](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (instance == nullptr)
            {
                if (onComplete)
                    onComplete(error.isNotEmpty() ? error : "Failed to load plugin");
                return;
            }

            auto& slot = chain[(size_t) slotIndex];

            unloadSlot(slotIndex);
            slot.plugin = std::move(instance);

            const auto requiredIns = getMainBusNumInputChannels();
            const auto requiredOuts = getMainBusNumOutputChannels();

            // setPlayConfigDetails() only jassert()s on failure - a no-op in release
            // builds - so a plugin that can't actually match our bus layout (e.g. a
            // stereo-only plugin hosted on a mono track) would otherwise be left half
            // negotiated and silently used anyway, feeding it a buffer with fewer
            // channels than it thinks it has. Confirmed this is what let a stereo-only
            // plugin get "loaded" onto a mono track and then crash creating its editor.
            slot.plugin->setPlayConfigDetails(requiredIns, requiredOuts, currentSampleRate, currentBlockSize);

            if (slot.plugin->getTotalNumInputChannels() != requiredIns
                || slot.plugin->getTotalNumOutputChannels() != requiredOuts)
            {
                slot.plugin.reset();

                if (onComplete)
                    onComplete(pluginName + " doesn't support "
                               + (requiredIns == 1 ? juce::String("mono") : juce::String("stereo"))
                               + " tracks.");
                return;
            }

            slot.plugin->prepareToPlay(currentSampleRate, currentBlockSize);

            if (onComplete)
                onComplete({});
        });
}

void GHSFXCompanionProcessor::unloadSlot(int slotIndex)
{
    jassert(isValidSlot(slotIndex));
    auto& slot = chain[(size_t) slotIndex];

    if (slot.plugin != nullptr)
    {
        slot.plugin->releaseResources();
        slot.plugin.reset();
    }
}

void GHSFXCompanionProcessor::moveSlot(int fromIndex, int toIndex)
{
    jassert(isValidSlot(fromIndex) && isValidSlot(toIndex));
    if (fromIndex == toIndex)
        return;

    auto& from = chain[(size_t) fromIndex];
    auto& to = chain[(size_t) toIndex];

    std::swap(from.plugin, to.plugin);

    // The bypass parameters themselves keep a fixed identity per slot index (hosts
    // rely on that), so swap the values they hold rather than the parameter objects.
    const bool fromBypassed = from.bypassParam->get();
    *from.bypassParam = to.bypassParam->get();
    *to.bypassParam = fromBypassed;
}

juce::AudioPluginInstance* GHSFXCompanionProcessor::getPluginInSlot(int slotIndex) const
{
    jassert(isValidSlot(slotIndex));
    return chain[(size_t) slotIndex].plugin.get();
}

juce::AudioParameterBool* GHSFXCompanionProcessor::getSlotBypassParameter(int slotIndex) const
{
    jassert(isValidSlot(slotIndex));
    return chain[(size_t) slotIndex].bypassParam;
}

void GHSFXCompanionProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("GHSFXCompanionState");

    for (int i = 0; i < maxChainSlots; ++i)
    {
        auto& slot = chain[(size_t) i];
        if (slot.plugin == nullptr)
            continue;

        juce::ValueTree slotState("Slot");
        slotState.setProperty("index", i, nullptr);
        slotState.setProperty("identifier", slot.plugin->getPluginDescription().createIdentifierString(), nullptr);
        slotState.setProperty("bypass", slot.bypassParam->get(), nullptr);

        juce::MemoryBlock hostedState;
        slot.plugin->getStateInformation(hostedState);
        slotState.setProperty("pluginState", hostedState.toBase64Encoding(), nullptr);

        state.addChild(slotState, -1, nullptr);
    }

    juce::MemoryOutputStream stream(destData, true);
    state.writeToStream(stream);
}

void GHSFXCompanionProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData(data, (size_t) sizeInBytes);
    if (!state.isValid())
        return;

    auto known = loadKnownPlugins();

    for (const auto& slotState : state)
    {
        const int slotIndex = slotState.getProperty("index", -1);
        if (!isValidSlot(slotIndex))
            continue;

        const auto identifier = slotState.getProperty("identifier").toString();
        if (identifier.isEmpty())
            continue;

        const bool bypass = slotState.getProperty("bypass", false);
        auto& slot = chain[(size_t) slotIndex];
        slot.pendingState.fromBase64Encoding(slotState.getProperty("pluginState").toString());

        // Match by identifier string against the cached list, then reload with the saved state.
        // (Reopening a saved Logic session needs this to bring every hosted plugin back.)
        for (auto& description : known)
        {
            if (description.createIdentifierString() != identifier)
                continue;

            loadPluginIntoSlot(slotIndex, description, [this, slotIndex, bypass](const juce::String& error)
            {
                auto& s = chain[(size_t) slotIndex];
                if (error.isEmpty() && s.plugin != nullptr && s.pendingState.getSize() > 0)
                    s.plugin->setStateInformation(s.pendingState.getData(), (int) s.pendingState.getSize());

                *s.bypassParam = bypass;
            });
            break;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GHSFXCompanionProcessor();
}
