#include "StudioEngine.h"
#include <algorithm>

namespace Studio
{
// ============================================================ folders
juce::File rootFolder()
{
    auto f = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("GHS").getChildFile("Studio");
    f.createDirectory();
    return f;
}
juce::File dropFolder()     { auto f = rootFolder().getChildFile("Drop");     f.createDirectory(); return f; }
juce::File songsFolder()    { auto f = rootFolder().getChildFile("Songs");    f.createDirectory(); return f; }
juce::File sessionsFolder() { auto f = rootFolder().getChildFile("Sessions"); f.createDirectory(); return f; }

namespace
{
    juce::var makeError(const juce::String& message)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("ok", false);
        o->setProperty("error", message);
        return juce::var(o);
    }

    juce::String nowIso() { return juce::Time::getCurrentTime().toISO8601(true); }

    bool nameHasExt(const juce::String& name, std::initializer_list<const char*> exts)
    {
        for (auto* e : exts)
            if (name.endsWithIgnoreCase(juce::String(".") + e))
                return true;
        return false;
    }
    bool isAudioName(const juce::String& name) { return nameHasExt(name, { "wav", "aif", "aiff", "flac", "mp3", "m4a", "ogg" }); }
    bool isMidiName(const juce::String& name)  { return nameHasExt(name, { "mid", "midi" }); }
    bool isAudioFile(const juce::File& f)      { return isAudioName(f.getFileName()); }

    /** Writes through a temp file so a crash can't leave a half-written json behind. */
    bool writeTextAtomically(const juce::File& target, const juce::String& text)
    {
        auto tmp = target.getSiblingFile(target.getFileName() + ".tmp");
        if (! tmp.replaceWithText(text, false, false, "\n"))
            return false;
        return tmp.moveFileTo(target);
    }

    juce::File uniqueChild(const juce::File& parent, const juce::String& name, const juce::String& ext = {})
    {
        auto f = parent.getChildFile(name + ext);
        for (int i = 2; f.exists() && i < 1000; ++i)
            f = parent.getChildFile(name + "-" + juce::String(i) + ext);
        return f;
    }

    juce::File songFile(const juce::String& id)
    {
        // id is a slug we generated, but treat it as untrusted: no path separators.
        return songsFolder().getChildFile(slugify(id)).getChildFile("song.json");
    }

    juce::var readJson(const juce::File& f)
    {
        if (! f.existsAsFile())
            return {};
        return juce::JSON::parse(f.loadFileAsString());
    }

