#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Standard Ed25519 (RFC 8032), built from Monocypher's EdDSA building blocks
// plus SHA-512. Matches what the license server signs with (Web Crypto
// "Ed25519"). Only verify() runs in the plugin; sign() is for tests and tools.

namespace room::ed25519
{
using PublicKey = std::array<uint8_t, 32>;
using Seed      = std::array<uint8_t, 32>;   // the 32-byte private key
using Signature = std::array<uint8_t, 64>;
using Digest512 = std::array<uint8_t, 64>;

Digest512 sha512 (const void* data, size_t size);

/** True if sig is a valid signature of msg by pub. */
bool verify (const Signature& sig, const PublicKey& pub, const void* msg, size_t size);

PublicKey publicKeyFromSeed (const Seed& seed);
Signature sign (const Seed& seed, const void* msg, size_t size);
} // namespace room::ed25519
