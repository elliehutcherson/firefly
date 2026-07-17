-- Initial schema. Conventions:
--   * All cash amounts are integer cents (BIGINT). Never floats.
--   * Prices are NUMERIC(14,4); costs are rounded to cents at execution time.
--   * Timestamps are TIMESTAMPTZ.

CREATE TABLE users (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    username TEXT NOT NULL,
    -- argon2id via libsodium crypto_pwhash_str.
    password_hash TEXT NOT NULL,
    -- Optional; only used for password recovery. No email => no recovery.
    email TEXT,
    email_verified_at TIMESTAMPTZ,
    -- Everyone starts with $10,000.
    cash_cents BIGINT NOT NULL DEFAULT 1000000,
    show_on_leaderboard BOOLEAN NOT NULL DEFAULT TRUE,
    public_trades BOOLEAN NOT NULL DEFAULT FALSE,
    signup_ip INET,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Usernames are case-insensitively unique.
CREATE UNIQUE INDEX users_username_lower_idx ON users (lower(username));

CREATE TABLE sessions (
    -- SHA-256 of the opaque session token; the raw token only lives in the
    -- user's cookie.
    token_hash BYTEA PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at TIMESTAMPTZ NOT NULL,
    last_seen_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    ip INET
);

CREATE INDEX sessions_user_id_idx ON sessions (user_id);
CREATE INDEX sessions_expires_at_idx ON sessions (expires_at);

-- The curated symbol universe (~S&P 500 + Nasdaq-100 + popular ETFs). Users
-- can only look up / trade symbols present here.
CREATE TABLE instruments (
    symbol TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    exchange TEXT NOT NULL,
    is_active BOOLEAN NOT NULL DEFAULT TRUE,
    added_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Permanent store of daily bars; backfilled once, then appended nightly.
-- Year/YTD/month charts are served entirely from this table.
CREATE TABLE candles_daily (
    symbol TEXT NOT NULL REFERENCES instruments (symbol),
    day DATE NOT NULL,
    open NUMERIC(14, 4) NOT NULL,
    high NUMERIC(14, 4) NOT NULL,
    low NUMERIC(14, 4) NOT NULL,
    close NUMERIC(14, 4) NOT NULL,
    volume BIGINT NOT NULL,
    PRIMARY KEY (symbol, day)
);

-- Immutable execution ledger. v1: market orders only, instant fill.
CREATE TABLE orders (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users (id),
    symbol TEXT NOT NULL REFERENCES instruments (symbol),
    side TEXT NOT NULL CHECK (side IN ('buy', 'sell', 'short', 'cover')),
    quantity BIGINT NOT NULL CHECK (quantity > 0),
    price NUMERIC(14, 4) NOT NULL,
    -- Signed cash impact, rounded to cents: negative for buy/cover,
    -- positive for sell/short.
    cash_delta_cents BIGINT NOT NULL,
    executed_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX orders_user_id_executed_at_idx ON orders (user_id, executed_at);

-- Current holdings. quantity < 0 means a short position.
CREATE TABLE positions (
    user_id BIGINT NOT NULL REFERENCES users (id),
    symbol TEXT NOT NULL REFERENCES instruments (symbol),
    quantity BIGINT NOT NULL,
    avg_price NUMERIC(14, 4) NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (user_id, symbol)
);

-- Periodic portfolio valuations (nightly + a few intraday); the source for
-- leaderboard deltas.
CREATE TABLE equity_snapshots (
    user_id BIGINT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    taken_at TIMESTAMPTZ NOT NULL,
    equity_cents BIGINT NOT NULL,
    PRIMARY KEY (user_id, taken_at)
);

-- Precomputed movers lists, replaced wholesale by the movers job.
CREATE TABLE movers (
    kind TEXT NOT NULL CHECK (kind IN
        ('pct_gain', 'pct_loss', 'dollar_gain', 'dollar_loss', 'volatility')),
    rank SMALLINT NOT NULL,
    symbol TEXT NOT NULL REFERENCES instruments (symbol),
    value NUMERIC(18, 4) NOT NULL,
    as_of TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (kind, rank)
);

-- Precomputed rankings, replaced wholesale by the leaderboard job. Only
-- includes users with show_on_leaderboard = TRUE at computation time.
CREATE TABLE leaderboard (
    period TEXT NOT NULL CHECK (period IN ('day', 'week', 'month', 'year')),
    rank INTEGER NOT NULL,
    user_id BIGINT NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    delta_cents BIGINT NOT NULL,
    delta_pct NUMERIC(10, 4) NOT NULL,
    as_of TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (period, rank)
);