    juce::var newSongObject(const juce::String& id, const juce::String& title, double bpm, const juce::String& key,
                            const juce::String& stage, const juce::String& source)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", id);
        o->setProperty("title", title);
        o->setProperty("bpm", bpm);
        o->setProperty("key", key);
        o->setProperty("stage", stage);
        o->setProperty("source", source);
        o->setProperty("created", nowIso());
        o->setProperty("updated", nowIso());
        o->setProperty("stems", juce::Array<juce::var>());
        o->setProperty("sourceFile", juce::String());
        o->setProperty("checklist", juce::var(new juce::DynamicObject()));
        o->setProperty("sessionPath", juce::String());
        o->setProperty("notes", juce::String());
        return juce::var(o);
    }

    juce::String songFolderPath(const juce::String& id) { return songsFolder().getChildFile(slugify(id)).getFullPathName(); }

    bool saveSong(const juce::var& song)
    {
        const auto id = song.getProperty("id", {}).toString();
        if (id.isEmpty())
            return false;
        auto f = songFile(id);
        f.getParentDirectory().createDirectory();
        return writeTextAtomically(f, juce::JSON::toString(song, false));
    }

    /** Unique song id from a title. */
    juce::String uniqueSongId(const juce::String& title)
    {
        const auto base = slugify(title);
        auto id = base;
        for (int i = 2; songsFolder().getChildFile(id).exists() && i < 1000; ++i)
            id = base + "-" + juce::String(i);
        return id;
    }

    juce::String titleFromFileName(const juce::File& f)
    {
        auto name = f.getFileNameWithoutExtension().replaceCharacters("_", " ").trim();
        return name.isEmpty() ? juce::String("Untitled song") : name;
    }

    /** Stem files that are really audio, listed as names relative to the song folder. */
    juce::Array<juce::var> listStemFiles(const juce::File& stemsDir)
    {
        juce::Array<juce::var> out;
        juce::Array<juce::File> files;
        stemsDir.findChildFiles(files, juce::File::findFiles, true);
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
                  { return a.getFileName().compareNatural(b.getFileName()) < 0; });
        for (auto& f : files)
            if (isAudioFile(f) && ! f.getFileName().startsWith("."))
                out.add(f.getRelativePathFrom(stemsDir.getParentDirectory()));
        return out;
    }

    /** Pulls only audio and MIDI out of a zip, flattening folders and skipping macOS junk. */
    int unzipStems(const juce::File& zip, const juce::File& destDir)
    {
        juce::ZipFile zf(zip);
        int written = 0;
        for (int i = 0; i < zf.getNumEntries(); ++i)
        {
            auto* e = zf.getEntry(i);
            if (e == nullptr)
                continue;
            const auto full = e->filename;
            if (full.endsWithChar('/') || full.contains("__MACOSX") || full.contains("/."))
                continue;
            const auto leaf = juce::File::createLegalFileName(full.fromLastOccurrenceOf("/", false, false));
            if (leaf.isEmpty() || leaf.startsWithChar('.'))
                continue;
            if (! (isAudioName(leaf) || isMidiName(leaf)))
                continue;

            std::unique_ptr<juce::InputStream> in(zf.createStreamForEntry(i));
            if (in == nullptr)
                continue;
            auto target = destDir.getChildFile(leaf);
            if (target.exists())
                target = uniqueChild(destDir, target.getFileNameWithoutExtension(), target.getFileExtension());
            juce::FileOutputStream out(target);
            if (out.failedToOpen())
                continue;
            out.writeFromInputStream(*in, -1);
            ++written;
        }
        return written;
    }

    juce::File doneFolder() { auto f = dropFolder().getChildFile("Done"); f.createDirectory(); return f; }

    void moveToDone(const juce::File& f)
    {
        f.moveFileTo(uniqueChild(doneFolder(), f.getFileNameWithoutExtension(), f.getFileExtension()));
    }

    /** A file is "still downloading" if its size changes or the browser's temp suffix is present. */
    bool looksFinished(const juce::File& f)
    {
        if (f.getFileName().endsWithIgnoreCase(".crdownload") || f.getFileName().endsWithIgnoreCase(".part")
            || f.getFileName().endsWithIgnoreCase(".download") || f.getFileName().startsWithChar('.'))
            return false;
        // Not touched in the last 3 seconds.
        return (juce::Time::getCurrentTime() - f.getLastModificationTime()).inSeconds() > 3.0;
    }
}

// ============================================================ helpers
juce::String slugify(const juce::String& text)
{
    juce::String out;
    for (auto c : text.toLowerCase())
        out << (juce::CharacterFunctions::isLetterOrDigit(c) && c < 128 ? juce::String::charToString(c) : juce::String("-"));
    while (out.contains("--"))
        out = out.replace("--", "-");
    out = out.trimCharactersAtStart("-").trimCharactersAtEnd("-");
    return out.isEmpty() ? juce::String("song") : out;
}

juce::String todayStamp() { return juce::Time::getCurrentTime().formatted("%Y-%m-%d"); }

// ============================================================ config
juce::var loadConfig()
{
    auto v = readJson(rootFolder().getChildFile("studio.json"));
    if (! v.isObject())
        v = juce::var(new juce::DynamicObject());
    auto* o = v.getDynamicObject();
    o->setProperty("rootPath", rootFolder().getFullPathName());
    o->setProperty("dropPath", dropFolder().getFullPathName());
    o->setProperty("songsPath", songsFolder().getFullPathName());
    return v;
}

juce::var saveConfig(const juce::String& json)
{
    auto parsed = juce::JSON::parse(json);
    if (! parsed.isObject())
        return makeError("Settings were not valid JSON, so nothing was saved.");

    auto f = rootFolder().getChildFile("studio.json");
    if (f.existsAsFile())
        f.copyFileTo(rootFolder().getChildFile("studio.backup.json"));

    auto* o = new juce::DynamicObject();
    o->setProperty("ok", writeTextAtomically(f, juce::JSON::toString(parsed, false)));
    return juce::var(o);
}

