// ROOM license server (Cloudflare Worker + D1).
//
// The plugin talks JSON to:
//   POST /device/start   {machine, name}            -> {code, url, interval}
//   POST /device/poll    {code}                     -> {pending} | {token, serverTime}
//   POST /activate/key   {key, machine, name}       -> {token, serverTime}
//   POST /refresh        {token, machine, name}     -> {token, serverTime} | {revoked}
//   POST /deactivate     {token, machine}           -> {ok}
// People use:
//   POST /signup                 email from the room page (launch window: launch license)
//   GET  /activate?code=         browser sign-in for the plugin (magic link by email)
//   GET  /account                licenses and computers, deactivate old ones
// Shopify calls:
//   POST /shopify/orders-paid       full license on the buyer's account
//   POST /shopify/orders-cancelled  revokes it again
//
// Settings live in wrangler.toml [vars] and secrets (see README.md).

import { signToken, verifyToken, sha256Hex, hmacSha256B64, safeEqual, randomHex, newLicenseKey } from './crypto.js';
import * as pages from './pages.js';

const DAY = 86400;

// ---------------------------------------------------------------------------
// config

function config(env) {
  const num = (v, d) => (v === undefined || v === '' ? d : Number(v));
  return {
    base: (env.PUBLIC_URL || 'https://license.prodbyeville.com').replace(/\/$/, ''),
    launchEndsAt: num(env.LAUNCH_ENDS_AT, 0),          // unix s; signups before this get a launch license
    lockAt: num(env.LOCK_AT, 0),                        // unix s; every launch license expires here
    trialDays: num(env.TRIAL_DAYS, 14),
    maxMachines: num(env.MAX_MACHINES, 3),
    graceDays: num(env.GRACE_DAYS, 14),                 // full licenses: offline grace
    shortCheckDays: num(env.SHORT_CHECK_DAYS, 3),       // launch and trial: check in this often
    checkoutLaunch: env.CHECKOUT_LAUNCH_URL || '',
    checkoutFull: env.CHECKOUT_FULL_URL || '',
    downloadMac: env.DOWNLOAD_MAC_URL || '',
    downloadWin: env.DOWNLOAD_WIN_URL || '',
    siteOrigin: env.SITE_ORIGIN || 'https://prodbyeville.com',
    support: env.SUPPORT_EMAIL || '',
    fromEmail: env.FROM_EMAIL || 'room <room@prodbyeville.com>',
    productIds: (env.SHOPIFY_PRODUCT_IDS || '').split(',').map((s) => s.trim()).filter(Boolean),
  };
}

const nowOf = (env) => (env.NOW ? env.NOW() : Math.floor(Date.now() / 1000));

// ---------------------------------------------------------------------------
// responses

function json(data, status = 200, headers = {}) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store', ...headers },
  });
}

function html(body, status = 200, headers = {}) {
  return new Response(body, {
    status,
    headers: {
      'content-type': 'text/html; charset=utf-8',
      'cache-control': 'no-store',
      'x-frame-options': 'DENY',
      'referrer-policy': 'no-referrer',
      ...headers,
    },
  });
}

const fail = (message, status = 400) => json({ error: message }, status);

async function readBody(request) {
  const type = request.headers.get('content-type') || '';
  if (type.includes('application/json')) {
    try {
      const v = await request.json();
      return v && typeof v === 'object' ? v : {};
    } catch {
      return {};
    }
  }
  if (type.includes('form')) return Object.fromEntries(await request.formData());
  return {};
}

const str = (v, max = 256) => (typeof v === 'string' ? v.trim().slice(0, max) : '');
const cleanEmail = (v) => {
  const e = str(v, 254).toLowerCase();
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(e) ? e : '';
};

// ---------------------------------------------------------------------------
// email

async function sendEmail(env, to, subject, text) {
  const cfg = config(env);
  if (!env.RESEND_API_KEY) {
    // Local development and tests: keep it instead of sending.
    (env.OUTBOX ||= []).push({ to, subject, text });
    return true;
  }
  const res = await fetch('https://api.resend.com/emails', {
    method: 'POST',
    headers: { authorization: `Bearer ${env.RESEND_API_KEY}`, 'content-type': 'application/json' },
    body: JSON.stringify({ from: cfg.fromEmail, to: [to], subject, text, reply_to: cfg.support || undefined }),
  });
  if (!res.ok) console.log('resend failed', res.status, await res.text());
  return res.ok;
}

