#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginScanning.h"

GHSFXCompanionProcessor::GHSFXCompanionProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.addDefaultFormats();
    addParameter(bypassParam = new juce::AudioParameterBool("bypass", "Bypass Hosted Plugin", false));
}

GHSFXCompanionProcessor::~GHSFXCompanionProcessor()
{
    unloadHostedPlugin();
}

void GHSFXCompanionProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    if (hostedPlugin != nullptr)
    {
        hostedPlugin->setRateAndBufferSizeDetails(sampleRate, samplesPerBlock);
        hostedPlugin->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void GHSFXCompanionProcessor::releaseResources()
{
    if (hostedPlugin != nullptr)
        hostedPlugin->releaseResources();
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

    if (hostedPlugin != nullptr && !bypassParam->get())
    {
        hostedPlugin->processBlock(buffer, midiMessages);
    }
    // else: pass audio through untouched (buffer already holds the input).
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

void GHSFXCompanionProcessor::loadHostedPlugin(const juce::PluginDescription& description,
                                                std::function<void(const juce::String&)> onComplete)
{
    // Must run on the message thread - createPluginInstanceAsync requires it,
    // and the callback below is guaranteed to also land back on it.
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    formatManager.createPluginInstanceAsync(
        description,
        currentSampleRate,
        currentBlockSize,
        [this, onComplete](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (instance == nullptr)
            {
                if (onComplete)
                    onComplete(error.isNotEmpty() ? error : "Failed to load plugin");
                return;
            }

            unloadHostedPlugin();

            hostedPlugin = std::move(instance);
            hostedPlugin->setPlayConfigDetails(getMainBusNumInputChannels(),
                                                getMainBusNumOutputChannels(),
                                                currentSampleRate,
                                                currentBlockSize);
            hostedPlugin->prepareToPlay(currentSampleRate, currentBlockSize);

            if (onComplete)
                onComplete({});
        });
}

void GHSFXCompanionProcessor::unloadHostedPlugin()
{
    if (hostedPlugin != nullptr)
    {
        hostedPlugin->releaseResources();
        hostedPlugin.reset();
    }
}

void GHSFXCompanionProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("GHSFXCompanionState");
    state.setProperty("bypass", bypassParam->get(), nullptr);

    if (hostedPlugin != nullptr)
    {
        state.setProperty("hostedPluginIdentifier", hostedPlugin->getPluginDescription().createIdentifierString(), nullptr);

        juce::MemoryBlock hostedState;
        hostedPlugin->getStateInformation(hostedState);
        state.setProperty("hostedPluginState", hostedState.toBase64Encoding(), nullptr);
    }

    juce::MemoryOutputStream stream(destData, true);
    state.writeToStream(stream);
}

void GHSFXCompanionProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData(data, (size_t) sizeInBytes);
    if (!state.isValid())
        return;

    if (state.hasProperty("bypass"))
        *bypassParam = (bool) state.getProperty("bypass");

    auto identifier = state.getProperty("hostedPluginIdentifier").toString();
    if (identifier.isEmpty())
        return;

    juce::String base64State = state.getProperty("hostedPluginState").toString();
    pendingHostedPluginState.fromBase64Encoding(base64State);

    // Match by identifier string against the cached list, then reload with the saved state.
    // (Reopening a saved Logic session needs this to bring the hosted plugin back.)
    for (auto& description : loadKnownPlugins())
    {
        if (description.createIdentifierString() == identifier)
        {
            loadHostedPlugin(description, [this](const juce::String& error)
            {
                if (error.isEmpty() && hostedPlugin != nullptr && pendingHostedPluginState.getSize() > 0)
                {
                    hostedPlugin->setStateInformation(pendingHostedPluginState.getData(),
                                                       (int) pendingHostedPluginState.getSize());
                }
            });
            break;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GHSFXCompanionProcessor();
}
