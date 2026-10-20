// node --test test/   (Node 22+, nothing to install)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import worker from '../src/index.js';
import { makeD1 } from './d1-shim.mjs';
import { hmacSha256B64, verifyToken } from '../src/crypto.js';

const DAY = 86400;
const LAUNCH_ENDS = 1_800_000_000;
const LOCK = LAUNCH_ENDS + 30 * DAY;
const SEED = btoa(String.fromCharCode(...new Uint8Array(32).map((_, i) => i + 1)));
const SECRET = 'shopify-test-secret';

function makeEnv(start = LAUNCH_ENDS - 10 * DAY) {
  const env = {
    DB: makeD1(),
    SIGNING_KEY: SEED,
    PUBLIC_URL: 'https://license.test',
    LAUNCH_ENDS_AT: String(LAUNCH_ENDS),
    LOCK_AT: String(LOCK),
    TRIAL_DAYS: '14',
    MAX_MACHINES: '2',
    CHECKOUT_LAUNCH_URL: 'https://shop.test/launch',
    CHECKOUT_FULL_URL: 'https://shop.test/full',
    SHOPIFY_WEBHOOK_SECRET: SECRET,
    OUTBOX: [],
    clock: start,
  };
  env.NOW = () => env.clock;
  return env;
}

async function call(env, method, path, body, { form = false, headers = {} } = {}) {
  const init = { method, headers: { ...headers } };
  if (body !== undefined) {
    if (form) {
      init.body = new URLSearchParams(body).toString();
      init.headers['content-type'] = 'application/x-www-form-urlencoded';
    } else {
      init.body = JSON.stringify(body);
      init.headers['content-type'] = 'application/json';
    }
  }
  const res = await worker.fetch(new Request('https://license.test' + path, init), env);
  const type = res.headers.get('content-type') || '';
  return { status: res.status, headers: res.headers, body: type.includes('json') ? await res.json() : await res.text() };
}

const payloadOf = async (token) => verifyToken(SEED, token);
const lastLink = (env) => env.OUTBOX.at(-1).text.match(/https:\/\/license\.test(\S+)\?t=([a-f0-9]+)/);

/** The whole browser sign-in, as the plugin and the person do it. */
async function browserActivate(env, email, machine = 'mac-1') {
  const start = await call(env, 'POST', '/device/start', { machine, name: 'studio mac' });
  assert.equal(start.status, 200);
  const code = new URL(start.body.url).searchParams.get('code');
  assert.equal((await call(env, 'POST', '/device/poll', { code })).body.pending, true);

  assert.equal((await call(env, 'GET', `/activate?code=${code}`)).status, 200);
  assert.equal((await call(env, 'POST', '/activate', { code, email }, { form: true })).status, 200);
  const [, path, t] = lastLink(env);
  assert.equal(path, '/activate/confirm');
  // A prefetching mail scanner only GETs: that must not use the link up.
  await call(env, 'GET', `/activate/confirm?t=${t}`);
  const done = await call(env, 'POST', '/activate/confirm', { t }, { form: true });
  assert.match(done.body, /room is open/);
  assert.equal((await call(env, 'POST', '/activate/confirm', { t }, { form: true })).status, 410, 'links work once');

  const poll = await call(env, 'POST', '/device/poll', { code });
  assert.equal(poll.status, 200, JSON.stringify(poll.body));
  return poll.body.token;
}

function shopify(env, path, order, secret = SECRET) {
  const raw = JSON.stringify(order);
  return hmacSha256B64(secret, raw).then((sig) =>
    worker.fetch(new Request('https://license.test' + path, {
      method: 'POST',
      headers: { 'content-type': 'application/json', 'x-shopify-hmac-sha256': sig },
      body: raw,
    }), env).then(async (r) => ({ status: r.status, body: await r.json() })));
}

const roomOrder = (id, email) => ({ id, email, line_items: [{ product_id: 1, title: 'room' }] });

test('launch window: signup, browser sign-in, launch license until the lock date', async () => {
  const env = makeEnv();
  const s = await call(env, 'POST', '/signup', { email: 'Ann@Example.com ' });
  assert.equal(s.status, 200);
  assert.equal(env.OUTBOX[0].to, 'ann@example.com');
  assert.match(env.OUTBOX[0].text, /open until/);

  const p = await payloadOf(await browserActivate(env, 'ann@example.com'));
  assert.equal(p.type, 'launch');
  assert.equal(p.expires, LOCK);
  assert.equal(p.machine, 'mac-1');
  assert.equal(p.checkBy, env.clock + 3 * DAY);
  assert.match(p.checkout, /^https:\/\/shop\.test\/launch\?checkout\[email\]=ann%40example\.com$/);
  assert.match(p.key, /^ROOM-[2-9A-Z]{4}-[2-9A-Z]{4}-[2-9A-Z]{4}-[2-9A-Z]{4}$/);
});

