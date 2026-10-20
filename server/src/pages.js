// The few pages people see: sign-in for the plugin, the account page, short messages.
// Brand: lowercase, quiet, #0A0A0A / #BA2727 / #2A2A2A / #F5F3EF.

export const esc = (s) =>
  String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);

const MONTHS = ['jan', 'feb', 'mar', 'apr', 'may', 'jun', 'jul', 'aug', 'sep', 'oct', 'nov', 'dec'];
export function day(unix) {
  const d = new Date(unix * 1000);
  return `${d.getUTCDate()} ${MONTHS[d.getUTCMonth()]} ${d.getUTCFullYear()}`;
}

function page(title, body) {
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>${esc(title)} · room</title>
<style>
  :root { --bg:#0A0A0A; --panel:#141414; --line:#2A2A2A; --ink:#F5F3EF; --dim:#8d8a85; --red:#BA2727; }
  * { box-sizing: border-box; }
  body { margin:0; min-height:100vh; display:flex; align-items:center; justify-content:center; padding:24px 16px;
         background:var(--bg); color:var(--ink); font:15px/1.5 -apple-system, "Helvetica Neue", Helvetica, Arial, sans-serif; }
  main { width:100%; max-width:420px; }
  .mark { letter-spacing:.32em; font-size:12px; color:var(--dim); margin-bottom:28px; }
  .mark b { color:var(--red); font-weight:600; }
  h1 { font-size:22px; font-weight:500; margin:0 0 8px; }
  p { color:var(--dim); margin:0 0 20px; }
  form { margin:0; }
  input[type=email] { width:100%; padding:12px 14px; margin-bottom:12px; border-radius:8px; border:1px solid var(--line);
         background:var(--panel); color:var(--ink); font:inherit; }
  input[type=email]:focus { outline:none; border-color:var(--red); }
  button { width:100%; padding:12px 14px; border:0; border-radius:8px; background:var(--red); color:var(--ink); font:inherit; cursor:pointer; }
  button.quiet { width:auto; padding:6px 10px; background:transparent; border:1px solid var(--line); color:var(--dim); font-size:13px; }
  button.quiet:hover { color:var(--ink); border-color:var(--dim); }
  .err { color:#e06a6a; }
  .card { border:1px solid var(--line); border-radius:10px; padding:16px; margin-bottom:14px; background:var(--panel); }
  .row { display:flex; align-items:center; justify-content:space-between; gap:12px; padding:10px 0; border-top:1px solid var(--line); }
  .row:first-of-type { border-top:0; }
  .meta { color:var(--dim); font-size:13px; }
  code { font-family: ui-monospace, Menlo, Consolas, monospace; font-size:13px; color:var(--ink); }
  a { color:var(--ink); }
</style>
</head>
<body><main><div class="mark"><b>●</b> ROOM</div>${body}</main></body>
</html>`;
}

export function message(title, text) {
  return page(title, `<h1>${esc(title)}</h1><p>${text}</p>`);
}

export function signIn(code, error = '') {
  return page('start your session', `
<h1>start your session</h1>
<p>sign in with the email you used for room. we'll send you a link.</p>
${error ? `<p class="err">${esc(error)}</p>` : ''}
<form method="post" action="/activate">
  <input type="hidden" name="code" value="${esc(code)}">
  <input type="email" name="email" placeholder="you@email.com" autocomplete="email" required autofocus>
  <button type="submit">send the link</button>
</form>`);
}

export function confirm(action, token, label) {
  return page(label, `
<h1>${esc(label)}</h1>
<p>one more press.</p>
<form method="post" action="${esc(action)}">
  <input type="hidden" name="t" value="${esc(token)}">
  <button type="submit">${esc(label)}</button>
</form>`);
}

export function accountLogin(error = '') {
  return page('your account', `
<h1>your room account</h1>
<p>see your license and the computers room is on.</p>
${error ? `<p class="err">${esc(error)}</p>` : ''}
<form method="post" action="/account/login">
  <input type="email" name="email" placeholder="you@email.com" autocomplete="email" required autofocus>
  <button type="submit">send me a sign-in link</button>
</form>`);
}

function licenseLabel(l, now) {
  if (l.revoked) return 'refunded';
  if (l.type === 'full') return 'room';
  const until = l.expires_at ? day(l.expires_at) : '';
  if (l.type === 'launch') return l.expires_at > now ? `launch · open until ${until}` : `launch · ended ${until}`;
  return l.expires_at > now ? `trial · until ${until}` : `trial · ended ${until}`;
}

export function account(email, licenses, now, cfg) {
  const hasFull = licenses.some((l) => l.type === 'full' && !l.revoked);
  const cards = licenses
    .filter((l) => !(hasFull && l.type !== 'full' && l.machines.length === 0))
    .map((l) => {
      const machines = l.machines.length
        ? l.machines.map((m) => `
  <div class="row">
    <div>${esc(m.name || 'a computer')}<div class="meta">last seen ${day(m.last_seen)}</div></div>
    <form method="post" action="/account/deactivate">
      <input type="hidden" name="machine" value="${esc(m.machine_id)}">
      <button class="quiet" type="submit">deactivate</button>
    </form>
  </div>`).join('')
        : '<p class="meta" style="margin:8px 0 0">not on any computer yet.</p>';
      return `
<div class="card">
  <div>${esc(licenseLabel(l, now))}</div>
  <div class="meta">key <code>${esc(l.key)}</code> · up to ${l.max_machines} computers</div>
  ${machines}
</div>`;
    })
    .join('');
  const buy = !hasFull && cfg.checkoutFull
    ? `<p><a href="${esc(cfg.checkoutFull + (cfg.checkoutFull.includes('?') ? '&' : '?') + 'checkout[email]=' + encodeURIComponent(email))}">get room</a></p>`
    : '';
  return page('your account', `
<h1>your account</h1>
<p>${esc(email)}</p>
${cards || '<p>no license yet. open room in your daw and press "start your session".</p>'}
${buy}
<p class="meta">a deactivated computer stops at its next check. it can sign in again later.${cfg.support ? ` questions: <a href="mailto:${esc(cfg.support)}">${esc(cfg.support)}</a>` : ''}</p>
<form method="post" action="/account/logout"><button class="quiet" type="submit">sign out</button></form>`);
}
