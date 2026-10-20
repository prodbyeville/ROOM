-- ROOM license server (Cloudflare D1 / SQLite).
-- Apply with: npx wrangler d1 execute room-licenses --remote --file=schema.sql

CREATE TABLE IF NOT EXISTS accounts (
  id          TEXT PRIMARY KEY,
  email       TEXT NOT NULL UNIQUE,
  created_at  INTEGER NOT NULL
);

-- launch | trial | full. A full license outranks the others on the same account.
CREATE TABLE IF NOT EXISTS licenses (
  id            TEXT PRIMARY KEY,
  account_id    TEXT NOT NULL REFERENCES accounts(id),
  type          TEXT NOT NULL,
  key           TEXT NOT NULL UNIQUE,
  created_at    INTEGER NOT NULL,
  expires_at    INTEGER,              -- NULL = never (full)
  max_machines  INTEGER NOT NULL,
  order_id      TEXT UNIQUE,          -- Shopify order, for full licenses
  revoked       INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS licenses_account ON licenses(account_id);

CREATE TABLE IF NOT EXISTS machines (
  license_id    TEXT NOT NULL REFERENCES licenses(id),
  machine_id    TEXT NOT NULL,
  name          TEXT,
  activated_at  INTEGER NOT NULL,
  last_seen     INTEGER NOT NULL,
  PRIMARY KEY (license_id, machine_id)
);

-- Browser sign-in for the plugin (device code flow).
CREATE TABLE IF NOT EXISTS devices (
  code        TEXT PRIMARY KEY,
  machine_id  TEXT NOT NULL,
  name        TEXT,
  created_at  INTEGER NOT NULL,
  expires_at  INTEGER NOT NULL,
  account_id  TEXT
);

-- Magic sign-in links (stored hashed).
CREATE TABLE IF NOT EXISTS logins (
  token_hash  TEXT PRIMARY KEY,
  email       TEXT NOT NULL,
  device_code TEXT,
  created_at  INTEGER NOT NULL,
  expires_at  INTEGER NOT NULL,
  used        INTEGER NOT NULL DEFAULT 0
);

-- Account page sessions (cookie, stored hashed).
CREATE TABLE IF NOT EXISTS sessions (
  id_hash     TEXT PRIMARY KEY,
  account_id  TEXT NOT NULL,
  expires_at  INTEGER NOT NULL
);
