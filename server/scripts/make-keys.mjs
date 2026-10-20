// Makes the signing key pair for the license server.
//   node scripts/make-keys.mjs
// Prints the secret for `wrangler secret put SIGNING_KEY` and the public key
// to paste into plugin/Source/licensing/License.cpp (kProductionKey).
// Run it once. Keep the secret somewhere safe: a new key logs everyone out.
import { signingKeys } from '../src/crypto.js';

const seed = crypto.getRandomValues(new Uint8Array(32));
const seedB64 = btoa(String.fromCharCode(...seed));
const { publicRaw } = await signingKeys(seedB64);
const hex = [...publicRaw].map((b) => '0x' + b.toString(16).padStart(2, '0'));

console.log('SIGNING_KEY (secret, for wrangler secret put SIGNING_KEY):\n');
console.log('  ' + seedB64 + '\n');
console.log('kProductionKey (public, for plugin/Source/licensing/License.cpp):\n');
console.log('    constexpr uint8_t kProductionKey[32] = {');
console.log('        ' + hex.slice(0, 16).join(', ') + ',');
console.log('        ' + hex.slice(16).join(', ') + ' };');