// ---------------------------------------------------------------------------
// data

async function accountByEmail(env, email) {
  return env.DB.prepare('SELECT * FROM accounts WHERE email = ?').bind(email).first();
}

async function ensureAccount(env, email) {
  const found = await accountByEmail(env, email);
  if (found) return found;
  const id = randomHex(12);
  await env.DB.prepare('INSERT OR IGNORE INTO accounts (id, email, created_at) VALUES (?, ?, ?)').bind(id, email, nowOf(env)).run();
  return accountByEmail(env, email);
}

async function createLicense(env, accountId, type, expiresAt, orderId = null) {
  const cfg = config(env);
  for (let attempt = 0; attempt < 5; attempt++) {
    const id = randomHex(12);
    const key = newLicenseKey();
    try {
      await env.DB.prepare(
        'INSERT INTO licenses (id, account_id, type, key, created_at, expires_at, max_machines, order_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?)'
      ).bind(id, accountId, type, key, nowOf(env), expiresAt, cfg.maxMachines, orderId).run();
      return env.DB.prepare('SELECT * FROM licenses WHERE id = ?').bind(id).first();
    } catch (e) {
      if (orderId && /UNIQUE/i.test(String(e))) return env.DB.prepare('SELECT * FROM licenses WHERE order_id = ?').bind(orderId).first();
      if (attempt === 4) throw e;
    }
  }
}

const rank = (l) => (l.type === 'full' ? 2 : 1);

/** The license that counts for an account: full beats launch/trial, newest wins a tie. */
async function bestLicense(env, accountId) {
  const { results } = await env.DB.prepare('SELECT * FROM licenses WHERE account_id = ? AND revoked = 0').bind(accountId).all();
  if (!results.length) return null;
  return results.sort((a, b) => rank(b) - rank(a) || b.created_at - a.created_at)[0];
}

/** First activation: launch license during the launch window, otherwise a trial that starts now. */
async function licenseForActivation(env, account) {
  const existing = await bestLicense(env, account.id);
  if (existing) return existing;
  const cfg = config(env);
  const now = nowOf(env);
  if (now < cfg.launchEndsAt) return createLicense(env, account.id, 'launch', cfg.lockAt);
  return createLicense(env, account.id, 'trial', now + cfg.trialDays * DAY);
}

/** Puts this computer on the license. Returns an error message if it's full. */
async function attachMachine(env, license, machineId, name) {
  const now = nowOf(env);
  const here = await env.DB.prepare('SELECT 1 FROM machines WHERE license_id = ? AND machine_id = ?').bind(license.id, machineId).first();
  if (here) {
    await env.DB.prepare('UPDATE machines SET last_seen = ?, name = COALESCE(?, name) WHERE license_id = ? AND machine_id = ?')
      .bind(now, name || null, license.id, machineId).run();
    return null;
  }
  const { n } = await env.DB.prepare('SELECT COUNT(*) AS n FROM machines WHERE license_id = ?').bind(license.id).first();
  if (n >= license.max_machines)
    return `room is already on ${license.max_machines} computers. free one up at ${config(env).base}/account`;
  await env.DB.prepare('INSERT INTO machines (license_id, machine_id, name, activated_at, last_seen) VALUES (?, ?, ?, ?, ?)')
    .bind(license.id, machineId, name || null, now, now).run();
  return null;
}

async function machineOnAccount(env, accountId, machineId) {
  return env.DB.prepare(
    'SELECT m.license_id FROM machines m JOIN licenses l ON l.id = m.license_id WHERE l.account_id = ? AND m.machine_id = ? AND l.revoked = 0 LIMIT 1'
  ).bind(accountId, machineId).first();
}

function checkoutFor(env, license, email) {
  const cfg = config(env);
  const url = license.type === 'launch' ? cfg.checkoutLaunch : cfg.checkoutFull;
  if (!url) return '';
  // Shopify fills the checkout email from this, so the purchase lands on the same account.
  return url + (url.includes('?') ? '&' : '?') + 'checkout[email]=' + encodeURIComponent(email);
}

