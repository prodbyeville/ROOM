// Signing, hashing and random helpers (Web Crypto: works in Workers and Node).

const enc = new TextEncoder();

export function b64url(bytes) {
  let s = '';
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

export function fromB64url(str) {
  const s = str.replace(/-/g, '+').replace(/_/g, '/');
  const bin = atob(s + '='.repeat((4 - (s.length % 4)) % 4));
  return Uint8Array.from(bin, (c) => c.charCodeAt(0));
}

export function fromB64(str) {
  return Uint8Array.from(atob(str), (c) => c.charCodeAt(0));
}

// PKCS#8 wrapper for a raw 32-byte Ed25519 private key (seed).
const PKCS8_PREFIX = Uint8Array.from([0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20]);

const keyCache = new Map();

/** SIGNING_KEY is the base64 of the 32-byte seed (scripts/make-keys.mjs). */
export async function signingKeys(seedB64) {
  if (keyCache.has(seedB64)) return keyCache.get(seedB64);
  const seed = fromB64(seedB64);
  if (seed.length !== 32) throw new Error('SIGNING_KEY must be 32 bytes, base64');
  const pkcs8 = new Uint8Array(PKCS8_PREFIX.length + 32);
  pkcs8.set(PKCS8_PREFIX);
  pkcs8.set(seed, PKCS8_PREFIX.length);
  const privateKey = await crypto.subtle.importKey('pkcs8', pkcs8, { name: 'Ed25519' }, true, ['sign']);
  const jwk = await crypto.subtle.exportKey('jwk', privateKey);
  const publicKey = await crypto.subtle.importKey('jwk', { kty: 'OKP', crv: 'Ed25519', x: jwk.x }, { name: 'Ed25519' }, true, ['verify']);
  const keys = { privateKey, publicKey, publicRaw: fromB64url(jwk.x) };
  keyCache.set(seedB64, keys);
  return keys;
}

/** token = b64url(json) "." b64url(signature of the json bytes) */
export async function signToken(seedB64, payload) {
  const { privateKey } = await signingKeys(seedB64);
  const body = enc.encode(JSON.stringify(payload));
  const sig = new Uint8Array(await crypto.subtle.sign({ name: 'Ed25519' }, privateKey, body));
  return b64url(body) + '.' + b64url(sig);
}

/** Returns the payload if the signature is ours, else null. */
export async function verifyToken(seedB64, token) {
  if (typeof token !== 'string' || token.length > 8192) return null;
  const dot = token.indexOf('.');
  if (dot <= 0) return null;
  try {
    const body = fromB64url(token.slice(0, dot));
    const sig = fromB64url(token.slice(dot + 1));
    const { publicKey } = await signingKeys(seedB64);
    const ok = await crypto.subtle.verify({ name: 'Ed25519' }, publicKey, sig, body);
    return ok ? JSON.parse(new TextDecoder().decode(body)) : null;
  } catch {
    return null;
  }
}

export async function sha256Hex(text) {
  const d = new Uint8Array(await crypto.subtle.digest('SHA-256', enc.encode(text)));
  return [...d].map((b) => b.toString(16).padStart(2, '0')).join('');
}

export async function hmacSha256B64(secret, raw) {
  const key = await crypto.subtle.importKey('raw', enc.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', key, typeof raw === 'string' ? enc.encode(raw) : raw));
  let s = '';
  for (const b of mac) s += String.fromCharCode(b);
  return btoa(s);
}

/** Constant-time string compare. */
export function safeEqual(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

export function randomHex(bytes = 16) {
  const a = crypto.getRandomValues(new Uint8Array(bytes));
  return [...a].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// Keys look like ROOM-7KQ2-M9XD-4TPW-HC3F (no 0/O/1/I/L/U to misread).
const KEY_ALPHABET = '23456789ABCDEFGHJKMNPQRSTVWXYZ';
export function newLicenseKey() {
  const a = crypto.getRandomValues(new Uint8Array(16));
  const chars = [...a].map((b) => KEY_ALPHABET[b % KEY_ALPHABET.length]).join('');
  return 'ROOM-' + chars.match(/.{4}/g).join('-');
}
