// Runs the worker locally on node:sqlite, for trying the plugin against it.
//   node test/serve.mjs [port]
// Signs with the same throwaway key as room_tests (never the real one).
// Test-only extras, never in the worker itself:
//   POST /__clock {t}       move the server clock (unix seconds)
//   POST /__buy {email}     a paid Shopify order; replies with the key
//   AUTO_APPROVE=email      browser sign-ins approve themselves as that email
import { createServer } from 'node:http';
import worker from '../src/index.js';
import { makeD1 } from './d1-shim.mjs';
import { hmacSha256B64 } from '../src/crypto.js';

const port = Number(process.argv[2] || 8787);
const seed = Uint8Array.from({ length: 32 }, (_, i) => i * 7 + 3);
const now = Math.floor(Date.now() / 1000);
const env = {
  DB: makeD1(),
  SIGNING_KEY: btoa(String.fromCharCode(...seed)),
  PUBLIC_URL: `http://127.0.0.1:${port}`,
  LAUNCH_ENDS_AT: String(process.env.LAUNCH_ENDS_AT || now + 10 * 86400),
  LOCK_AT: String(process.env.LOCK_AT || now + 40 * 86400),
  CHECKOUT_LAUNCH_URL: 'https://prodbyeville.com/launch',
  CHECKOUT_FULL_URL: 'https://prodbyeville.com/full',
  SHOPIFY_WEBHOOK_SECRET: 'local',
  OUTBOX: [],
  clock: 0,
};
env.NOW = () => env.clock || Math.floor(Date.now() / 1000);

async function extra(path, body) {
  if (path === '/__clock') { env.clock = Number(body.t) || 0; return { ok: true }; }
  if (path === '/__buy') {
    const raw = JSON.stringify({ id: Date.now(), email: body.email, line_items: [{ product_id: 1, title: 'room' }] });
    await worker.fetch(new Request(env.PUBLIC_URL + '/shopify/orders-paid', {
      method: 'POST', body: raw,
      headers: { 'content-type': 'application/json', 'x-shopify-hmac-sha256': await hmacSha256B64('local', raw) },
    }), env);
    return { key: env.OUTBOX.at(-1).text.match(/ROOM-[A-Z0-9-]+/)[0] };
  }
  return null;
}

createServer(async (req, res) => {
  const chunks = [];
  for await (const c of req) chunks.push(c);
  const body = Buffer.concat(chunks);
  const url = new URL(req.url, env.PUBLIC_URL);
  const special = req.method === 'POST' && url.pathname.startsWith('/__') ? await extra(url.pathname, JSON.parse(body || '{}')) : null;
  if (special) { res.writeHead(200, { 'content-type': 'application/json' }); res.end(JSON.stringify(special)); return; }

  const r = await worker.fetch(new Request(url, {
    method: req.method, headers: req.headers, body: ['GET', 'HEAD'].includes(req.method) ? undefined : body,
  }), env);
  const text = await r.text();
  if (url.pathname === '/device/start' && r.ok && process.env.AUTO_APPROVE) {
    const { code } = JSON.parse(text);
    const email = process.env.AUTO_APPROVE;
    await env.DB.prepare('INSERT OR IGNORE INTO accounts (id, email, created_at) VALUES (?, ?, ?)').bind('auto-' + email, email, env.NOW()).run();
    const acct = await env.DB.prepare('SELECT id FROM accounts WHERE email = ?').bind(email).first();
    await env.DB.prepare('UPDATE devices SET account_id = ? WHERE code = ?').bind(acct.id, code).run();
  }
  console.log(req.method, url.pathname, r.status);
  res.writeHead(r.status, Object.fromEntries(r.headers));
  res.end(text);
}).listen(port, '127.0.0.1', () => console.log(`room license server on http://127.0.0.1:${port}`));
