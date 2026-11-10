#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

// License signing (v6.1, WARP 1.0): the license server signs each activation with its RSA private
// key; the plugin checks it with the public key compiled in (LicensePublicKey.h), so a license file
// can't be made or edited without the server. The signature is plain RSA over the SHA-256 of a
// canonical text (the same text on both sides; see payload()):
//   signature = sha256 (payload) ^ d mod n        (server, Server/src/index.js: signPayload)
//   valid     = signature ^ e mod n == sha256 (payload)    (plugin: verify)
// Keys are juce::RSAKey strings ("exponent,modulus" in hex), made by Tools/LicenseTool.
namespace LicenseCrypto
{
    struct License
    {
        juce::String product, email, key, machineId, issuedAt, signature;
    };

    // The exact text that is signed. Any change here must be matched in Server/src/index.js.
    inline juce::String payload (const License& l)
    {
        return "warp-license-v1\n" + l.product + "\n" + l.email.trim().toLowerCase() + "\n" + l.key.trim().toUpperCase()
             + "\n" + l.machineId + "\n" + l.issuedAt;
    }

    inline juce::BigInteger hashOf (const juce::String& text)
    {
        const juce::SHA256 sha (text.toUTF8());
        juce::BigInteger value;
        value.parseString (sha.toHexString(), 16);
        return value;
    }

    inline bool verify (const juce::String& text, const juce::String& signatureHex, const juce::String& publicKey)
    {
        if (! publicKey.containsChar (',') || signatureHex.isEmpty() || ! signatureHex.containsOnly ("0123456789abcdefABCDEF"))
            return false;
        juce::BigInteger value;
        value.parseString (signatureHex, 16);
        if (value.isZero())
            return false;
        const juce::RSAKey key (publicKey);
        juce::BigInteger modulus;
        modulus.parseString (publicKey.fromFirstOccurrenceOf (",", false, false), 16);
        if (value >= modulus)
            return false;   // a real signature is always below the modulus
        if (! key.applyToValue (value))
            return false;
        return value == hashOf (text);
    }

    // For the tools and tests only: the server signs in JavaScript.
    inline juce::String sign (const juce::String& text, const juce::String& privateKey)
    {
        auto value = hashOf (text);
        juce::RSAKey (privateKey).applyToValue (value);
        return value.toString (16);
    }

    // The free launch week (WARP 1.0.3): the server signs its dates (and the time it answered) with
    // the same key, so the plugin can trust them offline and nobody can stretch them by editing a
    // file. All times are ISO 8601 UTC strings, signed exactly as the server sent them.
    struct FreeWeek
    {
        juce::String product, startsAt, endsAt, issuedAt, signature;
    };

    // The exact text that is signed (Server/src/index.js: freeWeekPayload).
    inline juce::String freeWeekPayload (const FreeWeek& w)
    {
        return "warp-freeweek-v1\n" + w.product + "\n" + w.startsAt + "\n" + w.endsAt + "\n" + w.issuedAt;
    }

    inline juce::var toVar (const FreeWeek& w)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("product", w.product);
        o->setProperty ("startsAt", w.startsAt);
        o->setProperty ("endsAt", w.endsAt);
        o->setProperty ("issuedAt", w.issuedAt);
        o->setProperty ("signature", w.signature);
        return juce::var (o);
    }

    inline FreeWeek freeWeekFromVar (const juce::var& v)
    {
        return { v["product"].toString(), v["startsAt"].toString(), v["endsAt"].toString(),
                 v["issuedAt"].toString(), v["signature"].toString() };
    }

    inline juce::var toVar (const License& l)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("product", l.product);
        o->setProperty ("email", l.email);
        o->setProperty ("key", l.key);
        o->setProperty ("machineId", l.machineId);
        o->setProperty ("issuedAt", l.issuedAt);
        o->setProperty ("signature", l.signature);
        return juce::var (o);
    }

    inline License fromVar (const juce::var& v)
    {
        return { v["product"].toString(), v["email"].toString(), v["key"].toString(),
                 v["machineId"].toString(), v["issuedAt"].toString(), v["signature"].toString() };
    }
}