test('every launch license ends on the lock date, by server time', async () => {
  const env = makeEnv();
  await call(env, 'POST', '/signup', { email: 'ann@example.com' });
  const token = await browserActivate(env, 'ann@example.com');
  env.clock = LOCK + 60;
  const r = await call(env, 'POST', '/refresh', { token, machine: 'mac-1' });
  const p = await payloadOf(r.body.token);
  assert.equal(p.type, 'launch');
  assert.equal(p.expires, LOCK);
  assert.ok(p.serverTime > p.expires, 'the plugin sees an expired token with the server time past it');
});

test('buying unlocks on the next check, no key to type', async () => {
  const env = makeEnv();
  await call(env, 'POST', '/signup', { email: 'ann@example.com' });
  const token = await browserActivate(env, 'ann@example.com');
  env.clock = LOCK + DAY;

  const paid = await shopify(env, '/shopify/orders-paid', roomOrder(5001, 'ANN@example.com'));
  assert.equal(paid.status, 200);
  assert.match(env.OUTBOX.at(-1).text, /ROOM-/);
  assert.equal((await shopify(env, '/shopify/orders-paid', roomOrder(5001, 'ann@example.com'))).body.duplicate, true);

  const r = await call(env, 'POST', '/refresh', { token, machine: 'mac-1' });
  const p = await payloadOf(r.body.token);
  assert.equal(p.type, 'full');
  assert.equal(p.expires, 0);
  assert.equal(p.checkBy, env.clock + 14 * DAY, 'paid: 14 days offline');
  assert.equal(p.checkout, '');
});

test('shopify: bad signature, other products', async () => {
  const env = makeEnv();
  assert.equal((await shopify(env, '/shopify/orders-paid', roomOrder(1, 'a@b.co'), 'wrong')).status, 401);
  const other = await shopify(env, '/shopify/orders-paid', { id: 2, email: 'a@b.co', line_items: [{ product_id: 9, title: 'hoodie' }] });
  assert.equal(other.body.skipped, true);
  env.SHOPIFY_PRODUCT_IDS = '777';
  assert.equal((await shopify(env, '/shopify/orders-paid', roomOrder(3, 'a@b.co'))).body.skipped, true);
  const byId = await shopify(env, '/shopify/orders-paid', { id: 4, email: 'a@b.co', line_items: [{ product_id: 777, title: 'x' }] });
  assert.equal(byId.body.ok, true);
  assert.equal(byId.body.skipped, undefined);
});

test('a refund takes it back', async () => {
  const env = makeEnv(LOCK + DAY);
  await shopify(env, '/shopify/orders-paid', roomOrder(77, 'bo@example.com'));
  const token = await browserActivate(env, 'bo@example.com');
  assert.equal((await payloadOf(token)).type, 'full');
  await shopify(env, '/shopify/orders-cancelled', roomOrder(77, 'bo@example.com'));
  const r = await call(env, 'POST', '/refresh', { token, machine: 'mac-1' });
  assert.equal(r.body.revoked, true);
});

test('after the offer: trial starts at first open, not at signup', async () => {
  const env = makeEnv(LAUNCH_ENDS + DAY);
  await call(env, 'POST', '/signup', { email: 'cy@example.com' });
  assert.doesNotMatch(env.OUTBOX[0].text, /open until/);
  env.clock += 5 * DAY;   // downloaded, opened five days later
  const p = await payloadOf(await browserActivate(env, 'cy@example.com'));
  assert.equal(p.type, 'trial');
  assert.equal(p.expires, env.clock + 14 * DAY);
  assert.match(p.checkout, /shop\.test\/full/);
});

test('people who never signed up can still start a trial', async () => {
  const env = makeEnv(LAUNCH_ENDS + DAY);
  const p = await payloadOf(await browserActivate(env, 'new@example.com'));
  assert.equal(p.type, 'trial');
});

