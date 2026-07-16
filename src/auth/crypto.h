#ifndef FIREFLY_AUTH_CRYPTO_H_
#define FIREFLY_AUTH_CRYPTO_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

// Thin boundary around libsodium; per the wrapper policy in docs/STYLE.md,
// crypto.cc is the only file that includes <sodium.h>.
//
// On the "constant-time comparisons" checklist item (docs/ARCHITECTURE.md):
// it is satisfied structurally rather than by sodium_memcmp call sites.
// Password checks go through crypto_pwhash_str_verify (internally
// constant-time), and session tokens are looked up BY THE SHA-256 OF THE
// SECRET — index-comparison timing can only leak bits of the hash, and
// recovering a 256-bit random token from its SHA-256 is infeasible. No code
// path compares a caller-supplied secret against a stored secret directly.

namespace firefly {

// Initializes libsodium; call once at startup and fail fast on error. The
// functions below also self-initialize so a missed call is a bug, not UB.
absl::Status InitCrypto();

// argon2id via crypto_pwhash_str at INTERACTIVE limits (~64 MiB, ~100 ms) —
// chosen over MODERATE because the e2-micro shares its 1 GB with Postgres.
// Returns the self-describing "$argon2id$..." string for users.password_hash.
absl::StatusOr<std::string> HashPassword(const std::string& password);

// Constant-time verify of `password` against a HashPassword() result.
bool VerifyPassword(const std::string& stored_hash,
                    const std::string& password);

// 32 bytes from randombytes_buf as 64 lowercase hex chars — the ff_session
// cookie value. The database only ever stores Sha256Hex(token).
std::string GenerateSessionToken();

// Lowercase hex SHA-256 (no \x prefix; SessionRepo adds the bytea framing).
std::string Sha256Hex(const std::string& data);

}  // namespace firefly

#endif  // FIREFLY_AUTH_CRYPTO_H_
