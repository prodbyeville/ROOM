# room license server

A small Cloudflare Worker with a D1 database at `license.prodbyeville.com`. It
gives out room licenses, signs the tokens the plugin checks, and turns a
Shopify purchase into a full license on the buyer's account. Emails go out
through Resend. The Cloudflare and Resend free tiers both cover this.

How it behaves:

- **Launch window** (before `LAUNCH_ENDS_AT`): an email entered on the room page
  gets the installer links and a launch license. Every launch license stops at
  `LOCK_AT`, judged by the server's clock.
- **After the launch:** new people get a trial when they first open room
  (`TRIAL_DAYS`, default 14). The trial starts on first open, not at download.
- **Buying** (Shopify `orders/paid`) adds a full license to the same email's
  account. room unlocks at its next check-in, so nobody has to type a key. The
  key still goes out in the purchase email as a backup.
- **Refunds** (`orders/cancelled`) take the full license back.
- **Computers:** each license allows `MAX_MACHINES` computers (default 3).
  People free up old ones themselves at `/account`.
- **Offline:** paid licenses keep working for `GRACE_DAYS` (14) without a
  check-in. Launch and trial licenses check in every `SHORT_CHECK_DAYS` (3).

## One-time setup

You need Node 22 or newer. `npx wrangler` downloads Wrangler the first time.

### 1. Cloudflare

1. Create a free Cloudflare account and add `prodbyeville.com` to it. If your
   domain's DNS is somewhere else, the simplest path is to move the nameservers
   to Cloudflare, which the dashboard walks you through.
2. `cd server && npx wrangler login`
3. `npx wrangler d1 create room-licenses` and paste the `database_id` it prints
   into `wrangler.toml`.
4. `npx wrangler d1 execute room-licenses --remote --file=schema.sql`

### 2. Signing key (once, ever)

```
node scripts/make-keys.mjs
```

- Copy the `SIGNING_KEY` line, then run `npx wrangler secret put SIGNING_KEY`
  and paste it. Also keep a copy in your password manager.
- Paste the `kProductionKey` block over the placeholder in
  `plugin/Source/licensing/License.cpp`.

The two halves belong together. If you ever make a new key, every room out
there stops trusting its saved license and has to sign in again.

### 3. Resend (email)

1. Create an account at resend.com and add `prodbyeville.com` as a sending
   domain. It gives you DNS records to add in Cloudflare.
2. Create an API key, then `npx wrangler secret put RESEND_API_KEY`.
3. Set `FROM_EMAIL` and `SUPPORT_EMAIL` in `wrangler.toml`.

### 4. Shopify

1. Make two products: **room (launch price)** and **room**. Turn off shipping
   ("this is a digital product").
2. For each one, make a cart link: `https://<your-store>/cart/<variant-id>:1`.
   The variant id is in the product's URL in the admin once you open the
   variant. Put them in `CHECKOUT_LAUNCH_URL` and `CHECKOUT_FULL_URL`. The
   server adds the person's email to the link, so the purchase lands on the
   same account they use in room.
3. Put both product ids in `SHOPIFY_PRODUCT_IDS`, comma separated. If you leave
   it empty, any line item titled "room" counts.
4. Go to **Settings → Notifications → Webhooks** and create two webhooks (JSON,
   latest API version):
   - Order payment → `https://license.prodbyeville.com/shopify/orders-paid`
   - Order cancellation → `https://license.prodbyeville.com/shopify/orders-cancelled`
5. Shopify shows a signing secret under the webhook list. Run
   `npx wrangler secret put SHOPIFY_WEBHOOK_SECRET` and paste it.

### 5. Dates, links, deploy

Fill in `wrangler.toml` `[vars]`:

- `LAUNCH_ENDS_AT` and `LOCK_AT` as unix seconds (epochconverter.com).
- `DOWNLOAD_MAC_URL` and `DOWNLOAD_WIN_URL` for the installers.

Then run `npx wrangler deploy`. The `routes` line sets up
`license.prodbyeville.com` for you.

### 6. The room page form

```html
<form method="post" action="https://license.prodbyeville.com/signup">
  <input type="email" name="email" placeholder="you@email.com" required>
  <button type="submit">get room</button>
</form>
```

This posts like a normal form and shows a "check your email" page. To keep
people on your page, send JSON with `fetch` instead. Requests from
`SITE_ORIGIN` are allowed.

### 7. Turn it on in the plugin

In `plugin/CMakeLists.txt` set `ROOM_LICENSING` to `ON`. Make sure the real
`kProductionKey` is in. Then build the release. Until then, room ships with
licensing off and never locks.

## Testing

```
npm test                                          # server tests (no installs)
AUTO_APPROVE=e2e@example.com node test/serve.mjs  # local server with a test key
room_tests --license-e2e http://127.0.0.1:8787    # the real plugin code against it
```

The Mac CI build runs both on every push.

## Day to day

- **Look someone up:**
  `npx wrangler d1 execute room-licenses --remote --command "SELECT * FROM licenses l JOIN accounts a ON a.id = l.account_id WHERE a.email = 'them@example.com'"`
- **Give someone a license by hand:** make a draft order for room in Shopify
  with their email, mark it as paid, and the webhook does the rest.
- **Move the lock date:** change `LOCK_AT` and redeploy. It reaches every launch
  license at its next check-in.
- **Rate limits:** sign-in emails are capped at 6 an hour per address. For
  more, add a Cloudflare rate-limiting rule on `/activate` and `/signup`.
