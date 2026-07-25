#include "PluginScanning.h"

juce::File GHSPluginScanning::getCacheFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("GHS FX Companion")
        .getChildFile("KnownPlugins.xml");
}

void GHSPluginScanning::performFullScanAndSaveCache(juce::AudioPluginFormatManager& formatManager)
{
    juce::KnownPluginList knownPlugins;

    for (auto* format : formatManager.getFormats())
    {
        juce::FileSearchPath searchPath = format->getDefaultLocationsToSearch();
        auto filesOrIdentifiers = format->searchPathsForPlugins(searchPath, true, true);

        for (auto& fileOrIdentifier : filesOrIdentifiers)
        {
            // This is the risky step (it briefly instantiates the plugin to query it) -
            // it's fine here because this whole executable IS the isolation boundary.
            // If a plugin crashes this process, it never touches Logic.
            juce::OwnedArray<juce::PluginDescription> typesFound;
            format->findAllTypesForFile(typesFound, fileOrIdentifier);

            for (auto* description : typesFound)
                knownPlugins.addType(*description);

            juce::Logger::writeToLog("Scanned: " + fileOrIdentifier);
        }
    }

    auto xml = knownPlugins.createXml();
    auto cacheFile = getCacheFile();
    cacheFile.getParentDirectory().createDirectory();
    xml->writeTo(cacheFile);

    juce::Logger::writeToLog("Wrote " + juce::String(knownPlugins.getNumTypes()) + " plugin(s) to "
                              + cacheFile.getFullPathName());
}

bool GHSPluginScanning::loadCachedList(juce::KnownPluginList& listToPopulate)
{
    auto cacheFile = getCacheFile();
    if (!cacheFile.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse(cacheFile);
    if (xml == nullptr)
        return false;

    listToPopulate.recreateFromXml(*xml);
    return true;
}
