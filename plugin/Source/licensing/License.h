#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Ed25519.h"

// ROOM licensing, plugin side.
//
// The license server (server/ in the repo) hands out signed tokens:
//
//     base64url(JSON payload) "." base64url(Ed25519 signature of the payload)
//
//     { "v": 1, "email": "...", "license": "...", "type": "launch" | "trial" | "full",
//       "machine": "<this computer>", "expires": <unix s, 0 = never>,
//       "checkBy": <unix s>, "serverTime": <unix s>, "checkout": "<url>", "key": "ROOM-...." }
//
// Rules (see the plan in the repo history):
//  - A ROOM instance decides at load: active or locked. That decision never
//    turns into "locked" during a session; a check can only unlock it.
//  - Time is judged by the newest server time ROOM has seen, so turning the
//    computer's clock back doesn't extend anything.
//  - Paid ("full") tokens keep working offline until checkBy (14 days after
//    the last check). Launch and trial tokens are short-lived on purpose.
//  - Nothing here ever runs on the audio thread.

namespace room::licensing
{
enum class Type { none, launch, trial, full };

struct Token
{
    juce::String raw;                 // exactly as received, for storage / refresh
    juce::String email, licenseId, machine, checkoutUrl, key;
    Type type = Type::none;
    juce::int64 expires = 0;          // unix seconds; 0 = never
    juce::int64 checkBy = 0;          // unix seconds
    juce::int64 serverTime = 0;       // unix seconds, when the server signed it
};

/** Parse and verify a token. Returns false for anything malformed, unsigned
    by `publicKey`, or issued for a different machine. */
bool parseToken (const juce::String& raw, const ed25519::PublicKey& publicKey,
                 const juce::String& machineId, Token& out);

enum class State
{
    welcome,     // never activated on this computer
    active,
    expired,     // launch period over / trial ended: buy to unlock
    checkIn      // a paid license that hasn't been able to check in for too long
};

struct Status
{
    State state = State::welcome;
    Type type = Type::none;
    juce::int64 expires = 0;          // for "open until ..." / "trial: n days left"
    juce::String email, checkoutUrl;
    bool isActive() const { return state == State::active; }
};

/** The rules, as a pure function (easy to test). `now` is the best known
    current time: max(local clock, newest server time seen). */
Status evaluate (const Token* token, juce::int64 now);

/** "open until 12 jan" / "trial · 9 days left" / "" (full). */
juce::String statusLine (const Status& s, juce::int64 now);

//==============================================================================
/** One per process (SharedResourcePointer). Keeps the token on disk, checks
    in with the server in the background and runs browser / key activation. */
class Manager : public juce::ChangeBroadcaster,
                private juce::Thread
{
public:
    Manager();
    ~Manager() override;

    /** Current status (cheap; any thread). */
    Status getStatus() const;
    juce::int64 now() const;

    /** Browser sign-in: opens the browser, then waits for the person to finish. */
    void startBrowserActivation();
    bool isWaitingForBrowser() const { return waitingForBrowser.load(); }
    void cancelBrowserActivation() { waitingForBrowser = false; }

    /** Backup path: activate with the key from the purchase email. */
    void activateWithKey (const juce::String& key);
    /** Check in now ("i already bought it"). */
    void refreshNow();
    /** Free this computer's seat and forget the license here. */
    void deactivateThisComputer();

    /** Last message for the UI ("couldn't reach the server", "key not found", ...). */
    juce::String getLastMessage() const;

    juce::String getMachineId() const { return machineId; }
    juce::String getAccountUrl() const { return serverBase() + "/account"; }

    //==========================================================================
    // Configuration (tests and development can override these)
    static juce::String serverBase();                 // https://license.prodbyeville.com (or ROOM_LICENSE_SERVER)
    static ed25519::PublicKey productionKey();
    static void setPublicKeyForTests (const ed25519::PublicKey&);
    static void setStorageFileForTests (const juce::File&);
    static void setMachineIdForTests (const juce::String&);
    /** For tests: pretend the local clock says this (0 = real clock). */
    static void setClockForTests (juce::int64 unixSeconds);
    /** For tests: install a token directly, as if the server had sent it. */
    bool installToken (const juce::String& raw);

    static constexpr juce::int64 checkEverySeconds = 3 * 24 * 3600;   // check in every 3 days

private:
    void run() override;
    void load();
    void save();
    bool post (const juce::String& path, const juce::var& body, juce::var& reply, juce::String& error);
    bool acceptReply (const juce::var& reply);
    void setMessage (const juce::String&);

    juce::String machineId, machineName;
    juce::File storage;
    mutable juce::CriticalSection lock;
    std::unique_ptr<Token> token;
    juce::int64 newestServerTime = 0;
    juce::int64 lastCheck = 0;
    juce::String lastMessage;

    enum class Job { none, refresh, browser, key, deactivate };
    std::atomic<Job> job { Job::none };
    juce::String pendingKey;
    std::atomic<bool> waitingForBrowser { false };
    juce::WaitableEvent wake;
};
} // namespace room::licensing
