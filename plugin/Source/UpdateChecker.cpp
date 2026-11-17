#include "UpdateChecker.h"
#include <utility>

UpdateChecker::UpdateChecker() : juce::Thread ("room update check")
{
    if (auto s = openSettings())
    {
        latestVersion = s->getValue ("latestVersion");
        downloadUrl = s->getValue ("latestUrl");
    }
}

UpdateChecker::~UpdateChecker()
{
    cancelPendingUpdate();
    stopThread (6000);
}

juce::String UpdateChecker::currentVersion()
{
   #ifdef JucePlugin_VersionString
    return JucePlugin_VersionString;
   #else
    return "1.0.0";
   #endif
}

bool UpdateChecker::isNewer (const juce::String& a, const juce::String& b)
{
    if (a.isEmpty())
        return false;
    auto pa = juce::StringArray::fromTokens (a.trim(), ".", {});
    auto pb = juce::StringArray::fromTokens (b.trim(), ".", {});
    for (int i = 0; i < std::max (pa.size(), pb.size()); ++i)
    {
        const auto x = pa[i].getIntValue(), y = pb[i].getIntValue();
        if (x != y)
            return x > y;
    }
    return false;
}

std::unique_ptr<juce::PropertiesFile> UpdateChecker::openSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "room";
    o.folderName = "eville";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}

void UpdateChecker::checkIfDue()
{
    if (isThreadRunning())
        return;
    if (auto s = openSettings())
    {
        const auto last = s->getValue ("lastCheck").getLargeIntValue();
        if (juce::Time::currentTimeMillis() - last < checkIntervalMs)
            return;
        s->setValue ("lastCheck", juce::String (juce::Time::currentTimeMillis()));   // one try per interval, online or not
        s->saveIfNeeded();
    }
    startThread (juce::Thread::Priority::low);
}

void UpdateChecker::checkNow (std::function<void (bool)> done)
{
    manualDone = std::move (done);
    if (isThreadRunning())
        return;   // the check under way will answer
    if (auto s = openSettings())
    {
        s->setValue ("lastCheck", juce::String (juce::Time::currentTimeMillis()));
        s->saveIfNeeded();
    }
    startThread (juce::Thread::Priority::low);
}

void UpdateChecker::run()
{
    auto fail = [this]
    {
        { const juce::ScopedLock sl (lock); failed = true; }
        triggerAsyncUpdate();
    };
    // no-cache: a proxy or CDN must not hand back an old latest.json
    auto stream = juce::URL (latestUrl).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (5000)
            .withExtraHeaders ("Cache-Control: no-cache"));
    if (threadShouldExit())
        return;
    if (stream == nullptr)
        return fail();
    const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
    const auto version = json.getProperty ("version", {}).toString();
    if (version.isEmpty())
        return fail();
    {
        const juce::ScopedLock sl (lock);
        fetchedVersion = version;
        fetchedUrl = json.getProperty ("url", {}).toString();
        fetched = true;
    }
    triggerAsyncUpdate();
}

void UpdateChecker::handleAsyncUpdate()
{
    {
        const juce::ScopedLock sl (lock);
        if (! fetched)
        {
            const auto wasFailure = std::exchange (failed, false);
            if (wasFailure && manualDone)
                std::exchange (manualDone, nullptr) (false);
            return;
        }
        fetched = false;
        latestVersion = fetchedVersion;
        downloadUrl = fetchedUrl;
    }
    if (auto s = openSettings())
    {
        s->setValue ("latestVersion", latestVersion);
        s->setValue ("latestUrl", downloadUrl);
        s->saveIfNeeded();
    }
    if (onResult)
        onResult();
    if (manualDone)
        std::exchange (manualDone, nullptr) (true);
}