test('key activation, machine limit, self-serve deactivation', async () => {
  const env = makeEnv(LOCK + DAY);
  await shopify(env, '/shopify/orders-paid', roomOrder(9, 'dee@example.com'));
  const key = env.OUTBOX.at(-1).text.match(/ROOM-[A-Z0-9-]+/)[0];

  assert.equal((await call(env, 'POST', '/activate/key', { key: 'ROOM-NOPE', machine: 'a' })).status, 404);
  const a = await call(env, 'POST', '/activate/key', { key: ` ${key.toLowerCase()} `, machine: 'a', name: 'studio' });
  assert.equal(a.status, 200);
  assert.equal((await call(env, 'POST', '/activate/key', { key, machine: 'b' })).status, 200);
  const c = await call(env, 'POST', '/activate/key', { key, machine: 'c' });
  assert.equal(c.status, 403);
  assert.match(c.body.error, /already on 2 computers/);
  assert.equal((await call(env, 'POST', '/activate/key', { key, machine: 'a' })).status, 200, 'the same computer again is fine');

  // Account page: magic link, cookie, deactivate "a".
  assert.match((await call(env, 'GET', '/account')).body, /send me a sign-in link/);
  await call(env, 'POST', '/account/login', { email: 'dee@example.com' }, { form: true });
  const [, path, t] = lastLink(env);
  assert.equal(path, '/account/auth');
  const auth = await call(env, 'POST', '/account/auth', { t }, { form: true });
  assert.equal(auth.status, 303);
  const cookie = auth.headers.get('set-cookie').split(';')[0];
  const page = await call(env, 'GET', '/account', undefined, { headers: { cookie } });
  assert.match(page.body, /studio/);
  assert.match(page.body, new RegExp(key));

  assert.equal((await call(env, 'POST', '/account/deactivate', { machine: 'a' }, { form: true, headers: { cookie, origin: 'https://evil.test' } })).status, 403);
  assert.equal((await call(env, 'POST', '/account/deactivate', { machine: 'a' }, { form: true, headers: { cookie } })).status, 303);

  // "a" is told on its next check; "c" now fits.
  assert.equal((await call(env, 'POST', '/refresh', { token: a.body.token, machine: 'a' })).body.revoked, true);
  assert.equal((await call(env, 'POST', '/activate/key', { key, machine: 'c' })).status, 200);
});

test('unknown emails get the same answer on the account page', async () => {
  const env = makeEnv();
  const r = await call(env, 'POST', '/account/login', { email: 'nobody@example.com' }, { form: true });
  assert.match(r.body, /if nobody@example.com has room/);
  assert.equal(env.OUTBOX.length, 0);
});

test('tokens: forged, other machine, deactivate from the plugin', async () => {
  const env = makeEnv();
  const token = await browserActivate(env, 'ed@example.com');
  assert.equal((await call(env, 'POST', '/refresh', { token, machine: 'someone-else' })).status, 401);
  const [body] = token.split('.');
  assert.equal((await call(env, 'POST', '/refresh', { token: body + '.AAAA', machine: 'mac-1' })).status, 401);
  assert.equal((await call(env, 'POST', '/deactivate', { token, machine: 'mac-1' })).body.ok, true);
  assert.equal((await call(env, 'POST', '/refresh', { token, machine: 'mac-1' })).body.revoked, true);
  // ...and the browser sign-in works again afterwards.
  assert.equal((await payloadOf(await browserActivate(env, 'ed@example.com'))).type, 'launch');
});

test('sign-in links: expiry, rate limit, device timeout', async () => {
  const env = makeEnv();
  const start = await call(env, 'POST', '/device/start', { machine: 'm' });
  const code = start.body.code;
  await call(env, 'POST', '/activate', { code, email: 'fi@example.com' }, { form: true });
  const [, , t] = lastLink(env);
  env.clock += 21 * 60;
  assert.equal((await call(env, 'POST', '/activate/confirm', { t }, { form: true })).status, 410);
  assert.equal((await call(env, 'POST', '/device/poll', { code })).status, 410);

  const fresh = (await call(env, 'POST', '/device/start', { machine: 'm' })).body.code;
  for (let i = 0; i < 5; i++) await call(env, 'POST', '/activate', { code: fresh, email: 'fi@example.com' }, { form: true });
  assert.equal((await call(env, 'POST', '/activate', { code: fresh, email: 'fi@example.com' }, { form: true })).status, 429);
});

test('signup: bad email, CORS for the room page, html form posts', async () => {
  const env = makeEnv();
  assert.equal((await call(env, 'POST', '/signup', { email: 'nope' })).status, 400);
  const ok = await call(env, 'POST', '/signup', { email: 'g@example.com' }, { headers: { origin: 'https://prodbyeville.com' } });
  assert.equal(ok.headers.get('access-control-allow-origin'), 'https://prodbyeville.com');
  const formPost = await call(env, 'POST', '/signup', { email: 'h@example.com' }, { form: true });
  assert.match(formPost.body, /check your email/);
  assert.equal((await call(env, 'GET', '/nope')).status, 404);
});
