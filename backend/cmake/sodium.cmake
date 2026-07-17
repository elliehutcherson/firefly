# libsodium comes from the system (apt: libsodium-dev); it is a stable-ABI C
# library that is painful to build in-tree. Exposed as PkgConfig::SODIUM.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
