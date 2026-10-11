#pragma once

#include <juce_core/juce_core.h>

/**
 * Studio: the launchpad side of GHS FX Companion.
 *
 * Plain file work only (no audio, no network, no threads), so it is cheap, safe to call
 * from the message thread, and works fully offline. The webview draws the dashboard and
 * runs the "next step" rules; this engine just owns the folders and files:
 *
 *   ~/Music/GHS/Studio/
 *       studio.json            templates + settings (written by the app)
 *       Drop/                  put a Suno Multitrack .zip (or one audio file) here
 *           Done/              originals are moved here after they are imported
 *       Songs/<slug>/
 *           song.json          title, bpm, key, stage, checklist, session path ...
 *           stems/             unzipped Suno stems (WAV)
 *           source/            a single full-mix file, if that is all you had
 *       Sessions/<date>-<slug>/
 *           <copy of your DAW template>
 *           SESSION-NOTES.md   tempo, key and where the stems are
 *
 * Nothing here ever deletes or overwrites your own files: templates are copied, never
 * edited, and imported originals are moved to Drop/Done, not removed.
 */
namespace Studio
{
    juce::File rootFolder();     // ~/Music/GHS/Studio
    juce::File dropFolder();     // ~/Music/GHS/Studio/Drop
    juce::File songsFolder();    // ~/Music/GHS/Studio/Songs
    juce::File sessionsFolder(); // ~/Music/GHS/Studio/Sessions

    /** Turns "My Song (v2)!" into "my-song-v2". Never empty. */
    juce::String slugify(const juce::String& text);

    /** "2026-10-11". */
    juce::String todayStamp();

    // ---- config (templates + settings), stored as JSON text so the UI owns its shape ----
    juce::var loadConfig();                       // {} if none yet
    juce::var saveConfig(const juce::String& json);

    // ---- songs ----
    juce::var listSongs();                        // array of song objects (newest first)
    juce::var createSong(const juce::String& title, double bpm, const juce::String& key);
    juce::var updateSong(const juce::String& id, const juce::String& patchJson); // shallow merge

    /** Looks in Drop/ for new Suno exports. A .zip becomes a song with stems; a single
        audio file becomes a song at the "Suno" stage that still needs stems. */
    juce::var scanDrop();

    // ---- sessions ----
    /** Copies the template (file or folder, e.g. .logicx / .als) into a new dated session
        folder, writes SESSION-NOTES.md and returns {ok, sessionPath, openPath}. */
    juce::var startSession(const juce::String& templatePath, const juce::String& projectName, const juce::String& songId);

    // ---- helpers the webview calls ----
    juce::var openPath(const juce::String& path);    // open with the default app
    juce::var revealPath(const juce::String& path);  // show in Finder / file manager
    juce::var openUrl(const juce::String& url);      // default browser; https only
    juce::var pathExists(const juce::String& path);
}