// ============================================================ songs
juce::var listSongs()
{
    juce::Array<juce::File> dirs;
    songsFolder().findChildFiles(dirs, juce::File::findDirectories, false);

    std::vector<juce::var> songs;
    for (auto& d : dirs)
    {
        auto song = readJson(d.getChildFile("song.json"));
        if (! song.isObject())
            continue;

        // Keep the stem list honest if files were added or removed by hand.
        auto stems = listStemFiles(d.getChildFile("stems"));
        song.getDynamicObject()->setProperty("stems", stems);
        song.getDynamicObject()->setProperty("folder", d.getFullPathName());
        song.getDynamicObject()->setProperty("stemsFolder", d.getChildFile("stems").getFullPathName());
        songs.push_back(song);
    }
    std::sort(songs.begin(), songs.end(), [](const juce::var& a, const juce::var& b)
              { return a.getProperty("updated", {}).toString() > b.getProperty("updated", {}).toString(); });

    juce::Array<juce::var> out;
    for (auto& s : songs)
        out.add(s);
    return out;
}

juce::var createSong(const juce::String& title, double bpm, const juce::String& key)
{
    const auto cleanTitle = title.trim().isEmpty() ? juce::String("Untitled song") : title.trim();
    const auto id = uniqueSongId(cleanTitle);
    auto song = newSongObject(id, cleanTitle, bpm, key, "suno", "manual");
    juce::File(songFolderPath(id)).createDirectory();
    if (! saveSong(song))
        return makeError("Could not create the song folder.");
    return song;
}

juce::var updateSong(const juce::String& id, const juce::String& patchJson)
{
    auto song = readJson(songFile(id));
    if (! song.isObject())
        return makeError("Song not found.");
    auto patch = juce::JSON::parse(patchJson);
    if (! patch.isObject())
        return makeError("Update was not valid JSON.");

    auto* target = song.getDynamicObject();
    for (auto& prop : patch.getDynamicObject()->getProperties())
    {
        // id and created are never changed from the UI.
        if (prop.name.toString() == "id" || prop.name.toString() == "created" || prop.name.toString() == "folder")
            continue;
        target->setProperty(prop.name, prop.value);
    }
    target->setProperty("updated", nowIso());
    if (! saveSong(song))
        return makeError("Could not save the song.");
    return song;
}

juce::var scanDrop()
{
    juce::Array<juce::var> imported;
    juce::Array<juce::var> waiting;

    juce::Array<juce::File> items;
    dropFolder().findChildFiles(items, juce::File::findFiles, false);

    for (auto& f : items)
    {
        const bool isZip = f.hasFileExtension("zip");
        if (! (isZip || isAudioFile(f)))
            continue;
        if (! looksFinished(f))
        {
            waiting.add(f.getFileName());
            continue;
        }

        const auto title = titleFromFileName(f);
        const auto id = uniqueSongId(title);
        const juce::File songDir(songFolderPath(id));
        songDir.createDirectory();

        auto song = newSongObject(id, title, 0.0, juce::String(), isZip ? "stems" : "suno", "drop");
        auto* o = song.getDynamicObject();

        if (isZip)
        {
            auto stemsDir = songDir.getChildFile("stems");
            stemsDir.createDirectory();
            const int n = unzipStems(f, stemsDir);
            if (n == 0)
            {
                // Not a stem pack. Leave the file where it is and say so.
                songDir.deleteRecursively();
                auto* e = new juce::DynamicObject();
                e->setProperty("file", f.getFileName());
                e->setProperty("problem", "No audio files found inside this zip.");
                waiting.add(juce::var(e));
                continue;
            }
            o->setProperty("stems", listStemFiles(stemsDir));
            o->setProperty("needsStems", false);
        }
        else
        {
            auto srcDir = songDir.getChildFile("source");
            srcDir.createDirectory();
            auto copy = srcDir.getChildFile(f.getFileName());
            f.copyFileTo(copy);
            o->setProperty("sourceFile", copy.getRelativePathFrom(songDir));
            o->setProperty("needsStems", true);
        }

        saveSong(song);
        moveToDone(f);

        auto* summary = new juce::DynamicObject();
        summary->setProperty("id", id);
        summary->setProperty("title", title);
        summary->setProperty("kind", isZip ? "stems" : "mix");
        imported.add(juce::var(summary));
    }

    auto* res = new juce::DynamicObject();
    res->setProperty("imported", imported);
    res->setProperty("waiting", waiting);
    res->setProperty("dropPath", dropFolder().getFullPathName());
    return juce::var(res);
}

