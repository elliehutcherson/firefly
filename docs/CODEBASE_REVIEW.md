# Firefly Codebase Review

Date: 2026-07-15

## Implementation status (2026-07-16)

The review work is complete enough to begin Milestone 4 (trading). The findings
below are retained as the rationale and historical record. Implemented items:

- transaction API and rollback-on-destruction semantics;
- atomic signup using one transaction;
- generic database infrastructure under `backend/src/db/`;
- checked `RowReader` and selective domain storage interfaces;
- safe SQL error classification and public API status mapping;
- bounded market-data caches and bounded connection-pool acquisition;
- immutable active-instrument snapshots with periodic refresh;
- schema invariants in `0003_schema_invariants.sql`;
- unit/integration test organization mirroring production domains; and
- references for required borrowed dependencies, retaining pointers only when
  null has documented meaning.

Deliberate deferrals:

- Metrics counters have no consumer until deployment monitoring exists. Pool
  exhaustion and refresh failures are logged now; add exported metrics with
  Milestone 7 rather than introducing an unused metrics dependency.
- Generate future instrument rebalance migrations from a checked-in source
  snapshot when the first rebalance is needed. The current seed is valid and
  remains the deployment source of truth.
- Cash bounds depend on the trading and margin arithmetic. Define them during
  Milestone 4 rather than encoding a speculative database policy.

The handoff that accompanied this review is archived at
`docs/archive/HANDOFF_M3_TO_M4.md` (its "next work" — Milestone 4 trading —
has since landed).

## Executive summary

The codebase is in good shape for its current milestone. It is small, readable,
well-tested, consistent with `docs/STYLE.md`, and notably disciplined about
dependencies. The architecture's most important constraints are visible in the
implementation: Postgres is the durable market-data cache, upstream calls are
coalesced, money uses fixed-point integers, SQL is parameterized, and third-party
libraries are kept behind project-owned adapters.

The database layer should be reorganized, but not into an ORM or a single large
"database module." The best fit is a hybrid:

- move generic Postgres infrastructure from `common/` to `db/`;
- keep domain-facing storage contracts with their owning modules;
- put Postgres implementations (SQL and row mapping) under `db/postgres/`;
- add interfaces at boundaries that have multiple consumers, meaningful test
  seams, or transactional behavior—not mechanically for every table;
- add transaction support before implementing trading.

This preserves direct SQL and its performance characteristics while making
dependency direction and transaction ownership much clearer.

At the original review checkpoint all 143 tests passed. After implementing the
findings, 158 hermetic unit tests and 19 PostgreSQL integration tests pass.

## What is working well

### Architecture and dependency discipline

- The modular-monolith structure is easy to navigate. HTTP concerns are kept in
  `api/`; market-data provider concerns stay in `marketdata/`; scheduling stays
  in `jobs/`.
- Crow, CPR, libpqxx, and libsodium are wrapped or isolated as required by the
  style guide. Feature code does not include pqxx.
- The project has resisted unnecessary infrastructure: no ORM, Redis, service
  container, or heavyweight framework. That is appropriate for the stated
  cost and scale targets.
- Interfaces exist where substitution is real and valuable (`Db`, `HttpClient`,
  `Clock`, and `MarketDataProvider`).

### Performance-conscious choices

- `CandleRepo::UpsertCandles` writes a batch in one SQL statement rather than
  issuing one insert per candle.
- `DailyBarSync` asks for all latest candle dates in one query and skips
  upstream calls for current symbols.
- The market-data cache coalesces concurrent misses without holding its mutex
  during network I/O.
- Repository queries select explicit columns and use suitable primary/indexed
  access paths for the current workload.
- The fixed-size connection pool is a sensible match for synchronous Crow
  handlers and a small Postgres instance.

### Maintainability, readability, and tests

- Names and function shapes closely follow the local style guide.
- Error propagation is consistent through `absl::Status` and the sanctioned
  status macros.
- Tests cover behavior, error propagation, request coalescing, boundary
  validation, and real Postgres round trips. The suite is fast enough to remain
  useful during development.
- Comments generally explain architectural intent or non-obvious constraints,
  rather than restating code.

## Findings and recommendations

### P0 — Add an explicit transaction API before trading

`Db::Query` and `Db::Execute` each acquire a connection and commit a separate
transaction. This cannot implement the architecture's promised trading flow:
locking a user/position with `SELECT ... FOR UPDATE`, validating funds and
margin, inserting the immutable order, and updating cash and position atomically.
The original TODO in `db/db.h` correctly anticipated this; the transaction
foundation has since been implemented.

Recommended shape:

