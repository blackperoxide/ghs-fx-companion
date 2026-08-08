#include "ChainPresets.h"

namespace
{
    juce::File presetFileFor(const juce::String& name)
    {
        return GHSChainPresets::getPresetsDirectory().getChildFile(juce::File::createLegalFileName(name.trim()) + ".xml");
    }
}

juce::File GHSChainPresets::getPresetsDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("GHS FX Companion")
        .getChildFile("Presets");
}

juce::StringArray GHSChainPresets::listPresetNames()
{
    juce::StringArray names;

    auto dir = getPresetsDirectory();
    if (!dir.isDirectory())
        return names;

    for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.xml"))
        names.add(f.getFileNameWithoutExtension());

    names.sort(true);
    return names;
}

bool GHSChainPresets::savePreset(const juce::String& name, const juce::ValueTree& chainState)
{
    if (name.trim().isEmpty())
        return false;

    if (!getPresetsDirectory().createDirectory())
        return false;

    if (auto xml = chainState.createXml())
        return xml->writeTo(presetFileFor(name));

    return false;
}

juce::ValueTree GHSChainPresets::loadPreset(const juce::String& name)
{
    auto file = presetFileFor(name);
    if (!file.existsAsFile())
        return {};

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return {};

    return juce::ValueTree::fromXml(*xml);
}

bool GHSChainPresets::deletePreset(const juce::String& name)
{
    return presetFileFor(name).deleteFile();
}
