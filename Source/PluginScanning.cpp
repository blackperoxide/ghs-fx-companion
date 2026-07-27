#include "PluginScanning.h"

juce::File GHSPluginScanning::getCacheFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("GHS FX Companion")
        .getChildFile("KnownPlugins.xml");
}

void GHSPluginScanning::performFullScanAndSaveCache(juce::AudioPluginFormatManager& formatManager,
                                                     const juce::String& scannerExecutablePath)
{
    juce::KnownPluginList knownPlugins;
    auto cacheFile = getCacheFile();
    cacheFile.getParentDirectory().createDirectory();

    for (auto* format : formatManager.getFormats())
    {
        juce::FileSearchPath searchPath = format->getDefaultLocationsToSearch();
        auto filesOrIdentifiers = format->searchPathsForPlugins(searchPath, true, true);

        for (auto& fileOrIdentifier : filesOrIdentifiers)
        {
            // The actual instantiate-to-query step happens in a throwaway child process,
            // not here - that's what survives a crashing plugin without losing the scan.
            juce::ChildProcess child;
            juce::StringArray args { scannerExecutablePath, "--scan-one", format->getName(), fileOrIdentifier };

            if (child.start(args))
            {
                const bool finished = child.waitForProcessToFinish(15000);
                auto output = child.readAllProcessOutput();

                if (!finished)
                {
                    child.kill();
                    juce::Logger::writeToLog("Timed out, skipping: " + fileOrIdentifier);
                }
                else
                {
                    int foundInThisFile = 0;
                    for (auto& line : juce::StringArray::fromLines(output))
                    {
                        if (!line.startsWith("PLUGIN_XML:"))
                            continue;

                        if (auto xml = juce::XmlDocument::parse(line.substring(11)))
                        {
                            juce::PluginDescription desc;
                            if (desc.loadFromXml(*xml))
                            {
                                knownPlugins.addType(desc);
                                ++foundInThisFile;
                            }
                        }
                    }

                    if (foundInThisFile > 0)
                        juce::Logger::writeToLog("Scanned: " + fileOrIdentifier);
                    else
                        juce::Logger::writeToLog("Skipped (crashed or no plugin found): " + fileOrIdentifier);
                }
            }
            else
            {
                juce::Logger::writeToLog("Failed to launch scanner child for: " + fileOrIdentifier);
            }

            // Save after every file - a crash later in the scan should never lose
            // everything found before it.
            if (auto xml = knownPlugins.createXml())
                xml->writeTo(cacheFile);
        }
    }

    // Guaranteed final write - the loop above only writes when it finds at least one
    // file to scan, so this covers the (rare, but real) case of zero plugins found.
    if (auto xml = knownPlugins.createXml())
        xml->writeTo(cacheFile);

    juce::Logger::writeToLog("Wrote " + juce::String(knownPlugins.getNumTypes()) + " plugin(s) to "
                              + cacheFile.getFullPathName());
}

void GHSPluginScanning::scanOneAndPrintResult(juce::AudioPluginFormatManager& formatManager,
                                               const juce::String& formatName,
                                               const juce::String& fileOrIdentifier)
{
    for (auto* format : formatManager.getFormats())
    {
        if (format->getName() != formatName)
            continue;

        juce::OwnedArray<juce::PluginDescription> typesFound;
        format->findAllTypesForFile(typesFound, fileOrIdentifier);

        for (auto* description : typesFound)
        {
            if (auto xml = description->createXml())
                std::cout << "PLUGIN_XML:" << xml->toString(juce::XmlElement::TextFormat().singleLine()) << std::endl;
        }

        return;
    }
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
