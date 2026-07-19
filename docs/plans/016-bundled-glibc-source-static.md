# Plan 016: Bundled glibc Source Static Build

## Goal

Build the deployable `rimau-server` binary without depending on host `glibc` or the Linux dynamic loader by compiling glibc from original upstream source inside the Rimau build.

## Implemented

- Added `RIMAU_FULLY_STATIC_SERVER=ON` and `RIMAU_USE_BUNDLED_GLIBC=ON` CMake options.
- Added `rimau_bundled_bison` to build GNU Bison 3.8.2 from source for glibc.
- Added `rimau_bundled_linux_headers` to install Linux UAPI headers from Linux kernel source 6.18.7.
- Added `rimau_bundled_glibc` to build GNU glibc 2.43 into `build/_deps/glibc/sysroot`.
- Linked `rimau-server` with `-static`, `-B<glibc-sysroot>/usr/lib`, and `--sysroot=<glibc-sysroot>`.
- Configured glibc with `--without-selinux` so host `libselinux` is not pulled into the bundled libc build.
- Configured bundled OpenSSL with `no-dso`.

## Validation

Commands run on 2026-07-19:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
make test
make check
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --protocols
ldd build/rimau-server || true
file build/rimau-server
readelf -l build/rimau-server | rg 'INTERP|Requesting program interpreter' || true
make status
make status-https
```

Result:

- Build passed.
- CTest passed, 6/6 tests.
- `make test` passed, 6/6 tests.
- `make check` passed.
- Default and `data/rimau.sqlite3` config checks passed.
- Default and `data/rimau.sqlite3` protocol checks passed.
- `ldd build/rimau-server` reported `not a dynamic executable`.
- `file build/rimau-server` reported a statically linked Linux x86-64 ELF.
- `readelf` found no dynamic interpreter.
- `make status` and `make status-https` reported no background server running.

## Known Limits

- Bundled glibc build is currently wired only for Linux x86_64 GNU/Clang.
- First build needs network access unless CMake source downloads are cached.
- Build-time still needs host compiler/tooling such as `gcc`/`g++`, `make`, `perl`, `gawk`, `msgfmt`, `python3`, and `m4`; these are not runtime shared-library dependencies for `rimau-server`.
- glibc configure warns that `makeinfo` is missing on this machine, so some glibc documentation/test features are disabled. Needs verification if glibc upstream test coverage becomes a release requirement.
- Static glibc DNS/NSS hostname resolution can still need matching glibc shared NSS modules at runtime. Rimau reverse proxy hostname upstreams use `getaddrinfo`, and bundled OpenSSL has a `gethostbyname` path; production hostname behavior needs dedicated validation. Needs verification.
