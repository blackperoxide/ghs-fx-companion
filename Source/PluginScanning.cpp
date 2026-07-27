#include "PluginScanning.h"
#include <map>

namespace
{
    constexpr int kMaxConcurrentScans = 4;
    constexpr int kScanTimeoutMs = 15000;

    juce::String makeManifestKey(const juce::String& formatName, const juce::String& fileOrIdentifier)
    {
        return formatName + "|" + fileOrIdentifier;
    }

    // Not every fileOrIdentifier is a real path on disk (some AU identifiers aren't),
    // in which case there's nothing to compare against and it's always rescanned.
    juce::int64 currentModTimeMs(const juce::String& fileOrIdentifier)
    {
        juce::File f(fileOrIdentifier);
        if (f.exists())
            return f.getLastModificationTime().toMilliseconds();
        return 0;
    }

    struct ManifestEntry
    {
        juce::int64 modTimeMs = 0;
        bool success = false;
    };

    using Manifest = std::map<juce::String, ManifestEntry>;

    Manifest loadManifest(const juce::File& manifestFile)
    {
        Manifest manifest;
        auto xml = juce::XmlDocument::parse(manifestFile);
        if (xml == nullptr)
            return manifest;

        for (auto* entry : xml->getChildIterator())
        {
            if (!entry->hasTagName("ENTRY"))
                continue;

            ManifestEntry e;
            e.modTimeMs = entry->getStringAttribute("mtime").getLargeIntValue();
            e.success = entry->getBoolAttribute("success");
            manifest[entry->getStringAttribute("key")] = e;
        }
        return manifest;
    }

    void saveManifest(const juce::File& manifestFile, const Manifest& manifest)
    {
        juce::XmlElement root("SCANMANIFEST");
        for (auto& [key, entry] : manifest)
        {
            auto* e = root.createNewChildElement("ENTRY");
            e->setAttribute("key", key);
            e->setAttribute("mtime", juce::String(entry.modTimeMs));
            e->setAttribute("success", entry.success);
        }
        root.writeTo(manifestFile);
    }

    struct WorkItem
    {
        juce::String formatName;
        juce::String fileOrIdentifier;
        juce::String key;
        juce::int64 modTimeMs = 0;
    };
}

juce::File GHSPluginScanning::getCacheFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("GHS FX Companion")
        .getChildFile("KnownPlugins.xml");
}

juce::File GHSPluginScanning::getManifestFile()
{
    return getCacheFile().getSiblingFile("ScanManifest.xml");
}

juce::Time GHSPluginScanning::getLastScanTime()
{
    auto cacheFile = getCacheFile();
    if (!cacheFile.existsAsFile())
        return {};
    return cacheFile.getLastModificationTime();
}