// ============================================================ sessions
juce::var startSession(const juce::String& templatePath, const juce::String& projectName, const juce::String& songId)
{
    juce::File tmpl(templatePath);
    if (! tmpl.exists())
        return makeError("That template can't be found any more. Re-add it from the template list.");

    const auto name = projectName.trim().isEmpty() ? juce::String("Untitled") : projectName.trim();
    auto sessionDir = uniqueChild(sessionsFolder(), todayStamp() + "-" + slugify(name));
    if (! sessionDir.createDirectory().wasOk())
        return makeError("Could not create the session folder.");

    // Keep the template's own extension (.logicx, .als, .luna ...) and name the copy after the project.
    const auto ext = tmpl.getFileExtension();
    auto copy = sessionDir.getChildFile(juce::File::createLegalFileName(name) + ext);

    const bool copied = tmpl.isDirectory() ? tmpl.copyDirectoryTo(copy) : tmpl.copyFileTo(copy);
    if (! copied)
    {
        sessionDir.deleteRecursively();
        return makeError("Could not copy the template. Check that you have disk space and permission.");
    }

    juce::String bpm, key, stemsPath;
    juce::var song;
    if (songId.isNotEmpty())
    {
        song = readJson(songFile(songId));
        if (song.isObject())
        {
            const double b = (double) song.getProperty("bpm", 0.0);
            bpm = b > 0.0 ? juce::String(b, 1) : juce::String();
            key = song.getProperty("key", {}).toString();
            auto stems = songsFolder().getChildFile(slugify(songId)).getChildFile("stems");
            if (stems.isDirectory())
                stemsPath = stems.getFullPathName();
        }
    }

    juce::String notes;
    notes << "# " << name << "\n\n"
          << "Started " << juce::Time::getCurrentTime().toString(true, true) << " from template: "
          << tmpl.getFileName() << "\n\n";
    notes << "- Tempo: " << (bpm.isNotEmpty() ? bpm + " BPM" : juce::String("set it in your DAW (check Suno Studio's timeline)")) << "\n";
    notes << "- Key: " << (key.isNotEmpty() ? key : juce::String("not set")) << "\n";
    if (stemsPath.isNotEmpty())
        notes << "- Stems folder (drag these into the DAW, set tempo first): " << stemsPath << "\n";
    notes << "\nTip: set the project tempo before importing stems so they land on the grid.\n";
    sessionDir.getChildFile("SESSION-NOTES.md").replaceWithText(notes, false, false, "\n");

    if (song.isObject())
    {
        song.getDynamicObject()->setProperty("sessionPath", copy.getFullPathName());
        song.getDynamicObject()->setProperty("updated", nowIso());
        saveSong(song);
    }

    auto* o = new juce::DynamicObject();
    o->setProperty("ok", true);
    o->setProperty("sessionPath", sessionDir.getFullPathName());
    o->setProperty("openPath", copy.getFullPathName());
    return juce::var(o);
}

// ============================================================ helpers for the webview
juce::var openPath(const juce::String& path)
{
    juce::File f(path);
    auto* o = new juce::DynamicObject();
    const bool ok = f.exists() && f.startAsProcess();
    o->setProperty("ok", ok);
    if (! ok)
        o->setProperty("error", f.exists() ? "The system could not open that file." : "That file or folder no longer exists.");
    return juce::var(o);
}

juce::var revealPath(const juce::String& path)
{
    juce::File f(path);
    auto* o = new juce::DynamicObject();
    if (! f.exists())
    {
        o->setProperty("ok", false);
        o->setProperty("error", "That file or folder no longer exists.");
        return juce::var(o);
    }
    f.revealToUser();
    o->setProperty("ok", true);
    return juce::var(o);
}

juce::var openUrl(const juce::String& url)
{
    auto* o = new juce::DynamicObject();
    // Only plain https links, never file: or custom schemes.
    const bool ok = url.startsWithIgnoreCase("https://") && juce::URL(url).launchInDefaultBrowser();
    o->setProperty("ok", ok);
    return juce::var(o);
}

juce::var pathExists(const juce::String& path)
{
    return juce::var(path.isNotEmpty() && juce::File(path).exists());
}

} // namespace Studio
