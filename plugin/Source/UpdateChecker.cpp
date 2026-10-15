#include "UpdateChecker.h"

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
    return "0.1.0";
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
        if (juce::Time::currentTimeMillis() - last < (juce::int64) 24 * 60 * 60 * 1000)
            return;
        s->setValue ("lastCheck", juce::String (juce::Time::currentTimeMillis()));   // one try a day, online or not
        s->saveIfNeeded();
    }
    startThread (juce::Thread::Priority::low);
}

void UpdateChecker::run()
{
    auto stream = juce::URL (latestUrl).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (5000));
    if (stream == nullptr || threadShouldExit())
        return;
    const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
    const auto version = json.getProperty ("version", {}).toString();
    if (version.isEmpty())
        return;
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
            return;
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
}
