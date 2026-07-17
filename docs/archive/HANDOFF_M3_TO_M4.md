# Firefly handoff — Milestone 3 review closeout → Milestone 4 (trading)

> **DEPRECATED — historical artifact.** This handoff was written 2026-07-16,
> after the Milestone 3 architectural-review closeout, to hand work into
> Milestone 4 (trading). Milestone 4 landed 2026-07-17 (commits
> 94a8453..e9f0b12, one per phase), so the repository state and "next steps"
> below no longer describe reality. Kept for history; do not follow it.
> Current state: docs/ARCHITECTURE.md milestones + git log.

Updated: 2026-07-16

## Repository state

- Branch: `master`.
- Local commit `5c0fe5e` (`cache instruments with non-null dependencies`) and
  this handoff documentation are ahead of `origin/master`. They are
  intentionally not pushed; the user asked to push them themselves.
- The working tree should be clean after this handoff document is committed.
- Local Postgres has migrations through `0003_schema_invariants.sql` applied.
- Last verification: 158/158 hermetic unit tests and 19/19 PostgreSQL
  integration tests passed.

Before changing code, read `docs/ARCHITECTURE.md`, `docs/STYLE.md`, and
`docs/CODEBASE_REVIEW.md`.

## Review closeout

The architectural review is complete. Do not restart the database or test
reorganizations. Two findings are intentionally deferred:

- exported metrics until Milestone 7 supplies a monitoring consumer; and
- an instrument-source snapshot/SQL generator until the first universe
  rebalance.

One optional micro-cleanup remains: `main.cc` loads the instrument snapshot
synchronously, then `JobRunner::Start()` performs its documented immediate
tick and refreshes it a second time. This costs one startup query and is safe.
Only change it if the scheduling API can stay simple.

## Next milestone: trading

Milestones 1–3 in `docs/ARCHITECTURE.md` are substantially complete. Start
Milestone 4 by settling semantics in tests before adding routes.

Recommended order:

1. Specify order-side transitions (`buy`, `sell`, `short`, `cover`), whole-share
   quantities, average-price behavior, cent rounding, market-hours policy, and
   simplified margin rules. Avoid inventing cash bounds before this arithmetic
   is explicit.
2. Add a narrow trading domain API. Prefer one transaction-sized store method
   over separate order/position repositories exposed to handlers.
3. Implement the Postgres operation using the existing `Transaction` API:
   lock cash and the relevant position with `SELECT ... FOR UPDATE`, validate,
   insert the immutable order, update cash, and update or delete the position.
   Zero-quantity positions must be deleted because the schema rejects them.
4. Unit-test domain decisions with a fake trading store. Keep SQL-shape tests
   close to the Postgres implementation.
5. Add real-Postgres integration tests for rollback and concurrent orders;
   scripted fakes cannot prove locking behavior.
6. Add the HTTP route only after the transaction behavior is pinned.

## Conventions and constraints

- Required borrowed dependencies use references. Raw pointers require a real,
  documented nullable state; `MarketDeps::provider` is intentionally nullable.
- Keep one principal class per file unless supporting types are trivial.
- Repositories propagate `absl::Status`; SQLSTATE classification stays in
  `src/db/db.cc`, and public sanitization stays in `src/api/status_mapping.cc`.
- Database check violations are application invariant failures: log details
  server-side and return sanitized `Internal` responses.
- Do not add an ORM, Redis, a generic repository framework, speculative indexes,
  or a metrics dependency without a demonstrated need.
- Preserve direct SQL, transaction visibility, cache bounds, and the current
  dependency discipline.

## Useful commands

```sh
./scripts/build_and_test.sh
RUN_INTEGRATION=1 ./scripts/build_and_test.sh
git status -sb
```