async function tokenReply(env, license, email, machineId) {
  const cfg = config(env);
  const now = nowOf(env);
  const full = license.type === 'full';
  const payload = {
    v: 1,
    email,
    license: license.id,
    type: license.type,
    machine: machineId,
    // Launch licenses follow LOCK_AT as it is now, so moving the date reaches everyone.
    expires: full ? 0 : license.type === 'launch' ? cfg.lockAt : license.expires_at || 0,
    checkBy: now + (full ? cfg.graceDays : cfg.shortCheckDays) * DAY,
    serverTime: now,
    checkout: full ? '' : checkoutFor(env, license, email),
    key: license.key,
  };
  return json({ token: await signToken(env.SIGNING_KEY, payload), serverTime: now });
}

/** Activate this computer on the account's best license (falls back to a license it's already on). */
async function activate(env, account, machineId, name) {
  let license = await licenseForActivation(env, account);
  const full = await attachMachine(env, license, machineId, name);
  if (full) {
    const on = await machineOnAccount(env, account.id, machineId);
    if (!on) return fail(full, 403);
    license = await env.DB.prepare('SELECT * FROM licenses WHERE id = ?').bind(on.license_id).first();
  }
  return tokenReply(env, license, account.email, machineId);
}

// ---------------------------------------------------------------------------
// plugin endpoints

const machineFrom = (body) => ({ machine: str(body.machine, 200), name: str(body.name, 100) });

async function deviceStart(request, env) {
  const { machine, name } = machineFrom(await readBody(request));
  if (!machine) return fail('missing machine');
  const now = nowOf(env);
  const code = randomHex(16);
  await env.DB.prepare('INSERT INTO devices (code, machine_id, name, created_at, expires_at) VALUES (?, ?, ?, ?, ?)')
    .bind(code, machine, name || null, now, now + 20 * 60).run();
  await env.DB.prepare('DELETE FROM devices WHERE expires_at < ?').bind(now - DAY).run();
  return json({ code, url: `${config(env).base}/activate?code=${code}`, interval: 3, serverTime: now });
}

async function devicePoll(request, env) {
  const code = str((await readBody(request)).code, 64);
  const device = code && (await env.DB.prepare('SELECT * FROM devices WHERE code = ?').bind(code).first());
  if (!device || device.expires_at < nowOf(env)) return fail('sign-in timed out. try again from room.', 410);
  if (!device.account_id) return json({ pending: true, serverTime: nowOf(env) });
  const account = await env.DB.prepare('SELECT * FROM accounts WHERE id = ?').bind(device.account_id).first();
  const reply = await activate(env, account, device.machine_id, device.name);
  await env.DB.prepare('DELETE FROM devices WHERE code = ?').bind(code).run();
  return reply;
}

async function activateKey(request, env) {
  const body = await readBody(request);
  const { machine, name } = machineFrom(body);
  const key = str(body.key, 64).toUpperCase().replace(/\s+/g, '');
  if (!machine || !key) return fail('paste the key from your purchase email.');
  const license = await env.DB.prepare('SELECT * FROM licenses WHERE key = ?').bind(key).first();
  if (!license || license.revoked) return fail("that key didn't work. check it against your purchase email.", 404);
  const account = await env.DB.prepare('SELECT * FROM accounts WHERE id = ?').bind(license.account_id).first();
  const full = await attachMachine(env, license, machine, name);
  if (full) return fail(full, 403);
  return tokenReply(env, license, account.email, machine);
}

async function tokenAccount(env, body) {
  const payload = await verifyToken(env.SIGNING_KEY, str(body.token, 8192));
  if (!payload || payload.machine !== str(body.machine, 200)) return null;
  const account = await accountByEmail(env, payload.email);
  return account ? { payload, account } : null;
}

async function refresh(request, env) {
  const body = await readBody(request);
  const found = await tokenAccount(env, body);
  if (!found) return fail('this license is not valid here.', 401);
  const { payload, account } = found;
  // Deactivated from the account page (or never activated on this account): forget it.
  if (!(await machineOnAccount(env, account.id, payload.machine))) return json({ revoked: true, serverTime: nowOf(env) });
  return activate(env, account, payload.machine, str(body.name, 100));
}

