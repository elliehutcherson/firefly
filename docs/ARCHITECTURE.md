# Firefly — Architecture

A paper-trading web app: users get $10,000 virtual dollars to buy, sell, and
short US equities at (near) real prices, browse charts and daily movers, and
compete on opt-in leaderboards.

Design priorities, in order: **maintainable, secure, cheap to run, performant,
scalable-enough**. This is a hobby project targeting $0–15/month of
infrastructure.

## Key decisions (and why)

These four decisions shape everything else:

1. **The scarce resource is upstream API quota, not our traffic.** Free
   market-data tiers are tiny. One popular ticker viewed by 1,000 users must
   cost us *one* upstream call. Our own database is the primary data store;
   the provider is a trickle feed. Request coalescing + caching + Cloudflare
   edge caching enforce this at every layer.

2. **No distributed database.** A single Postgres instance on one small VM
   handles tens of thousands of users for this workload (a few writes per
   trade, read-mostly everything else). The schema is kept portable so we
   *could* move to Cloud SQL later, but we don't start there.

3. **Curated symbol universe, not "all tickers."** Roughly S&P 500 +
   Nasdaq-100 + popular ETFs (~600–800 symbols, `instruments` table). This
   bounds API usage and cache size, and it is *how we compute movers
   ourselves* — free APIs don't offer a good movers endpoint, but we can
   derive gainers/losers/volatility from a universe we already store.

4. **Historical data is immutable — fetch once, keep forever.** After a
   one-time backfill, we append one daily bar per symbol per day. Year / YTD /
   month charts are served 100% from our own Postgres with zero upstream
   calls. Only the "today" view touches fresher data.

## Market data: Alpaca (free Basic plan)

A free Alpaca paper-trading account (no funded brokerage) provides everything
from one provider:

| Need | Alpaca feature | Notes |
|---|---|---|
| Live-ish quote for trade execution | Latest trade/quote, **IEX feed** | True real-time, but IEX is ~2.5% of US volume (slightly thin prices) |
| Daily bars (year/YTD/month charts) | Historical bars, **SIP feed** | Consolidated tape, 7+ years back, free when data is >15 min old |
| Intraday "day" chart | Minute bars, SIP feed | Fetched on demand, 15-min delayed |
| Rate budget | ~200 REST calls/min (free tier) | Generous; coalescing keeps real usage far below |

**Anti-cheat pricing rule:** leaderboards create an incentive to exploit
delayed prices (watch real-time price elsewhere, "front-run" the simulator
with guaranteed profit). Therefore trades **execute at the IEX real-time
price**, while **charts display SIP delayed/historical data**. Both are free.

The provider sits behind a small interface (`MarketDataProvider`) so a dead
free tier means writing one new adapter class, not a rewrite.

**Licensing caveat:** showing provider data to our users is technically
redistribution. Delayed data is treated far more permissively than real-time —
another reason displayed charts stay on the delayed feed. Re-check Alpaca's
data terms before public launch.

### Caching layers

1. **Cloudflare edge** — market-data GET endpoints are public and served with
   `Cache-Control: public, max-age=60`; the edge absorbs most read traffic.
2. **In-process TTL cache** (C++ maps + mutex) — quotes cached 30–60 s,
   minute bars 1–2 min. No Redis: one process on one box, a network hop buys
   nothing.
3. **Request coalescing** — concurrent cache misses for the same key share a
   single upstream call (waiters block on the same future).
4. **Postgres** — daily bars, permanently.

## Backend: C++20, Crow + focused libraries

No heavyweight framework. The API is pure JSON; the frontend is a separate
static SPA, so no server-side templating is needed.

| Concern | Library | How it's vendored |
|---|---|---|
| HTTP server / routing / middleware | Crow | git submodule (header-only) |
| Async I/O (Crow dependency) | standalone asio | git submodule (header-only) |
| JSON | nlohmann/json | git submodule |
| Utilities, logging, flags | abseil-cpp | git submodule |
| Postgres client | libpqxx | git submodule (needs system `libpq-dev`) |
| Outbound HTTP (Alpaca, Turnstile) | cpr | git submodule (needs system `libcurl4-openssl-dev`) |
| Password hashing (argon2id), random tokens | libsodium | system `libsodium-dev` |
| Tests | googletest | git submodule |

Dependency policy: **git submodules under `backend/third_party/`, built with
`add_subdirectory` or a small interface target in `backend/cmake/`** (see zebes).
Only stable-ABI C libraries come from the system (libpq, libcurl, libsodium);
the Dockerfile pins them for production.

Threading model: Crow runs synchronous handlers on a thread pool. A blocking
libpqxx call occupies a worker thread — a non-issue at hobby scale with a
connection pool sized to the thread count, and Caddy in front buffers slow
clients.

The binary is a **modular monolith**:

```
backend/src/
├── api/         HTTP layer: routes, middleware (auth, rate limit), serialization
├── auth/        signup/login, argon2id, sessions, Turnstile verification
├── marketdata/  provider interface, Alpaca adapter, caches, coalescing
├── trading/     orders, positions, cash, margin rules
├── jobs/        in-process background timers (bar append, movers, snapshots)
└── common/      config, db pool, errors, money type
```

Modules expose narrow interfaces; `api/` is the only module that knows about
HTTP, and `db/` is the only module that touches connection handling.

## System diagram