void GHSPluginScanning::performFullScanAndSaveCache(juce::AudioPluginFormatManager& formatManager,
                                                     const juce::String& scannerExecutablePath,
                                                     const ProgressCallback& onProgress)
{
    auto cacheFile = getCacheFile();
    auto manifestFile = getManifestFile();
    cacheFile.getParentDirectory().createDirectory();

    // Whatever was already known, so unchanged plugins can be carried forward
    // instead of rescanned.
    juce::KnownPluginList previousList;
    loadCachedList(previousList);
    auto previousTypes = previousList.getTypes();

    auto manifest = loadManifest(manifestFile);
    juce::KnownPluginList knownPlugins;

    juce::Array<WorkItem> toScan;
    int totalConsidered = 0;
    int skippedUnchanged = 0;

    for (auto* format : formatManager.getFormats())
    {
        juce::FileSearchPath searchPath = format->getDefaultLocationsToSearch();
        auto filesOrIdentifiers = format->searchPathsForPlugins(searchPath, true, true);

        for (auto& fileOrIdentifier : filesOrIdentifiers)
        {
            ++totalConsidered;

            WorkItem item;
            item.formatName = format->getName();
            item.fileOrIdentifier = fileOrIdentifier;
            item.key = makeManifestKey(item.formatName, fileOrIdentifier);
            item.modTimeMs = currentModTimeMs(fileOrIdentifier);

            auto existing = manifest.find(item.key);
            const bool unchanged = existing != manifest.end()
                                    && existing->second.success
                                    && item.modTimeMs != 0
                                    && existing->second.modTimeMs == item.modTimeMs;

            if (unchanged)
            {
                for (auto& desc : previousTypes)
                    if (desc.fileOrIdentifier == fileOrIdentifier && desc.pluginFormatName == item.formatName)
                        knownPlugins.addType(desc);

                ++skippedUnchanged;
                continue;
            }

            toScan.add(item);
        }
    }

    juce::Logger::writeToLog(juce::String(totalConsidered) + " plugin(s) found, "
                              + juce::String(skippedUnchanged) + " unchanged since last scan (skipped), "
                              + juce::String(toScan.size()) + " to (re)scan.");

    // Save the carried-forward entries now, in case toScan is empty or we get
    // killed before the first batch finishes.
    if (auto xml = knownPlugins.createXml())
        xml->writeTo(cacheFile);

    struct ActiveScan
    {
        WorkItem item;
        juce::ChildProcess child;
        juce::uint32 startTimeMs = 0;
    };

    std::vector<std::unique_ptr<ActiveScan>> active;
    int nextIndex = 0;
    int completed = 0;
    const int totalToScan = toScan.size();

    if (onProgress)
        onProgress(0, totalToScan, {});

    while (nextIndex < toScan.size() || !active.empty())
    {
        // Top up the pool - run several scans at once instead of one at a time.
        while ((int) active.size() < kMaxConcurrentScans && nextIndex < toScan.size())
        {
            auto scan = std::make_unique<ActiveScan>();
            scan->item = toScan.getReference(nextIndex++);

            juce::StringArray args { scannerExecutablePath, "--scan-one", scan->item.formatName, scan->item.fileOrIdentifier };

            if (scan->child.start(args))
            {
                scan->startTimeMs = juce::Time::getMillisecondCounter();
                active.push_back(std::move(scan));
            }
            else
            {
                juce::Logger::writeToLog("Failed to launch scanner child for: " + scan->item.fileOrIdentifier);
                manifest[scan->item.key] = { scan->item.modTimeMs, false };
                ++completed;
                if (onProgress)
                    onProgress(completed, totalToScan, scan->item.fileOrIdentifier);
            }
        }

        // Harvest anything finished (or stuck) since the last pass.
        for (int i = (int) active.size() - 1; i >= 0; --i)
        {
            auto& scan = *active[(size_t) i];
            const bool timedOut = (juce::Time::getMillisecondCounter() - scan.startTimeMs) > (juce::uint32) kScanTimeoutMs;

            if (scan.child.isRunning() && !timedOut)
                continue;

            if (timedOut && scan.child.isRunning())
            {
                scan.child.kill();
                juce::Logger::writeToLog("Timed out, skipping: " + scan.item.fileOrIdentifier);
                manifest[scan.item.key] = { scan.item.modTimeMs, false };
            }
            else
            {
                auto output = scan.child.readAllProcessOutput();
                scan.child.waitForProcessToFinish(0);

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

                manifest[scan.item.key] = { scan.item.modTimeMs, foundInThisFile > 0 };

                if (foundInThisFile > 0)
                    juce::Logger::writeToLog("Scanned: " + scan.item.fileOrIdentifier);
                else
                    juce::Logger::writeToLog("Skipped (crashed or no plugin found): " + scan.item.fileOrIdentifier);
            }

            ++completed;
            if (onProgress)
                onProgress(completed, totalToScan, scan.item.fileOrIdentifier);

            // Save after every file - a crash later in the scan should never lose
            // everything found before it.
            if (auto xml = knownPlugins.createXml())
                xml->writeTo(cacheFile);
            saveManifest(manifestFile, manifest);

            active.erase(active.begin() + i);
        }

        if (!active.empty())
            juce::Thread::sleep(20);
    }

    // Guaranteed final write - covers zero plugins found, and zero plugins needing
    // (re)scanning this run.
    if (auto xml = knownPlugins.createXml())
        xml->writeTo(cacheFile);
    saveManifest(manifestFile, manifest);

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
