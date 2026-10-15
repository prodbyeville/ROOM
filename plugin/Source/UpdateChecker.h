#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>

// Checks prodbyeville.com for a newer ROOM, at most once a day, in the
// background. The site hosts a tiny file:
//
//   https://prodbyeville.com/room/latest.json
//   { "version": "0.2.0", "url": "https://prodbyeville.com/room" }
//
// No network, a missing file or bad JSON: nothing happens. The last answer is
// remembered (eville/room.settings) so every editor knows without asking again.

class UpdateChecker : private juce::Thread,
                      private juce::AsyncUpdater
{
public:
    UpdateChecker();
    ~UpdateChecker() override;

    /** Start a check if the last one is more than a day old. */
    void checkIfDue();

    bool isUpdateAvailable() const { return isNewer (latestVersion, currentVersion()); }
    juce::String getLatestVersion() const { return latestVersion; }
    juce::String getDownloadUrl() const { return downloadUrl.isNotEmpty() ? downloadUrl : websiteUrl; }

    /** Called on the message thread when a check finishes. */
    std::function<void()> onResult;

    static juce::String currentVersion();
    /** ROOM's small per-user settings file (eville/room.settings). */
    static std::unique_ptr<juce::PropertiesFile> openSettings();
    /** True if version a (e.g. "0.2.0") is newer than b. */
    static bool isNewer (const juce::String& a, const juce::String& b);

    static constexpr const char* latestUrl = "https://prodbyeville.com/room/latest.json";
    static constexpr const char* websiteUrl = "https://prodbyeville.com";
    static constexpr const char* supportEmail = "support@prodbyeville.com";

private:
    void run() override;
    void handleAsyncUpdate() override;

    juce::String latestVersion, downloadUrl;
    juce::CriticalSection lock;
    juce::String fetchedVersion, fetchedUrl;
    bool fetched = false;
};