```
 Phone / desktop browser
        │
        ▼
 Cloudflare (free): TLS, edge cache, Turnstile, IP rate rules, DDoS
        │
        ├── static SPA ── Cloudflare Pages (Svelte build, free)
        │
        ▼
 GCP e2-micro VM (free tier) ── Docker Compose
 ┌────────────────────────────────────────────────┐
 │ Caddy (reverse proxy, TLS to origin)           │
 │   ▼                                            │
 │ firefly (single C++ binary)                    │
 │  ├─ REST API                                   │
 │  ├─ market data cache + coalescing             │
 │  └─ background jobs (in-process timers)        │
 │ PostgreSQL 16 ─── nightly pg_dump → GCS        │
 └────────────────────────────────────────────────┘
        │
        ▼ (outbound only)
 Alpaca Market Data API (free Basic plan)
```

## Auth & abuse control

- **Username + password only.** Password hashed with argon2id
  (`crypto_pwhash_str`, libsodium). Email is optional, collected solely for
  password recovery, with an explicit "no email ⇒ no recovery" warning.
- **Server-side sessions.** Opaque 256-bit random token in an
  `HttpOnly; Secure; SameSite=Lax` cookie; the DB stores only the token's
  hash. No JWTs — on a single server they only add revocation problems.
- **Anti-abuse, outer to inner:**
  - Cloudflare rate-limit rules + bot fight mode (free).
  - Cloudflare Turnstile on signup/login (invisible, near-zero friction),
    verified server-side.
  - Per-IP signup cap (e.g. 3/day) enforced in-app.
  - Token-bucket rate limits per session and per IP in Crow middleware.
- All writes require a session. Market-data reads are public (they're
  edge-cached; abuse hits the cache, not Alpaca).

## Trading rules (v1)

- All money is **integer cents (`BIGINT`)** — never floats. Prices are
  `NUMERIC` in the DB; costs are rounded to cents at execution.
- Market orders only, whole shares, instant execution at the current cached
  IEX price. No limit orders, options, or leverage in v1 — each is a large
  complexity multiplier.
- Shorting: proceeds credited to cash; simplified maintenance margin (equity
  ≥ 25% of short exposure); the snapshot job force-covers violators.
- Every order is a row in an immutable `orders` ledger; `positions` and
  `users.cash_cents` are updated in the same transaction with
  `SELECT ... FOR UPDATE` so concurrent orders cannot corrupt cash.
- Trading allowed only during regular market hours (v1).

## Movers & leaderboards

Both are **precomputed by background jobs, never at request time**:

- **Movers** — from the universe's last two daily bars (+ fresh quotes for
  actively traded symbols): top N by % change, $ change, and volatility
  (high–low range). Refreshed every few minutes into the `movers` table.
- **Leaderboards** — periodic equity snapshots per user (nightly + a few
  intraday) → deltas over day/week/month/year into `leaderboard`. Serving is
  a trivial indexed read.
- Privacy: `users.show_on_leaderboard` and `users.public_trades` flags,
  checked at query time.

## Database schema (summary)

See `backend/migrations/` for the source of truth.

| Table | Purpose |
|---|---|
| `users` | account, argon2id hash, optional email, `cash_cents`, privacy flags |
| `sessions` | hashed token, user, expiry, last-seen |
| `instruments` | the curated symbol universe |
| `candles_daily` | permanent daily OHLCV per symbol |
| `orders` | immutable execution ledger (side, qty, price, cost) |
| `positions` | current qty (negative = short) + avg cost per user/symbol |
| `equity_snapshots` | periodic portfolio value per user |
| `movers` | precomputed movers lists |
| `leaderboard` | precomputed rankings per period |

Migrations are plain SQL files in `backend/migrations/`, applied in order by
`scripts/migrate.sh` (records applied files in `schema_migrations`, one
transaction per file).

## Frontend

- **Svelte + TypeScript + Vite**, built as a static SPA, deployed to
  Cloudflare Pages (free). Talks to the API over JSON; no SSR.
- **TradingView lightweight-charts** for all stock charts (candles, areas,
  range switching, touch support, ~45 KB).
- Responsive layout, phone-first; no component framework unless it earns it.

## Deployment & cost

- One **GCP e2-micro** VM (always-free tier; e2-small ~$13/mo if cramped),
  running Docker Compose: Caddy + firefly + Postgres.
- **Cloudflare free** in front: DNS, TLS, CDN, Turnstile, rate rules.
- Frontend on **Cloudflare Pages** (free).
- Backups: nightly `pg_dump` to GCS (pennies).
- CI: GitHub Actions — build + tests + Docker image; deploy is
  `docker compose pull && up -d` over SSH.
- **Total: $0–15/month** + domain. The first paid upgrade this project will
  ever want is a market-data plan, not infrastructure.

## Security checklist

- Parameterized queries only (libpqxx); no string-built SQL.
- argon2id password hashes; session tokens stored hashed; constant-time
  comparisons (libsodium).
- TLS terminated at Cloudflare and Caddy; Crow listens on localhost only.
- Cookies: `HttpOnly; Secure; SameSite=Lax`. CORS locked to the app origin.
- All input validated at the `api/` boundary; symbols validated against
  `instruments`.
- Container runs as non-root; no secrets in the repo (env vars via
  `.env`, gitignored).
- Prices/cash: integer cents; overflow-checked arithmetic in the money type.

## Milestones

1. **Walking skeleton (this repo now):** CMake + submodules, Crow server with
   `/healthz`, docker-compose Postgres, migrations runner, CI.
2. **Market data:** Alpaca adapter, universe seed, daily-bar backfill +
   nightly append job, quote cache + coalescing, chart endpoints.
3. **Auth:** signup/login/logout, sessions, Turnstile, rate limiting.
4. **Trading:** orders, positions, cash, shorts + margin check job.
5. **Movers + leaderboards:** jobs + endpoints + privacy flags.
6. **Frontend:** Svelte SPA — search, charts, trade ticket, portfolio,
   movers, leaderboards.
7. **Deploy:** VM, Caddy, Cloudflare, backups, monitoring (uptime ping +
   log rotation).
