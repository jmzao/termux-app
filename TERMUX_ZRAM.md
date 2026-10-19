# Termux-ZRAM fork

This repository is an **actual fork of `termux/termux-app`**, not a companion APK. It retains the upstream terminal emulator, service, sessions, settings and bootstrap installer and assigns the fork its own Android application id:

- app id: `com.termuxzram`
- app data / eventual Termux prefix: `/data/data/com.termuxzram/files/usr`
- display name: `Termux-ZRAM`

## What RAMZ does

`ramz-native/` contains an **opt-in compressed block store**. Programs using `libramz.so` explicitly store eligible blocks. The library uses LZ4 or Zstd, retains source data until compressed data has been verified, tracks metadata, verifies integrity on recovery and keeps raw data where compression is not a net win. The `ramz` command provides `status`, `stats`, `config` and `benchmark`.

It does **not** compress arbitrary Termux processes, the ART heap, stacks, pointers or Android kernel RAM. It is not kernel zram.

## Required bootstrap rebuild

A different Android app id means bootstrap executables must be rebuilt for `/data/data/com.termuxzram/files/usr`. The upstream `com.termux` bootstrap must never be installed in this fork. Before a distributable APK build, supply a verified matching `bootstrap-aarch64.zip` from a corresponding `termux-packages` fork.

For ARM64: build an aarch64 bootstrap for this prefix; provide its controlled URL and SHA-256 via `TERMUX_ZRAM_BOOTSTRAP_AARCH64_URL` and `TERMUX_ZRAM_BOOTSTRAP_AARCH64_SHA256`; add the RAMZ package recipe to that packages fork; then build an `arm64-v8a` APK.

## Validation

In a compatible Termux environment:

```sh
pkg install clang make liblz4 libzstd
cd ramz-native
make
make test
./build/ramz-benchmark --bytes 33554432 --algorithm lz4
./build/ramz-benchmark --bytes 33554432 --algorithm zstd
```

Record RSS, raw bytes, compressed bytes, metadata and timing. Random/incompressible workloads must remain raw and never be presented as RAM savings.

## Licensing

The upstream Termux application retains its upstream license. RAMZ is MIT.
