# RAMZ validation results

## Scope

These are reproducible **host** results for the explicit RAMZ block store. They validate the library implementation, not an Android ARM64 APK. Android packaging remains blocked on a matching `com.termuxzram` aarch64 bootstrap and an Android NDK build environment; see [`../TERMUX_ZRAM.md`](../TERMUX_ZRAM.md).

RAMZ is intentionally opt-in: an application hands it bytes, receives a handle, and asks RAMZ to recover them later. It does not reduce Android's system RAM, intercept arbitrary `malloc()` calls, move native pointers, compress another process, or implement kernel zram.

## Reproduce

```sh
cd ramz-native
make clean
make TEST_HOOKS=1 all
./build/ramz-tests
./build/ramz-benchmark --bytes 8388608 --algorithm lz4
./build/ramz-benchmark --bytes 8388608 --algorithm zstd

# The installable library deliberately omits the test-only corruption hook.
make clean && make
nm -D build/libramz.so | grep ramz_test_corrupt && exit 1 || true
```

The benchmark allocates and touches a source mapping, calls `ramz_put`, then releases that source mapping only after RAMZ has completed compression and (by default) post-compression decompression/checksum verification. It reads `/proc/self/status` for `VmRSS` and `VmSize` and records `getrusage()` CPU time. `stored_payload_bytes + metadata_estimate_bytes` is the portable accounting result; the estimate does not include allocator arena slack or `unordered_map` bucket slack, so RSS is reported separately.

## Recorded run (2026-10-18)

Host run with 8,388,608-byte blocks, integrity verification enabled, one stored block per case. Numbers naturally vary with allocator and OS state.

| Codec | Case | Recovered data | Stored payload + metadata | Compression CPU / wall | Decompression CPU / access wall | RSS change after source release |
|---|---|---:|---:|---:|---:|---:|
| LZ4 | repetitive | verified | 33,057 B | 21.408 / 21.411 ms | 9.740 / 9.739 ms | -7,948 KiB |
| LZ4 | structured | verified | 2,433,477 B | 22.815 / 23.106 ms | 9.744 / 9.764 ms | -5,840 KiB |
| LZ4 | random | verified, retained raw | 8,388,672 B | 10.472 / 10.471 ms | 9.530 / 9.530 ms | -2,380 KiB* |
| Zstd | repetitive | verified | 44,612 B | 22.701 / 24.678 ms | 10.442 / 10.441 ms | -7,516 KiB |
| Zstd | structured | verified | 2,264,535 B | 23.826 / 23.826 ms | 9.587 / 9.586 ms | -6,044 KiB |
| Zstd | random | verified, retained raw | 8,388,672 B | 11.232 / 11.232 ms | 9.503 / 9.515 ms | -2,236 KiB* |

\* The random payload was explicitly reported as `stored raw: insufficient savings`. Its RSS delta is allocator/mapping noise after a source mapping is unmapped, **not** a RAMZ saving. The accounted store size is 64 bytes larger than the original because of metadata, therefore this case must not be advertised as a memory reduction.

## Integrity and production-boundary checks

- LZ4 and Zstd round-trip tests passed.
- Raw fallback, invalid handle, output bounds, release, and deliberate payload-corruption detection passed.
- A production build completed with no exported `ramz_test_corrupt` symbol; that mutation entry point is compiled only when `TEST_HOOKS=1`.
- `ramz_put` retains the original caller data until compression succeeds and optional verification completes. If compression is not a net win, it stores a raw owned copy and returns `RAMZ_EINCOMPRESSIBLE` with a valid handle.

## Interpretation

The repetitive and structured cases demonstrate recoverable, measured process-RSS reductions for a test process whose source payload has been intentionally handed off and released. They do **not** imply the same result for arbitrary programs, VM heaps, stacks, pointer-rich graphs, or Android system RAM. The random case demonstrates the opposite policy: no false compression claim and no use of a larger compressed representation.