async function deactivate(request, env) {
  const found = await tokenAccount(env, await readBody(request));
  if (!found) return fail('this license is not valid here.', 401);
  await removeMachine(env, found.account.id, found.payload.machine);
  return json({ ok: true, serverTime: nowOf(env) });
}

async function removeMachine(env, accountId, machineId) {
  await env.DB.prepare('DELETE FROM machines WHERE machine_id = ? AND license_id IN (SELECT id FROM licenses WHERE account_id = ?)')
    .bind(machineId, accountId).run();
}

// ---------------------------------------------------------------------------
// people: signup, browser sign-in, account page

function cors(env, request) {
  const origin = request.headers.get('origin');
  return origin && origin === config(env).siteOrigin
    ? { 'access-control-allow-origin': origin, 'access-control-allow-headers': 'content-type', vary: 'origin' }
    : {};
}

async function signup(request, env) {
  const body = await readBody(request);
  const wantsHtml = !(request.headers.get('content-type') || '').includes('application/json');
  const email = cleanEmail(body.email);
  if (!email) return wantsHtml ? html(pages.message('that email looks off', 'go back and try again.'), 400) : fail('that email looks off.', 400);

  const cfg = config(env);
  const account = await ensureAccount(env, email);
  const now = nowOf(env);
  if (now < cfg.launchEndsAt && !(await bestLicense(env, account.id))) await createLicense(env, account.id, 'launch', cfg.lockAt);

  const lines = ['here is room.', ''];
  if (cfg.downloadMac) lines.push(`mac: ${cfg.downloadMac}`);
  if (cfg.downloadWin) lines.push(`windows: ${cfg.downloadWin}`);
  lines.push('', 'open room in your daw and press "start your session". sign in with this email and you are in.');
  if (now < cfg.launchEndsAt) lines.push('', `everything is open until ${pages.day(cfg.lockAt)}.`);
  await sendEmail(env, email, 'your copy of room', lines.join('\n'));

  return wantsHtml
    ? html(pages.message('check your email', 'the installer is on its way.'))
    : json({ ok: true }, 200, cors(env, request));
}

async function rateLimited(env, email) {
  const { n } = await env.DB.prepare('SELECT COUNT(*) AS n FROM logins WHERE email = ? AND created_at > ?')
    .bind(email, nowOf(env) - 3600).first();
  return n >= 6;
}

async function sendMagicLink(env, email, deviceCode, path, subject, intro) {
  const token = randomHex(24);
  const now = nowOf(env);
  await env.DB.prepare('INSERT INTO logins (token_hash, email, device_code, created_at, expires_at) VALUES (?, ?, ?, ?, ?)')
    .bind(await sha256Hex(token), email, deviceCode, now, now + 20 * 60).run();
  const link = `${config(env).base}${path}?t=${token}`;
  return sendEmail(env, email, subject, `${intro}\n\n${link}\n\nthe link works for 20 minutes. if you didn't ask for it, ignore this email.`);
}

async function useLogin(env, token) {
  const hash = await sha256Hex(str(token, 128));
  const login = await env.DB.prepare('SELECT * FROM logins WHERE token_hash = ?').bind(hash).first();
  if (!login || login.used || login.expires_at < nowOf(env)) return null;
  await env.DB.prepare('UPDATE logins SET used = 1 WHERE token_hash = ?').bind(hash).run();
  return login;
}

async function activatePage(request, env, url) {
  const code = str(url.searchParams.get('code'), 64);
  const device = code && (await env.DB.prepare('SELECT * FROM devices WHERE code = ?').bind(code).first());
  if (!device || device.expires_at < nowOf(env)) return html(pages.message('this link ran out', 'press "start your session" in room again.'), 410);
  return html(pages.signIn(code));
}

async function activateSubmit(request, env) {
  const body = await readBody(request);
  const code = str(body.code, 64);
  const email = cleanEmail(body.email);
  const device = code && (await env.DB.prepare('SELECT * FROM devices WHERE code = ?').bind(code).first());
  if (!device || device.expires_at < nowOf(env)) return html(pages.message('this link ran out', 'press "start your session" in room again.'), 410);
  if (!email) return html(pages.signIn(code, 'that email looks off.'), 400);
  if (await rateLimited(env, email)) return html(pages.message('slow down', 'too many sign-in emails. try again in an hour.'), 429);
  await sendMagicLink(env, email, code, '/activate/confirm', 'start your session', 'press the link to activate room on your computer.');
  return html(pages.message('check your email', `we sent a link to ${pages.esc(email)}. open it on any device.`));
}

