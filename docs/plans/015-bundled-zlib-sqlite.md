# Plan 015: Bundled zlib And SQLite

Status: Implemented for bundled static zlib, bundled static SQLite, and static C++ runtime linking for the `rimau-server` deployment binary where supported.

## Goal

Remove runtime dependency on system `libz` and `libsqlite3` while preserving gzip compression and SQLite runtime configuration.

## Scope

- Implemented: CMake target `rimau_bundled_zlib`.
- Implemented: Pin zlib `1.3.2` source archive URL and SHA256.
- Implemented: Link Rimau against bundled `libz.a`.
- Implemented: CMake target `rimau_bundled_sqlite`.
- Implemented: Pin SQLite `3.53.3` amalgamation URL and SHA256.
- Implemented: Compile SQLite amalgamation into bundled `libsqlite3.a`.
- Implemented: Disable SQLite loadable extensions with `SQLITE_OMIT_LOAD_EXTENSION=1`.
- Implemented: Reject system SQLite/zlib builds through CMake options.
- Implemented: Static-link `libstdc++` and `libgcc` into `rimau-server` on Linux GNU/Clang builds when supported.

## Current Validation

```bash
cmake -S . -B build -DRIMAU_ENABLE_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --check-config
ldd build/rimau-server
```

At the time this plan was completed, `ldd` output only showed `libc.so.6` and the Linux dynamic loader. No dynamic `libssl`, `libcrypto`, `libz`, `libsqlite3`, `libstdc++`, or `libgcc_s` links were reported. This is superseded by Plan 016 for the current Linux x86_64 deployment binary.

## Limits

- Full static `glibc` was out of scope for this plan; Plan 016 later implements it for the current Linux x86_64 deployment binary.
- First build needs network access unless dependency archives are already cached by CMake.
- There is no helper script yet to update OpenSSL, SQLite, zlib, Bison, Linux headers, and glibc pins together.
