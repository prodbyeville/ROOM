#include "License.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstring>
#include <optional>

namespace room::licensing
{
namespace
{
    // Replace with the server's real public key (server/README.md, "keys").
    // Until the licensing switch is on (ROOM_LICENSING in CMake), nothing is locked.
    constexpr uint8_t kProductionKey[32] = {
        0x3a, 0x6f, 0x1c, 0x9e, 0x52, 0xd4, 0x07, 0xb8, 0x91, 0x2e, 0x64, 0xf0, 0x1d, 0x8a, 0xc3, 0x75,
        0x0b, 0xe9, 0x46, 0x22, 0x7f, 0xa1, 0x5c, 0x38, 0xd6, 0x0e, 0x93, 0x4b, 0xbf, 0x17, 0x68, 0xc2 };

    std::optional<ed25519::PublicKey> testKey;
    juce::File testStorage;
    juce::String testMachine;
    std::atomic<juce::int64> testClock { 0 };

    juce::int64 localNow()
    {
        const auto t = testClock.load();
        return t != 0 ? t : juce::Time::currentTimeMillis() / 1000;
    }

    bool base64UrlDecode (const juce::String& in, juce::MemoryBlock& out)
    {
        auto s = in.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
        while (s.length() % 4 != 0)
            s << "=";
        juce::MemoryOutputStream mo (out, false);
        return juce::Base64::convertFromBase64 (mo, s);
    }

    Type typeFrom (const juce::String& s)
    {
        if (s == "launch") return Type::launch;
        if (s == "trial")  return Type::trial;
        if (s == "full")   return Type::full;
        return Type::none;
    }

    juce::String hexOf (const uint8_t* p, size_t n)
    {
        return juce::String::toHexString (p, (int) n, 0);
    }
}

bool parseToken (const juce::String& raw, const ed25519::PublicKey& publicKey,
                 const juce::String& machineId, Token& out)
{
    const auto dot = raw.indexOfChar ('.');
    if (dot <= 0 || raw.length() > 8192)
        return false;

    juce::MemoryBlock payload, sigBytes;
    if (! base64UrlDecode (raw.substring (0, dot), payload)
        || ! base64UrlDecode (raw.substring (dot + 1), sigBytes)
        || sigBytes.getSize() != 64)
        return false;

    ed25519::Signature sig {};
    std::memcpy (sig.data(), sigBytes.getData(), 64);
    if (! ed25519::verify (sig, publicKey, payload.getData(), payload.getSize()))
        return false;

    const auto json = juce::JSON::parse (payload.toString());
    if (! json.isObject() || (int) json.getProperty ("v", 0) != 1)
        return false;

    Token t;
    t.raw = raw;
    t.email = json.getProperty ("email", {}).toString();
    t.licenseId = json.getProperty ("license", {}).toString();
    t.machine = json.getProperty ("machine", {}).toString();
    t.checkoutUrl = json.getProperty ("checkout", {}).toString();
    t.key = json.getProperty ("key", {}).toString();
    t.type = typeFrom (json.getProperty ("type", {}).toString());
    t.expires = (juce::int64) json.getProperty ("expires", 0);
    t.checkBy = (juce::int64) json.getProperty ("checkBy", 0);
    t.serverTime = (juce::int64) json.getProperty ("serverTime", 0);

    if (t.type == Type::none || t.machine != machineId || t.serverTime <= 0)
        return false;
    out = t;
    return true;
}

Status evaluate (const Token* token, juce::int64 now)
{
    Status s;
    if (token == nullptr || token->type == Type::none)
        return s;   // welcome

    s.type = token->type;
    s.expires = token->expires;
    s.email = token->email;
    s.checkoutUrl = token->checkoutUrl;
    if (token->expires > 0 && now >= token->expires)
        s.state = State::expired;
    else if (token->checkBy > 0 && now > token->checkBy)
        s.state = State::checkIn;
    else
        s.state = State::active;
    return s;
}

juce::String statusLine (const Status& s, juce::int64 now)
{
    if (! s.isActive() || s.expires <= 0)
        return {};
    if (s.type == Type::launch)
    {
        const auto when = juce::Time (s.expires * 1000).formatted ("%d %b").toLowerCase();
        return "open until " + (when.startsWithChar ('0') ? when.substring (1) : when);
    }
    if (s.type == Type::trial)
    {
        const auto days = (int) std::ceil ((double) (s.expires - now) / 86400.0);
        return juce::String (juce::CharPointer_UTF8 ("trial \xc2\xb7 ")) + juce::String (std::max (1, days)) + (days == 1 ? " day left" : " days left");
    }
    return {};
}

//==============================================================================
static juce::File defaultStorage()
{
   #if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/eville");
   #else
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("eville");
   #endif
    return dir.getChildFile ("room.license");
}

static juce::String computeMachineId()
{
    // Hash the OS's device ID so the raw identifier never leaves the computer.
    auto id = juce::SystemStats::getUniqueDeviceID();
    if (id.isEmpty())
        id = juce::SystemStats::getMachineIdentifiers (juce::SystemStats::MachineIdFlags::uniqueId
                                                       | juce::SystemStats::MachineIdFlags::fileSystemId).joinIntoString ("|");
    const auto utf8 = ("room|" + id).toStdString();
    const auto d = ed25519::sha512 (utf8.data(), utf8.size());
    return hexOf (d.data(), 16);
}

juce::String Manager::serverBase()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("ROOM_LICENSE_SERVER", {});
    return env.isNotEmpty() ? env.trimCharactersAtEnd ("/") : juce::String ("https://license.prodbyeville.com");
}

