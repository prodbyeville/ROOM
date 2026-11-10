#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "LicenseCrypto.h"
#include "LicensePublicKey.h"

#include <atomic>
#include <functional>

// The license (ROOM 1.0, free): WARP's licensing, on WARP's server (update.prodbyeville.com).
// ROOM is a $0 Shopify product; the order emails a key (ROOM-XXXXX-XXXXX-XXXXX-XXXXX). Signing
// in with that email + key asks the server, which returns a license signed for this computer;
// it's saved in the user's app data (ROOM/license.json) and checked offline with the public key
// every time ROOM loads, so ROOM never needs the internet after activating. Every two weeks it
// quietly asks the server whether the license is still good; only a clear "revoked" turns it
// off, and no answer keeps it working. Up to three computers per key.
//
// Every request says "product": "room" (without it the server treats it as WARP's), and only a
// license signed for "room" is accepted. LicenseCrypto.h and LicensePublicKey.h are WARP's,
// unchanged: both plugins share the key pair.
//
// Shared by every instance (juce::SharedResourcePointer). The audio thread never touches it:
// each processor decides at load whether it's locked (RoomAudioProcessor). Network calls run on
// their own thread and report back on the message thread. A change (activated, forgotten)
// is broadcast so open instances can unlock.
class LicenseManager final : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* apiBase = "https://update.prodbyeville.com/api/v1";
    static constexpr const char* product = "room";
    // "get room free": the download page, which links to the $0 product for a key.
    static constexpr const char* getUrl = "https://update.prodbyeville.com/room";
    static constexpr const char* supportEmail = "support@prodbyeville.com";

    // Opens an email to support in the user's mail app, with the subject filled in.
    static void emailSupport (const juce::String& subject)
    {
        juce::URL ("mailto:" + juce::String (supportEmail) + "?subject=" + juce::URL::addEscapeChars (subject, true)).launchInDefaultBrowser();
    }

    LicenseManager()
    {
        load();
        revalidateIfDue();
    }

    ~LicenseManager() override
    {
        aliveFlag->store (false);   // replies still on their way find the manager gone
        pool.removeAllJobs (true, 6000);
    }

    bool isActivated() const noexcept { return activated.load (std::memory_order_relaxed); }
    juce::String licensedEmail() const { return current.email; }

    // This computer's id as the server sees it: a hash of the OS's device id (not the id itself).
    static juce::String machineId()
    {
        const auto raw = juce::SystemStats::getUniqueDeviceID() + "|" + juce::SystemStats::getComputerName();
        return juce::SHA256 (("warp-machine|" + raw).toUTF8()).toHexString().substring (0, 32);
    }

    // Signs in: on success the license is saved and isActivated() turns true. `done` is called on
    // the message thread with an empty string, or the error to show.
    void activate (const juce::String& email, const juce::String& key, std::function<void (juce::String)> done)
    {
        auto body = new juce::DynamicObject();
        body->setProperty ("email", email.trim().toLowerCase());
        body->setProperty ("key", key.trim().toUpperCase());
        body->setProperty ("machineId", machineId());
        body->setProperty ("machineName", juce::SystemStats::getComputerName());
        body->setProperty ("product", product);
        post ("/activate", juce::var (body), [this, done] (const juce::var& reply, const juce::String& networkError)
        {
            if (networkError.isNotEmpty())
                return done (networkError);
            if (! (bool) reply["ok"])
                return done (reply["error"].toString().isNotEmpty() ? reply["error"].toString() : juce::String ("The license server didn't accept that."));
            if (! store (LicenseCrypto::fromVar (reply["license"])))
                return done ("The license came back damaged. Please try again, or contact support.");
            done ({});
        });
    }

    // Frees this computer's activation (so the license can move to another computer), then
    // forgets the license here even if the server can't be reached.
    void deactivate (std::function<void (juce::String)> done)
    {
        auto body = new juce::DynamicObject();
        body->setProperty ("email", current.email);
        body->setProperty ("key", current.key);
        body->setProperty ("machineId", machineId());
        body->setProperty ("product", product);
        post ("/deactivate", juce::var (body), [this, done] (const juce::var& reply, const juce::String& networkError)
        {
            forget();
            done (networkError.isNotEmpty() ? "Deactivated here, but the server couldn't be reached: this computer may still count "
                                              "towards your three until it's freed (contact support if you run out)."
                                            : (bool) reply["ok"] ? juce::String() : reply["error"].toString());
        });
    }

    // Checks a license against the compiled-in public key and this computer (for tests too).
    static bool isValid (const LicenseCrypto::License& l, const juce::String& publicKey = currentPublicKey())
    {
        return l.product == product && l.machineId == machineId()
            && LicenseCrypto::verify (LicenseCrypto::payload (l), l.signature, publicKey);
    }

    //==========================================================================
    // Tests only: a throwaway key pair and a scratch folder.
    static void setPublicKeyForTests (const juce::String& k) { testKey() = k; }
    static void setFolderForTests (const juce::File& f)       { testFolder() = f; }
    // As if the server had sent this license (or a clear "revoked").
    bool installForTests (const LicenseCrypto::License& l)    { return store (l); }
    void revokeForTests()                                     { forget(); }