```cpp
class Transaction {
 public:
  virtual absl::StatusOr<Rows> Query(...)=0;
  virtual absl::StatusOr<int64_t> Execute(...)=0;
  virtual absl::Status Commit()=0;
};

class Database {
 public:
  virtual absl::StatusOr<std::unique_ptr<Transaction>> Begin()=0;
  // Single-statement conveniences may remain.
};
```

Rollback should be automatic on destruction unless committed. A higher-level
`TradingStore::ExecuteOrder(...)` may be even safer: it can own the complete
transaction and expose one domain operation, preventing callers from accidentally
splitting an invariant across transactions. Test at least one concurrent-order
case against real Postgres; a scripted fake cannot validate lock behavior.

### P0 — Make signup atomic or deliberately compensate

Signup currently inserts the user and then creates a session in a separate
transaction. If session creation fails, the API returns an error but the account
exists. Retrying then reports that the username is taken. This is a correctness
problem at a user-visible boundary.

Prefer a domain operation such as `AuthStore::CreateUserAndSession(...)` backed
by one transaction. If keeping two repositories, allow both operations to run
on the same transaction object. Deleting the user as compensation is inferior:
it adds another failure mode and is not atomic.

### P1 — Reorganize DB code, but preserve domain ownership

`common/` currently contains both genuinely cross-cutting types (`Clock`,
`Money`) and the entire Postgres mechanism. Meanwhile SQL implementations are
mixed into `auth/` and `marketdata/`. Neither is seriously harmful at the
current size, but trading, movers, and leaderboards will make the boundary less
clear.

Recommended target structure:

```text
backend/src/
├── auth/
│   ├── user_store.h          # domain records + narrow contract
│   └── session_store.h
├── marketdata/
│   ├── candle_store.h
│   └── instrument_store.h
├── trading/
│   └── trading_store.h       # transaction-sized domain operations
└── db/
    ├── database.h            # Database/Transaction abstractions
    └── postgres/
        ├── database.cc
        ├── connection_pool.{h,cc}
        ├── row_reader.{h,cc}
        ├── user_store.{h,cc}
        ├── session_store.{h,cc}
        ├── candle_store.{h,cc}
        └── instrument_store.{h,cc}
```

The implemented first step moves the database abstractions, connection pool,
and shared SQL types into `backend/src/db/`, while leaving repository implementations
beside their domains. The important rule is dependency direction: handlers and
jobs should depend on domain-shaped storage contracts, while Postgres code
depends on those contracts. A folder move by itself provides little value.

Avoid a generic `Repository<T>`, active-record model, query builder, or ORM.
Firefly's important queries are domain-specific, Postgres-aware, and often
transactional. Abstracting SQL syntax would add complexity and can obscure
locking, batching, indexes, and query plans without producing a real portability
benefit.

### P1 — Introduce repository interfaces selectively

Handlers and jobs currently accept concrete `UserRepo`, `SessionRepo`,
`CandleRepo`, and `InstrumentRepo` objects. Tests substitute `FakeDb`, which
means many tests assert SQL strings and encoded parameters rather than solely
the caller's behavior. This is useful for repository tests but unnecessarily
couples handler/job tests to persistence details.

Use interfaces where they clarify a domain boundary:

- `UserStore` and `SessionStore` for auth workflows;
- `CandleStore` and `InstrumentStore` for market-data handlers/jobs;
- one transaction-oriented `TradingStore` rather than interfaces for each
  trading table.

Then use domain fakes in handler/job tests and keep `FakeDb` only for Postgres
adapter unit tests. Retain real-Postgres integration tests for SQL semantics.

Do not add an interface merely because a class touches the database. The
connection pool and Postgres row reader are implementation details, and a
one-consumer leaf repository may remain concrete until a boundary earns the
abstraction.

### P1 — Centralize safe row decoding

Every repository receives `vector<optional<string>>` and manually performs
column-count, null, integer, date, and numeric checks. `UserRepo` and
`CandleRepo` already duplicate `GetColumn`; `SessionRepo` has a third ad hoc
version. This is repetitive and index-based mapping is easy to break when a
query's select list changes.

Add a small project-owned `RowReader`, not a mapping framework. It could expose
checked methods such as `RequiredString(index)`, `OptionalString(index)`,
`Int64(index)`, `CivilDay(index)`, and `Time(index)`, while attaching a query or
entity label to errors. This reduces boilerplate and standardizes malformed DB
data handling without adding a dependency or runtime-significant overhead.

Consider typed DB parameters at the same time. Today all values are formatted
as strings and casts are embedded in SQL. A small variant over null, string,
`int64_t`, time, and civil day would make repository calls clearer. This is a
readability/type-safety improvement, not a prerequisite for scale.