ed25519::PublicKey Manager::productionKey()
{
    if (testKey) return *testKey;
    ed25519::PublicKey k {};
    std::memcpy (k.data(), kProductionKey, 32);
    return k;
}

void Manager::setPublicKeyForTests (const ed25519::PublicKey& k) { testKey = k; }
void Manager::setStorageFileForTests (const juce::File& f) { testStorage = f; }
void Manager::setMachineIdForTests (const juce::String& m) { testMachine = m; }
void Manager::setClockForTests (juce::int64 t) { testClock = t; }

Manager::Manager() : juce::Thread ("ROOM license")
{
    machineId = testMachine.isNotEmpty() ? testMachine : computeMachineId();
    machineName = juce::SystemStats::getComputerName();
    storage = testStorage != juce::File() ? testStorage : defaultStorage();
    load();
    startThread (juce::Thread::Priority::low);
}

Manager::~Manager()
{
    waitingForBrowser = false;
    signalThreadShouldExit();
    wake.signal();
    stopThread (8000);
}

juce::int64 Manager::now() const
{
    const juce::ScopedLock sl (lock);
    return std::max (localNow(), newestServerTime);
}

Status Manager::getStatus() const
{
    const auto t = now();
    const juce::ScopedLock sl (lock);
    return evaluate (token.get(), t);
}

juce::String Manager::getLastMessage() const
{
    const juce::ScopedLock sl (lock);
    return lastMessage;
}

void Manager::setMessage (const juce::String& m)
{
    {
        const juce::ScopedLock sl (lock);
        lastMessage = m;
    }
    sendChangeMessage();
}

void Manager::load()
{
    const auto json = juce::JSON::parse (storage.loadFileAsString());
    if (! json.isObject())
        return;
    const juce::ScopedLock sl (lock);
    newestServerTime = (juce::int64) json.getProperty ("seen", 0);
    lastCheck = (juce::int64) json.getProperty ("checked", 0);
    Token t;
    if (parseToken (json.getProperty ("token", {}).toString(), productionKey(), machineId, t))
    {
        newestServerTime = std::max (newestServerTime, t.serverTime);
        token = std::make_unique<Token> (t);
    }
}

void Manager::save()
{
    auto* obj = new juce::DynamicObject();
    {
        const juce::ScopedLock sl (lock);
        obj->setProperty ("token", token != nullptr ? token->raw : juce::String());
        obj->setProperty ("seen", newestServerTime);
        obj->setProperty ("checked", lastCheck);
    }
    storage.getParentDirectory().createDirectory();
    storage.replaceWithText (juce::JSON::toString (juce::var (obj)));
}

bool Manager::installToken (const juce::String& raw)
{
    Token t;
    if (! parseToken (raw, productionKey(), machineId, t))
        return false;
    {
        const juce::ScopedLock sl (lock);
        token = std::make_unique<Token> (t);
        newestServerTime = std::max (newestServerTime, t.serverTime);
        lastCheck = std::max (localNow(), t.serverTime);
    }
    save();
    sendChangeMessage();
    return true;
}

bool Manager::acceptReply (const juce::var& reply)
{
    if (const auto st = (juce::int64) reply.getProperty ("serverTime", 0); st > 0)
    {
        const juce::ScopedLock sl (lock);
        newestServerTime = std::max (newestServerTime, st);
    }
    if (reply.getProperty ("revoked", false))
    {
        {
            const juce::ScopedLock sl (lock);
            token.reset();
        }
        save();
        sendChangeMessage();
        return true;
    }
    const auto raw = reply.getProperty ("token", {}).toString();
    return raw.isNotEmpty() && installToken (raw);
}