private:
    static juce::String& testKey()     { static juce::String k; return k; }
    static juce::File& testFolder()    { static juce::File f; return f; }
    static juce::String currentPublicKey() { return testKey().isNotEmpty() ? testKey() : juce::String (LicensePublicKey::key); }

    static juce::File folder()
    {
        if (testFolder() != juce::File())
            return testFolder();
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("ROOM");
    }
    static juce::File licenseFile() { return folder().getChildFile ("license.json"); }
    static juce::File checkedFile() { return folder().getChildFile ("license.checked"); }

    void load()
    {
        const auto license = LicenseCrypto::fromVar (juce::JSON::parse (licenseFile().loadFileAsString()));
        if (isValid (license))
        {
            current = license;
            activated = true;
        }
    }

    bool store (const LicenseCrypto::License& license)
    {
        if (! isValid (license))
            return false;
        folder().createDirectory();
        licenseFile().replaceWithText (juce::JSON::toString (LicenseCrypto::toVar (license)));
        checkedFile().replaceWithText (juce::String (juce::Time::currentTimeMillis()));
        current = license;
        activated = true;
        sendChangeMessage();
        return true;
    }

    void forget()
    {
        licenseFile().deleteFile();
        checkedFile().deleteFile();
        current = {};
        activated = false;
        sendChangeMessage();
    }

    // Every 14 days: is the license still good? Only a clear "revoked" from the server turns it off.
    void revalidateIfDue()
    {
        if (! activated)
            return;
        const auto last = checkedFile().loadFileAsString().getLargeIntValue();
        if (juce::Time::currentTimeMillis() - last < (juce::int64) 14 * 24 * 60 * 60 * 1000)
            return;
        auto body = new juce::DynamicObject();
        body->setProperty ("email", current.email);
        body->setProperty ("key", current.key);
        body->setProperty ("machineId", machineId());
        body->setProperty ("product", product);
        post ("/validate", juce::var (body), [this] (const juce::var& reply, const juce::String& networkError)
        {
            if (networkError.isNotEmpty())
                return;   // offline: keep working, ask again next time
            if (reply["revoked"].isBool() && (bool) reply["revoked"])
                forget();
            else
                checkedFile().replaceWithText (juce::String (juce::Time::currentTimeMillis()));
        });
    }

    // POSTs JSON to the server on the pool's thread; `then` runs on the message thread with the
    // reply, or an empty reply and a network error.
    void post (const juce::String& path, const juce::var& body, std::function<void (const juce::var&, const juce::String&)> then)
    {
        auto url = juce::URL (juce::String (apiBase) + path).withPOSTData (juce::JSON::toString (body, true));
        auto alive = aliveFlag;
        pool.addJob ([url, then, alive]
        {
            int status = 0;
            juce::String text;
            const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                     .withExtraHeaders ("Content-Type: application/json")
                                     .withConnectionTimeoutMs (10000)
                                     .withStatusCode (&status);
            if (auto stream = url.createInputStream (options))
                text = stream->readEntireStreamAsString();
            const auto reply = juce::JSON::parse (text);
            const auto error = (status == 0 || ! reply.isObject())
                                 ? juce::String ("Couldn't reach the license server. Check your internet connection and try again.")
                                 : juce::String();
            juce::MessageManager::callAsync ([then, reply, error, alive]
            {
                if (alive->load())
                    then (reply, error);
            });
        });
    }

    std::atomic<bool> activated { false };
    LicenseCrypto::License current;
    juce::ThreadPool pool { 1 };
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);
};
