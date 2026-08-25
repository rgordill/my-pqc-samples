# PQC Primitive Microbenchmark Suite

Microbenchmark for **post-quantum cryptographic primitives** (ML-KEM, ML-DSA, and SLH-DSA) across multiple libraries. This measures raw algorithm performance (keygen, encaps/decaps, sign/verify) — it does **not** benchmark TLS handshakes or protocol-level operations.

All crypto libraries are **built from source** in `third_party/` — no system `*-devel` packages are used for the benchmarked libraries themselves.

## Prerequisites

All crypto libraries are built from source by `scripts/build-deps.sh`. The benchmark binaries themselves only need a C++ toolchain; the packages below are required to compile OpenSSL, GnuTLS, BoringSSL, AWS-LC, and leancrypto.

### Build tools

| Package   | Notes |
| --------- | ----- |
| `gcc-c++` | C++17 compiler for benchmark binaries |
| `cmake`   | Build system (>= 3.16); used for BoringSSL, AWS-LC, and the benchmark |
| `make`    | Used by OpenSSL and GnuTLS builds |
| `git`     | Clone OpenSSL, BoringSSL, AWS-LC, and leancrypto from source |
| `curl`    | Download GnuTLS release tarball |
| `golang`  | Required by BoringSSL and AWS-LC build systems |
| `meson`   | Build system for leancrypto (`pip install --user meson ninja`) |
| `ninja`   | Parallel build backend for meson |
| `perf`    | Optional; used by `scripts/perf-profile.sh` |

### OpenSSL build (Perl modules)

OpenSSL 4.x runs Perl configure scripts that expect these modules on a minimal install:

| Package              | Notes |
| -------------------- | ----- |
| `perl-FindBin`       | OpenSSL `Configure` |
| `perl-IPC-Cmd`       | OpenSSL `Configure` |
| `perl-File-Compare`  | OpenSSL `Configure` |
| `perl-File-Path`     | OpenSSL `Configure` |
| `perl-File-Temp`     | OpenSSL `Configure` |
| `perl-Time-Piece`    | OpenSSL 4.x `Configure` |
| `perl-Text-Template` | OpenSSL `Configure` |

### GnuTLS build dependencies

| Package              | Notes |
| -------------------- | ----- |
| `nettle-devel`       | libnettle / libhogweed (checked by `pkg-config`) |
| `gmp-devel`          | GNU multiple precision arithmetic |
| `libtasn1-devel`     | ASN.1 parsing |
| `libunistring-devel` | Unicode string handling |

### leancrypto (PQC backend for GnuTLS)

leancrypto provides the ML-DSA and ML-KEM implementations used by GnuTLS. It is built with meson/ninja (no system packages required beyond a C compiler). Install meson with `pip install --user meson ninja` if not available.

Install everything on Fedora/RHEL:

```bash
sudo dnf install gcc-c++ cmake make git curl perf golang \
    perl-FindBin perl-IPC-Cmd perl-File-Compare perl-File-Path \
    perl-File-Temp perl-Time-Piece perl-Text-Template \
    nettle-devel gmp-devel libtasn1-devel libunistring-devel
pip install --user meson ninja
```

`scripts/build-deps.sh` checks for GnuTLS build dependencies (`nettle`, `hogweed`, `gmp`, `libtasn1` via `pkg-config`) and meson/ninja before starting.

## Quick Start

```bash
# 1. Build ALL third-party dependencies from source
bash scripts/build-deps.sh

# 2. Build the benchmark binaries
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 3. Run the full benchmark sweep
bash scripts/run-pqc-bench.sh
```

Results are saved to `results/<timestamp>/summary.csv`.

### Dynamic Linking Note

All libraries are built as shared libraries (`.so`). Each benchmark binary has RPATH/RUNPATH set to resolve its own `libcrypto.so` / `libgnutls.so` / `libleancrypto.so` correctly. **Do not** set a global `LD_LIBRARY_PATH` containing multiple library paths — several libraries share the same `libcrypto.so` filename and will cross-load incorrectly. The `bench_gnutls` binary uses `DT_RPATH` (via `--disable-new-dtags`) to ensure the transitive leancrypto dependency is resolved correctly.

## Architecture

Each benchmark binary runs a **lock-free per-thread design**:

1. Each thread gets its own pre-allocated state (keys, buffers)
2. After a warmup phase, threads run the target operation in a tight loop
3. Clock checks happen every 256 iterations to minimize overhead
4. No shared mutable state between threads — no locks, no atomics in the hot path

This design measures pure algorithm throughput without synchronization artifacts.

## Supported Algorithms and Operations



### ML-KEM (Key Encapsulation Mechanism)


| Algorithm   | keygen | encaps | decaps |
| ----------- | ------ | ------ | ------ |
| ML-KEM-512  | yes    | yes    | yes    |
| ML-KEM-768  | yes    | yes    | yes    |
| ML-KEM-1024 | yes    | yes    | yes    |




### ML-DSA (Module-Lattice Digital Signature)


| Algorithm | keygen | sign | verify |
| --------- | ------ | ---- | ------ |
| ML-DSA-44 | yes    | yes  | yes    |
| ML-DSA-65 | yes    | yes  | yes    |
| ML-DSA-87 | yes    | yes  | yes    |




### SLH-DSA (Stateless Hash-Based Digital Signature)


| Algorithm          | keygen | sign | verify | Notes                            |
| ------------------ | ------ | ---- | ------ | -------------------------------- |
| SLH-DSA-SHA2-128s  | yes    | yes  | yes    | Small signatures, very slow sign |
| SLH-DSA-SHA2-128f  | yes    | yes  | yes    | Fast variant                     |
| SLH-DSA-SHA2-192s  | yes    | yes  | yes    | Small signatures, very slow sign |
| SLH-DSA-SHA2-192f  | yes    | yes  | yes    | Fast variant                     |
| SLH-DSA-SHA2-256s  | yes    | yes  | yes    | Small signatures, very slow sign |
| SLH-DSA-SHA2-256f  | yes    | yes  | yes    | Fast variant                     |
| SLH-DSA-SHAKE-128s | yes    | yes  | yes    | SHAKE-based, small signatures    |
| SLH-DSA-SHAKE-128f | yes    | yes  | yes    | SHAKE-based, fast variant        |
| SLH-DSA-SHAKE-192s | yes    | yes  | yes    | SHAKE-based, small signatures    |
| SLH-DSA-SHAKE-192f | yes    | yes  | yes    | SHAKE-based, fast variant        |
| SLH-DSA-SHAKE-256s | yes    | yes  | yes    | SHAKE-based, small signatures    |
| SLH-DSA-SHAKE-256f | yes    | yes  | yes    | SHAKE-based, fast variant        |


> **SLH-DSA 's' vs 'f' variants**: 's' (small) variants produce smaller signatures but are orders of magnitude slower to sign. 'f' (fast) variants are ~20x faster for signing but produce larger signatures.



## Library Support Matrix


| Library     | Version      | PQC Backend   | ML-KEM-512 | ML-KEM-768  | ML-KEM-1024 | ML-DSA-44       | ML-DSA-65       | ML-DSA-87       | SLH-DSA               | In Sweep |
| ----------- | ------------ | ------------- | ---------- | ----------- | ----------- | --------------- | --------------- | --------------- | --------------------- | -------- |
| OpenSSL 4.x | 4.0.1        | built-in      | all ops    | all ops     | all ops     | all ops         | all ops         | all ops         | all 12 variants       | Yes      |
| OpenSSL 3.x | 3.6.3        | built-in      | all ops    | all ops     | all ops     | all ops         | all ops         | all ops         | all 12 variants       | Yes      |
| AWS-LC      | 5.5.0        | built-in      | all ops    | all ops     | all ops     | all ops         | all ops         | all ops         | —                     | Yes      |
| BoringSSL   | 0.20260803.0 | built-in      | —          | all ops     | —           | —               | all ops         | —               | SHA2-128s, SHAKE-256f | Yes      |
| GnuTLS      | 3.8.13       | leancrypto 1.8| —          | keygen only | —           | keygen/sign/ver | keygen/sign/ver | keygen/sign/ver | —                     | Yes      |




### Library Limitations

- **BoringSSL**: Only supports ML-KEM-768, ML-DSA-65, SLH-DSA-SHA2-128s, and SLH-DSA-SHAKE-256f (limited parameter sets). Returns exit code 2 for unsupported combinations.
- **AWS-LC**: Full ML-KEM and ML-DSA support. **No SLH-DSA support** in this version.
- **ML-KEM-512**: Not supported by BoringSSL.