// GET shows a button, POST does the work: mail scanners that prefetch links don't use them up.
async function activateConfirm(request, env, url) {
  if (request.method === 'GET') return html(pages.confirm('/activate/confirm', str(url.searchParams.get('t'), 128), 'activate room'));
  const login = await useLogin(env, (await readBody(request)).t);
  if (!login || !login.device_code) return html(pages.message('this link ran out', 'press "start your session" in room again.'), 410);
  const account = await ensureAccount(env, login.email);
  await env.DB.prepare('UPDATE devices SET account_id = ? WHERE code = ?').bind(account.id, login.device_code).run();
  return html(pages.message('room is open', 'go back to your daw. it takes a few seconds.'));
}

const SESSION_COOKIE = 'room_session';

async function sessionAccount(request, env) {
  const cookie = request.headers.get('cookie') || '';
  const m = cookie.match(new RegExp(`(?:^|;\\s*)${SESSION_COOKIE}=([a-f0-9]{48})`));
  if (!m) return null;
  const row = await env.DB.prepare('SELECT account_id FROM sessions WHERE id_hash = ? AND expires_at > ?').bind(await sha256Hex(m[1]), nowOf(env)).first();
  return row ? env.DB.prepare('SELECT * FROM accounts WHERE id = ?').bind(row.account_id).first() : null;
}

async function accountPage(request, env) {
  const account = await sessionAccount(request, env);
  if (!account) return html(pages.accountLogin());
  const { results: licenses } = await env.DB.prepare('SELECT * FROM licenses WHERE account_id = ? ORDER BY created_at').bind(account.id).all();
  for (const l of licenses) {
    if (l.type === 'launch') l.expires_at = config(env).lockAt;
    l.machines = (await env.DB.prepare('SELECT machine_id, name, last_seen FROM machines WHERE license_id = ? ORDER BY activated_at').bind(l.id).all()).results;
  }
  return html(pages.account(account.email, licenses, nowOf(env), config(env)));
}

async function accountLogin(request, env) {
  const email = cleanEmail((await readBody(request)).email);
  if (!email) return html(pages.accountLogin('that email looks off.'), 400);
  if (await rateLimited(env, email)) return html(pages.message('slow down', 'too many sign-in emails. try again in an hour.'), 429);
  // Same answer whether or not there's an account, so the page can't be used to look people up.
  if (await accountByEmail(env, email)) await sendMagicLink(env, email, null, '/account/auth', 'your room account', 'press the link to open your room account.');
  return html(pages.message('check your email', `if ${pages.esc(email)} has room, a sign-in link is on its way.`));
}

async function accountAuth(request, env, url) {
  if (request.method === 'GET') return html(pages.confirm('/account/auth', str(url.searchParams.get('t'), 128), 'open my account'));
  const login = await useLogin(env, (await readBody(request)).t);
  const account = login && !login.device_code && (await accountByEmail(env, login.email));
  if (!account) return html(pages.message('this link ran out', 'ask for a new one.'), 410);
  const id = randomHex(24);
  await env.DB.prepare('INSERT INTO sessions (id_hash, account_id, expires_at) VALUES (?, ?, ?)').bind(await sha256Hex(id), account.id, nowOf(env) + 30 * DAY).run();
  return new Response(null, {
    status: 303,
    headers: { location: '/account', 'set-cookie': `${SESSION_COOKIE}=${id}; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=${30 * DAY}` },
  });
}

function sameOrigin(request, env) {
  const origin = request.headers.get('origin');
  return !origin || origin === config(env).base;
}

async function accountDeactivate(request, env) {
  if (!sameOrigin(request, env)) return fail('bad origin', 403);
  const account = await sessionAccount(request, env);
  if (!account) return new Response(null, { status: 303, headers: { location: '/account' } });
  await removeMachine(env, account.id, str((await readBody(request)).machine, 200));
  return new Response(null, { status: 303, headers: { location: '/account' } });
}