### P1 — Fix database error classification and API disclosure

The pqxx adapter maps every SQL error other than unique violation to
`InvalidArgument`, and includes `e.what()` plus SQL state in the returned
message. API framing returns status messages directly to clients. Consequently,
an internal constraint error or malformed application query can become HTTP 400
and may expose database details.

Recommended policy:

- map only expected, explicitly handled SQL states to public semantic codes
  (for example unique violation to `AlreadyExists`, serialization failure to
  `Aborted`, connection failures to `Unavailable`);
- treat unexpected SQL errors as `Internal`;
- log detailed pqxx diagnostics server-side with operation context;
- return stable, sanitized messages for internal/unavailable errors at the HTTP
  boundary.

This also avoids making future schema mistakes look like client mistakes.

### P1 — Bound cache memory

`CachedProvider` expires entries logically but does not evict successful expired
entries until the exact same key is requested again. Quote keys are bounded by
the curated universe, but minute-bar keys include start and end timestamps, so
normal traffic creates new keys every minute indefinitely. This is a slow memory
leak in the long-running process.

Add a maximum entry count and an opportunistic expired-entry sweep when inserting
or when the limit is reached, similar to the rate limiter. A full LRU dependency
is not justified. Metrics for entry count, hit/miss, coalesced waiters, and
upstream errors would make the scarce-resource strategy observable.

### P2 — Cache active-instrument membership in process

Every quote or candle request first performs an indexed `instruments` lookup.
This is cheap, but it is unnecessary for a universe that changes only through
migrations and is central to every market-data read. At higher traffic it adds
a DB round trip before cache/provider work.

Load active symbols into an immutable in-process set at startup and refresh it
periodically or after a controlled administrative update. Keep the repository as
the source of truth. This is a modest optimization; Cloudflare caching makes it
lower priority than transactions and cache bounds.

### P2 — Make pool pressure bounded and observable

`ConnectionPool::Acquire` waits indefinitely when all connections are leased.
That is
simple, but a slow query or leaked transaction could consume every Crow worker
without a useful timeout or diagnostic.

Add an acquisition deadline and return `DeadlineExceeded` or `Unavailable` when
exhausted. Record wait duration and pool utilization. Also ensure the configured
pool size is intentionally related to Crow's worker count; the current default
of four may be correct for a small VM but is implicit.

### P2 — Strengthen schema invariants before trading

The schema handles keys and major relationships well, but database constraints
should protect invariants even if application validation regresses. Candidates:

- `users.cash_cents` within the supported arithmetic range;
- non-negative candle prices and volume, plus `high >= low`;
- positive order prices;
- a deliberate policy for zero-quantity positions (delete them or disallow
  them);
- normalized instrument symbol shape if symbols can ever be administered
  outside reviewed migrations.

Do not add speculative indexes. Add them from concrete query shapes and verify
important reads/writes with `EXPLAIN (ANALYZE, BUFFERS)` on representative data.

### P2 — Separate generated/large seed data from schema evolution

The large instrument seed is valid as a migration, but future index rebalances
will make hand-review and provenance increasingly awkward. Keep migrations as
the deployment source of truth, while generating rebalance SQL from a checked-in
small script and source snapshot. This improves reproducibility without adding a
runtime dependency.

## Suggested implementation sequence

1. Define transaction semantics and implement/test the pqxx transaction wrapper.
2. Add an atomic auth signup operation and use the transaction API.
3. Create `backend/src/db/` and move generic DB/pool code without changing behavior.
4. Extract a small checked `RowReader` and migrate repositories incrementally.
5. Introduce domain storage interfaces as the auth/market modules are touched;
   split Postgres implementations into `db/postgres/` if that layout remains
   clearer after the first moves.
6. Design trading persistence around one transaction-sized domain operation.
7. Bound `CachedProvider` memory and add lightweight operational metrics.
8. Add pool timeouts and schema constraints before public deployment.

Each step can be landed independently. Avoid a flag-day repository rewrite.

## Verification performed

- Read `docs/ARCHITECTURE.md`, `docs/STYLE.md`, the root build files, schema
  migrations, application composition, handlers, jobs, providers, DB wrapper,
  connection pool, repositories, fakes, and representative tests.
- Searched production and test code for pqxx exposure and database access.
  pqxx is confined to `backend/src/db/db.cc`, `backend/src/db/connection_pool.cc`, and the
  internal pool header, as intended.
- Original review run: `./scripts/build_and_test.sh` passed 143/143 tests.
- Final implementation run: 158/158 hermetic unit tests passed.
- Final PostgreSQL integration run: 19/19 tests passed.
