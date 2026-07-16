#include "src/auth/crypto.h"

#include <sodium.h>

#include <array>
#include <string>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace firefly {
namespace {

constexpr size_t kTokenBytes = 32;

// sodium_init() returns 0 on success, 1 if already initialized, -1 on error.
bool EnsureInit() {
  static const bool ok = sodium_init() >= 0;
  return ok;
}

std::string ToHex(const unsigned char* data, size_t size) {
  // sodium_bin2hex needs room for the trailing NUL.
  std::string hex(size * 2 + 1, '\0');
  sodium_bin2hex(hex.data(), hex.size(), data, size);
  hex.resize(size * 2);
  return hex;
}

}  // namespace

absl::Status InitCrypto() {
  if (!EnsureInit()) {
    return absl::InternalError("sodium_init failed");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> HashPassword(const std::string& password) {
  if (!EnsureInit()) {
    return absl::InternalError("sodium_init failed");
  }
  std::array<char, crypto_pwhash_STRBYTES> hash;
  if (crypto_pwhash_str(hash.data(), password.data(), password.size(),
                        crypto_pwhash_OPSLIMIT_INTERACTIVE,
                        crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
    // Only fails on insufficient memory.
    return absl::ResourceExhaustedError("password hashing failed");
  }
  return std::string(hash.data());
}

bool VerifyPassword(const std::string& stored_hash,
                    const std::string& password) {
  if (!EnsureInit() || stored_hash.size() >= crypto_pwhash_STRBYTES) {
    return false;
  }
  // crypto_pwhash_str_verify wants a NUL-terminated buffer; std::string's
  // c_str() provides one.
  return crypto_pwhash_str_verify(stored_hash.c_str(), password.data(),
                                  password.size()) == 0;
}

std::string GenerateSessionToken() {
  // A failed init here would be a programming error caught at startup by
  // InitCrypto; never silently emit a weak token.
  CHECK(EnsureInit()) << "libsodium unavailable";
  std::array<unsigned char, kTokenBytes> bytes;
  randombytes_buf(bytes.data(), bytes.size());
  return ToHex(bytes.data(), bytes.size());
}

std::string Sha256Hex(const std::string& data) {
  CHECK(EnsureInit()) << "libsodium unavailable";
  std::array<unsigned char, crypto_hash_sha256_BYTES> digest;
  crypto_hash_sha256(digest.data(),
                     reinterpret_cast<const unsigned char*>(data.data()),
                     data.size());
  return ToHex(digest.data(), digest.size());
}

}  // namespace firefly