async function accountLogout(request, env) {
  const cookie = request.headers.get('cookie') || '';
  const m = cookie.match(new RegExp(`(?:^|;\\s*)${SESSION_COOKIE}=([a-f0-9]{48})`));
  if (m) await env.DB.prepare('DELETE FROM sessions WHERE id_hash = ?').bind(await sha256Hex(m[1])).run();
  return new Response(null, { status: 303, headers: { location: '/account', 'set-cookie': `${SESSION_COOKIE}=; Path=/; Max-Age=0` } });
}

// ---------------------------------------------------------------------------
// shopify

async function shopifyOrder(request, env) {
  const raw = await request.text();
  const sent = request.headers.get('x-shopify-hmac-sha256') || '';
  if (!env.SHOPIFY_WEBHOOK_SECRET || !safeEqual(await hmacSha256B64(env.SHOPIFY_WEBHOOK_SECRET, raw), sent)) return fail('bad signature', 401);
  let order;
  try {
    order = JSON.parse(raw);
  } catch {
    return fail('bad body');
  }
  const cfg = config(env);
  const isRoom = (item) =>
    cfg.productIds.length ? cfg.productIds.includes(String(item.product_id)) : /\broom\b/i.test(String(item.title || item.name || ''));
  const email = cleanEmail(order.email || order.contact_email || order.customer?.email);
  const orderId = String(order.id || '');
  return { order, email, orderId, hasRoom: (order.line_items || []).some(isRoom) };
}

async function shopifyPaid(request, env) {
  const parsed = await shopifyOrder(request, env);
  if (parsed instanceof Response) return parsed;
  const { email, orderId, hasRoom } = parsed;
  if (!hasRoom || !email || !orderId) return json({ ok: true, skipped: true });   // 200 so Shopify doesn't retry

  const already = await env.DB.prepare('SELECT 1 FROM licenses WHERE order_id = ?').bind(orderId).first();
  if (already) return json({ ok: true, duplicate: true });

  const account = await ensureAccount(env, email);
  const license = await createLicense(env, account.id, 'full', null, orderId);
  const cfg = config(env);
  await sendEmail(env, email, 'room is yours',
    [
      'thank you. room is yours.',
      '',
      'if room is already on your computer, it unlocks on its own the next time it loads.',
      'new computer: open room, press "start your session" and sign in with this email.',
      '',
      `your key, for computers that can't sign in through the browser: ${license.key}`,
      '',
      `manage your computers: ${cfg.base}/account`,
    ].join('\n'));
  return json({ ok: true });
}

async function shopifyCancelled(request, env) {
  const parsed = await shopifyOrder(request, env);
  if (parsed instanceof Response) return parsed;
  if (parsed.orderId) await env.DB.prepare('UPDATE licenses SET revoked = 1 WHERE order_id = ?').bind(parsed.orderId).run();
  return json({ ok: true });
}

// ---------------------------------------------------------------------------

const routes = {
  'POST /device/start': deviceStart,
  'POST /device/poll': devicePoll,
  'POST /activate/key': activateKey,
  'POST /refresh': refresh,
  'POST /deactivate': deactivate,
  'POST /signup': signup,
  'GET /activate': activatePage,
  'POST /activate': activateSubmit,
  'GET /activate/confirm': activateConfirm,
  'POST /activate/confirm': activateConfirm,
  'GET /account': accountPage,
  'POST /account/login': accountLogin,
  'GET /account/auth': accountAuth,
  'POST /account/auth': accountAuth,
  'POST /account/deactivate': accountDeactivate,
  'POST /account/logout': accountLogout,
  'POST /shopify/orders-paid': shopifyPaid,
  'POST /shopify/orders-cancelled': shopifyCancelled,
};

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === 'OPTIONS' && url.pathname === '/signup')
      return new Response(null, { status: 204, headers: { ...cors(env, request), 'access-control-allow-methods': 'POST' } });
    if (request.method === 'GET' && url.pathname === '/') return Response.redirect(config(env).siteOrigin + '/room', 302);
    const handler = routes[`${request.method} ${url.pathname}`];
    if (!handler) return fail('not found', 404);
    try {
      return await handler(request, env, url);
    } catch (e) {
      console.log('error', url.pathname, e && e.stack);
      return fail('something went wrong on our side. try again in a minute.', 500);
    }
  },
};