### GnuTLS PQC Support (leancrypto)

GnuTLS 3.8.13 requires the `--with-leancrypto` configure flag to enable PQC algorithms. Without it, keygen calls are no-ops (completing in nanoseconds instead of microseconds) and sign/verify return empty results. The `build-deps.sh` script builds leancrypto 1.8.0 from source and configures GnuTLS with PQC enabled.

**Supported operations** (verified via dry-run):

| Algorithm   | keygen | sign | verify | encaps/decaps |
| ----------- | ------ | ---- | ------ | ------------- |
| ML-KEM-768  | yes    | —    | —      | Not in public API (TLS-only) |
| ML-KEM-1024 | no-op  | —    | —      | Not in public API (TLS-only) |
| ML-DSA-44   | yes    | yes  | yes    | —             |
| ML-DSA-65   | yes    | yes  | yes    | —             |
| ML-DSA-87   | yes    | yes  | yes    | —             |

**Key findings:**
- ML-DSA sign/verify requires `gnutls_privkey_sign_data2()` / `gnutls_pubkey_verify_data2()` with `GNUTLS_SIGN_MLDSA{44,65,87}`. The generic `gnutls_privkey_sign_data(..., GNUTLS_DIG_SHA256, ...)` does not work for ML-DSA.
- ML-KEM-768 keygen works (10.9 µs/op); ML-KEM-1024 keygen is a stub in this version.
- ML-KEM encaps/decaps are not exposed in the GnuTLS public API — ML-KEM is only available as TLS hybrid key exchange (e.g., X25519MLKEM768).
- Reference: [RHEL 10 PQC Interoperability](https://access.redhat.com/articles/7119430) confirms GnuTLS supports ML-DSA-44/65/87 signing and ML-KEM hybrid TLS groups.

## Scripts


| Script                                                | Purpose                                                                                                                                  |
| ----------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `scripts/build-deps.sh`                               | Builds all crypto libraries (OpenSSL 4.x, OpenSSL 3.x, leancrypto, GnuTLS, BoringSSL, AWS-LC) from source into `third_party/install/` |
| `scripts/run-pqc-bench.sh [outdir]`                   | Full benchmark sweep across all libraries/algorithms/threads                                                                             |
| `scripts/perf-profile.sh [lib] [algo] [op] [seconds]` | Profile a specific combination with `perf stat` and `perf record`                                                                        |




## Interpreting Results

The benchmark outputs:

- **ops/sec** — total operations per second across all threads
- **ns/op** — average nanoseconds per operation (aggregate)

Key things to look for:

1. **Thread scaling**: Does ops/sec scale linearly with thread count? PQC algorithms are compute-bound so should scale well.
2. **Library comparison**: Compare ops/sec for the same algorithm/operation across libraries at the same thread count.
3. **KEM vs DSA cost**: ML-DSA operations are significantly slower than ML-KEM (expect 5-20x difference in ns/op).
4. **SLH-DSA cost**: SLH-DSA is 100-1000x slower than ML-DSA for signing. Verify is ~2-5x slower than ML-DSA verify.
5. **Parameter set scaling**: Higher security levels (768→1024, 65→87) increase cost — quantify the overhead.
6. **'s' vs 'f' tradeoff**: SLH-DSA 's' variants are ~20x slower for sign but produce smaller signatures.



## Project Structure

```
benchmark/
├── CMakeLists.txt           # Top-level build (dynamic linking, all from source)
├── common/
│   ├── bench_common.h       # Shared types and API
│   └── bench_common.cpp     # Argument parsing, timing, output
├── backends/
│   ├── openssl/             # OpenSSL 4.x + 3.x (EVP API, same source)
│   ├── boringssl/           # BoringSSL (direct API + SLH-DSA)
│   ├── aws_lc/              # AWS-LC (EVP API)
│   └── gnutls/              # GnuTLS (abstract API + leancrypto PQC)
├── scripts/
│   ├── build-deps.sh        # Build ALL dependencies from source (incl. leancrypto)
│   ├── run-pqc-bench.sh     # Full benchmark sweep
│   └── perf-profile.sh      # perf profiling
├── third_party/             # Built dependencies (gitignored)
└── results/                 # Benchmark output (gitignored)
```

