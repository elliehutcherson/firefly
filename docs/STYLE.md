# Firefly C++ Style Guide

We follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
This document distills the parts that matter most day-to-day, plus
project-specific conventions; where it is silent, Google style wins.
Formatting is enforced by `.clang-format` (`BasedOnStyle: Google`) and
linting by `.clang-tidy` — run both before sending code for review.

## Integer types

- Use plain `int` for ordinary small values (counters, ports, sizes that
  trivially fit).
- When the width matters — persisted values, protocol fields, anything that
  could plausibly exceed 32 bits — use exact-width types from `<cstdint>`
  (`int64_t`, `uint16_t`, ...).
- Never use `short`, `long`, or `long long` (clang-tidy's
  `google-runtime-int` enforces this).
- Don't use unsigned types just to say "this can't be negative"; reserve them
  for bit patterns and modular arithmetic.
- All cash amounts are integer cents in `int64_t`; prices are e4 fixed point
  (`int64_t`, 1/10000 dollar — matches the DB's `NUMERIC(14,4)`), converted
  from JSON via `src/common/money.h`. Never represent money as a floating
  point value.

## Declaration order

Per Google's
[declaration order](https://google.github.io/styleguide/cppguide.html#Declaration_Order):
`public:` before `protected:` before `private:`, and within each section:

1. Types and type aliases
2. Static constants
3. Factory functions and other static functions
4. Constructors and assignment operators
5. Destructor
6. All other functions
7. Data members — **always last**

Functions, including static factories like `Config::FromEnv()`, always appear
above data members.

## Scoping

- All code lives in namespace `firefly`. File-local helpers go in an unnamed
  namespace inside the `.cc` — never in headers.
- The unnamed namespace sits at the top of the file: one block, immediately
  after the opening of `namespace firefly`, before any exported definitions.
- No `using namespace` directives, ever. No namespace aliases in headers.
- Declare variables in the narrowest scope possible, as close to first use as
  possible, and initialize them at declaration.
- No global or static objects with non-trivial constructors or destructors
  (static initialization-order fiasco). `constexpr` globals are fine.

## Functions

- Keep functions short and single-purpose. Around 40 lines, start looking for
  a function trying to get out.
- Prefer named functions to multi-line lambdas. A lambda should fit on one
  line (capture, forward, done — e.g. route registration); the moment it
  grows a body, extract a named function. Named functions get column-zero
  braces, show up in stack traces, and are individually testable.
- **Prefer early returns.** Validate inputs and handle failure cases first,
  returning immediately; the happy path reads straight down at minimal
  indentation.
- Prefer less nesting. More than two or three levels of indentation means:
  invert the condition and return early, or extract a helper.
- Prefer return values to output parameters.

## Naming

Names should be descriptive; length proportional to scope.

| Entity | Style | Example |
|---|---|---|
| Files | `snake_case.cc` / `.h` | `market_data_cache.cc` |
| Types (classes, structs, enums, aliases) | `PascalCase` | `MarketDataProvider` |
| Functions | `PascalCase` | `FetchDailyBars()` |
| Variables, parameters | `snake_case` | `bind_address` |
| Private data members | trailing underscore | `config_` |
| Constants (`constexpr`/`static const`), enumerators | `kPascalCase` | `kMaxPort` |
| Namespaces | `lowercase` | `firefly` |
| Macros | avoid; `SCREAMING_CASE` if unavoidable | |

## RAII and ownership

- Every resource — memory, DB connection, socket, file, mutex lock — is owned
  by an object whose destructor releases it. Cleanup must not depend on
  reaching the end of a function (early returns make that a bug).
- `std::unique_ptr` expresses ownership; a raw pointer or reference is always
  a non-owning borrow. No manual `new`/`delete`.
- Use a reference for a required borrowed dependency that must outlive its
  consumer. Use a raw pointer only when `nullptr` has a documented meaning or
  an external API requires one; do not encode required dependencies as
  nullable state.
- Locks are held via `std::lock_guard` / `std::scoped_lock`; never manual
  `lock()`/`unlock()` pairs.

## Minimize mutable state

- `const` by default: local variables, methods, and parameters that don't
  mutate. Use `constexpr` where the value is compile-time.
- Prefer pure functions — inputs in, return value out — over methods that
  mutate members.
- Establish an object's invariants at construction and keep it immutable
  afterward where possible. Fewer fields, fewer states an object can be in,
  fewer bugs.
- No global mutable state. Shared mutable state needs exactly one owner and
  a mutex.

## Errors, not exceptions

Google style: we don't throw exceptions in our own code. Failure is signaled
with `absl::Status` / `absl::StatusOr<T>`. Assertions (`CHECK`-style) are for
programming errors only, never for conditions that can occur at runtime.

- Propagate errors with `RETURN_IF_ERROR` and `ASSIGN_OR_RETURN` from
  `src/common/status_macros.h` — these two are the sanctioned exception to
  the no-macros rule. Don't hand-write `if (!status.ok()) return status;`.
- In tests, assert with `EXPECT_OK`/`ASSERT_OK`
  (`tests/support/status_matchers.h`)
  and match error codes with `absl_testing::StatusIs` — never a bare
  `EXPECT_FALSE(result.ok())`, which passes for the *wrong* error.

## Test organization

- `tests/unit/` mirrors `src/`; keep one test file per production class or
  cohesive production source file.
- `tests/integration/` contains tests that cross a real infrastructure or
  vendor boundary. `tests/e2e/` is reserved for tests that exercise the full
  application through its public interface.
- Shared fakes live under `tests/fakes/<domain>/`. A fake used by only one test
  stays local to that test file.
- `tests/fuzz/` and `tests/performance/` are created only for actual fuzz
  targets and benchmarks. Timing assertions are not performance tests.
- Unit tests are hermetic and run by default. Integration and e2e tests must
  be labeled accordingly in CMake.

## Logging

- All diagnostics go through abseil logging (`absl/log/log.h`):
  `LOG(INFO)`, `LOG(WARNING)`, `LOG(ERROR)`, `LOG(FATAL)`. Never
  `std::cout`/`std::cerr`/`printf` for diagnostics.
- `absl::InitializeLog()` is called exactly once, at the top of `main()`.
- `LOG(FATAL)` and `CHECK` terminate the process — programming errors only,
  never conditions that can occur at runtime.
- Third-party libraries that log are routed into absl via small adapters,
  per the wrapper policy below (e.g. Crow's logger is bridged in `api/`).

## Wrap third-party libraries

Feature code never calls a third-party library directly; each dependency is
used through a thin, project-owned wrapper. The wrapper:

- translates the library's error signaling into `absl::Status` /
  `absl::StatusOr<T>` — exceptions never escape the wrapper (libpqxx throws;
  `catch` lives in the wrapper and nowhere else);
- exposes our vocabulary and types in its interface, not the library's;
- is the single place to mock in tests, and the single place to change if we
  swap the dependency (see `MarketDataProvider` in
  [ARCHITECTURE.md](ARCHITECTURE.md)).

Concretely: the DB wrapper in `common/` is the only code that includes pqxx
headers; `common/http` is the only code that includes cpr headers, and the
Alpaca adapter talks to the network through that `HttpClient` interface (which
is also how tests fake it). Crow stays confined to `api/`. Header-only utility
libraries used as vocabulary types (abseil, nlohmann/json) don't need
wrapping.

## Formatting and braces

`clang-format` (Google base config, `.clang-format`) is the arbiter of all
formatting — never hand-format against it. The rules it implements that come
up most:

- The opening brace is always attached to the end of the signature line
  (`void Function() {`), including when the parameter list wraps. Google
  style never puts `{` on its own line.
- The closing brace of a function sits at the indentation of its declaration:
  column zero for namespace-scope functions (namespace contents are not
  indented), one level in for members defined inside a class body.
- An indented `});` is the tail of a multi-line lambda, not a function —
  which is one more reason to extract named functions (see Functions above).

Note the division of labor: `.clang-format` owns layout (braces, wrapping,
include order); `.clang-tidy` owns semantic lint (naming, `long`, magic
numbers). Brace placement is never a clang-tidy concern.

## Files and includes

- Files are `.cc` / `.h`, one class or tightly-related group per pair.
- Header guards: `FIREFLY_<PATH>_<FILE>_H_` (e.g. `FIREFLY_API_SERVER_H_`).
- Production includes are rooted at `src/`: `#include "api/server.h"`.
  Test-support includes are rooted at `tests/`: `#include "fakes/db/fake_db.h"`.
- Include order (blank line between groups): related header first, then C
  system, C++ standard library, third-party, then project headers.
- Third-party **umbrella headers** — ones that do nothing but re-`#include` a
  library's real headers, e.g. `cpr/cpr.h` and `crow.h` — need a trailing
  `// IWYU pragma: keep`:

  ```cpp
  #include "cpr/cpr.h"  // IWYU pragma: keep
  ```

  clangd's include-cleaner reports *"header is not used directly"* for them,
  because the symbols we actually name (`cpr::Get`, `crow::SimpleApp`, ...) are
  defined in the sub-headers the umbrella pulls in, not in the umbrella itself.
  The canonical fix — an `// IWYU pragma: export` inside the umbrella — isn't
  ours to make: these headers live in pinned submodules under `third_party/` that
  we don't edit. The `keep` pragma on our include line suppresses the false
  positive without touching vendored code. Our own headers are never umbrellas,
  so they never need it — and leaving the diagnostic on means it still catches
  genuinely unused includes we write.

## Project conventions

- Configuration comes from environment variables via `Config::FromEnv()`;
  don't read `getenv` elsewhere.
- JSON in and out of the API uses nlohmann/json.
- Dependencies are pinned git submodules under `third_party/`; only stable-ABI C
  libraries (libpq, libcurl, libsodium) come from the system. See
  [ARCHITECTURE.md](ARCHITECTURE.md) for the policy.