bool Manager::post (const juce::String& path, const juce::var& body, juce::var& reply, juce::String& error)
{
    int status = 0;
    juce::StringPairArray headers;
    auto url = juce::URL (serverBase() + path).withPOSTData (juce::JSON::toString (body, true));
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                             .withExtraHeaders ("Content-Type: application/json")
                                             .withConnectionTimeoutMs (15000)
                                             .withStatusCode (&status)
                                             .withResponseHeaders (&headers));
    if (stream == nullptr)
    {
        error = "couldn't reach the license server. check your connection.";
        return false;
    }
    reply = juce::JSON::parse (stream->readEntireStreamAsString());
    if (status >= 200 && status < 300 && reply.isObject())
        return true;
    error = reply.getProperty ("error", "the license server said no (" + juce::String (status) + ")").toString();
    return false;
}

void Manager::startBrowserActivation()
{
    waitingForBrowser = true;
    job = Job::browser;
    wake.signal();
}

void Manager::activateWithKey (const juce::String& key)
{
    {
        const juce::ScopedLock sl (lock);
        pendingKey = key.trim().toUpperCase();
    }
    job = Job::key;
    wake.signal();
}

void Manager::refreshNow()
{
    job = Job::refresh;
    wake.signal();
}

void Manager::deactivateThisComputer()
{
    job = Job::deactivate;
    wake.signal();
}

void Manager::run()
{
    bool firstPass = true;
    while (! threadShouldExit())
    {
        auto todo = job.exchange (Job::none);

        // Check in when ROOM loads, then every few days.
        if (todo == Job::none)
        {
            juce::ScopedLock sl (lock);
            if (token != nullptr && (firstPass || localNow() - lastCheck > checkEverySeconds))
                todo = Job::refresh;
        }
        firstPass = false;

        juce::var reply;
        juce::String error;
        juce::DynamicObject::Ptr body (new juce::DynamicObject());
        body->setProperty ("machine", machineId);
        body->setProperty ("name", machineName);

        switch (todo)
        {
            case Job::refresh:
            {
                juce::String raw;
                {
                    const juce::ScopedLock sl (lock);
                    if (token != nullptr) raw = token->raw;
                }
                if (raw.isEmpty()) break;
                body->setProperty ("token", raw);
                if (post ("/refresh", juce::var (body.get()), reply, error))
                {
                    acceptReply (reply);
                    setMessage ({});
                }
                else
                {
                    setMessage (error);
                }
                break;
            }
            case Job::key:
            {
                juce::String key;
                {
                    const juce::ScopedLock sl (lock);
                    key = pendingKey;
                }
                body->setProperty ("key", key);
                if (post ("/activate/key", juce::var (body.get()), reply, error) && acceptReply (reply))
                    setMessage ({});
                else
                    setMessage (error.isNotEmpty() ? error : juce::String ("that key didn't work."));
                break;
            }
            case Job::browser:
            {
                if (! post ("/device/start", juce::var (body.get()), reply, error))
                {
                    waitingForBrowser = false;
                    setMessage (error);
                    break;
                }
                const auto code = reply.getProperty ("code", {}).toString();
                const juce::URL page (reply.getProperty ("url", {}).toString());
                const auto interval = juce::jlimit (1, 30, (int) reply.getProperty ("interval", 3));
                juce::MessageManager::callAsync ([page] { page.launchInDefaultBrowser(); });
                sendChangeMessage();

                const auto giveUp = juce::Time::getMillisecondCounter() + 15u * 60u * 1000u;
                while (waitingForBrowser && ! threadShouldExit() && juce::Time::getMillisecondCounter() < giveUp)
                {
                    wake.wait (interval * 1000);
                    if (! waitingForBrowser || threadShouldExit())
                        break;
                    auto poll = new juce::DynamicObject();
                    poll->setProperty ("code", code);
                    juce::var r;
                    juce::String e;
                    if (post ("/device/poll", juce::var (poll), r, e))
                    {
                        if (r.getProperty ("token", {}).toString().isNotEmpty())
                        {
                            acceptReply (r);
                            setMessage ({});
                            break;
                        }
                        // still waiting for the person to sign in
                    }
                    else if (r.getProperty ("error", {}).toString().isNotEmpty())
                    {
                        setMessage (e);
                        break;
                    }
                }
                waitingForBrowser = false;
                sendChangeMessage();
                break;
            }
            case Job::deactivate:
            {
                juce::String raw;
                {
                    const juce::ScopedLock sl (lock);
                    if (token != nullptr) raw = token->raw;
                }
                body->setProperty ("token", raw);
                if (raw.isNotEmpty() && ! post ("/deactivate", juce::var (body.get()), reply, error))
                    setMessage (error);
                {
                    const juce::ScopedLock sl (lock);
                    token.reset();
                }
                save();
                sendChangeMessage();
                break;
            }
            case Job::none:
                break;
        }

        if (job.load() == Job::none)
            wake.wait (60 * 60 * 1000);   // an hour, or until someone asks for something
    }
}
} // namespace room::licensing
