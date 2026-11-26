# PQC Primitive Microbenchmark Suite

Microbenchmark for **post-quantum cryptographic primitives** (ML-KEM, ML-DSA, and SLH-DSA) across multiple libraries. This measures raw algorithm performance (keygen, encaps/decaps, sign/verify) — it does **not** benchmark TLS handshakes or protocol-level operations.

All crypto libraries are **built from source** in `third_party/` — no system `*-devel` packages are used for the benchmarked libraries themselves.

## Prerequisites

All crypto libraries are built from source by `scripts/build-deps.sh`. The benchmark binaries themselves only need a C++ toolchain; the packages below are required to compile OpenSSL, GnuTLS, BoringSSL, AWS-LC, and leancrypto.

### Build tools


| Package   | Notes                                                                 |
| --------- | --------------------------------------------------------------------- |
| `gcc-c++` | C++17 compiler for benchmark binaries                                 |
| `cmake`   | Build system (>= 3.16); used for BoringSSL, AWS-LC, and the benchmark |
| `make`    | Used by OpenSSL and GnuTLS builds                                     |
| `git`     | Clone OpenSSL, BoringSSL, AWS-LC, and leancrypto from source          |
| `curl`    | Download GnuTLS release tarball                                       |
| `golang`  | Required by BoringSSL and AWS-LC build systems                        |
| `meson`   | Build system for leancrypto (`pip install --user meson ninja`)        |
| `ninja`   | Parallel build backend for meson                                      |
| `nasm`    | x86 assembler; required by Intel IPP Crypto build                     |
| `perf`    | Optional; used by `scripts/perf-profile.sh`                           |
| `js-d3-flame-graph` | Optional; flamegraph visualization for perf profiling      |




### OpenSSL build (Perl modules)

OpenSSL 4.x runs Perl configure scripts that expect these modules on a minimal install:


| Package              | Notes                   |
| -------------------- | ----------------------- |
| `perl-FindBin`       | OpenSSL `Configure`     |
| `perl-IPC-Cmd`       | OpenSSL `Configure`     |
| `perl-File-Compare`  | OpenSSL `Configure`     |
| `perl-File-Copy`     | OpenSSL `configdata.pm` |
| `perl-File-Path`     | OpenSSL `Configure`     |
| `perl-File-Temp`     | OpenSSL `Configure`     |
| `perl-Time-Piece`    | OpenSSL 4.x `Configure` |
| `perl-Text-Template` | OpenSSL `Configure`     |




### GnuTLS build dependencies


| Package              | Notes                                            |
| -------------------- | ------------------------------------------------ |
| `nettle-devel`       | libnettle / libhogweed (checked by `pkg-config`) |
| `gmp-devel`          | GNU multiple precision arithmetic                |
| `libtasn1-devel`     | ASN.1 parsing                                    |
| `libunistring-devel` | Unicode string handling                          |




### leancrypto (PQC backend for GnuTLS)

leancrypto provides the ML-DSA and ML-KEM implementations used by GnuTLS. It is built with meson/ninja (no system packages required beyond a C compiler). Install meson with `pip install --user meson ninja` if not available.

Install everything on Fedora/RHEL:

```bash
sudo dnf install gcc-c++ cmake make git curl golang \
    perl-FindBin perl-IPC-Cmd perl-File-Compare perl-File-Copy perl-File-Path \
    perl-File-Temp perl-Time-Piece perl-Text-Template \
    nettle-devel gmp-devel libtasn1-devel libunistring-devel \
    python3-meson-python meson ninja-build nasm \
    perf js-d3-flame-graph
```

`scripts/build-deps.sh` checks for Perl's `File::Copy`, GnuTLS build dependencies (`nettle`, `hogweed`, `gmp`, `libtasn1` via `pkg-config`) and meson/ninja before starting.

## Quick Start

```bash
# 1. Build ALL third-party dependencies from source
bash scripts/build-deps.sh

# 2. Build the benchmark binaries
cmake -B build
cmake --build build -j$(nproc)

# 3. Run the full benchmark sweep
bash scripts/run-pqc-bench.sh
```

Results are saved to `results/<timestamp>/summary.csv`.

Dependency versions were checked against upstream releases on 2026-11-26:

- [OpenSSL 4.1.0](https://github.com/openssl/openssl/releases/tag/openssl-4.1.0) and
  [OpenSSL 3.6.5](https://github.com/openssl/openssl/releases/tag/openssl-3.6.5)
  (the latter preserves the existing 3.6 comparison series).
- [BoringSSL 0.20261105.0](https://boringssl.googlesource.com/boringssl/+/refs/tags/0.20261105.0).
- [AWS-LC 5.15.0](https://github.com/aws/aws-lc/releases/tag/v5.15.0).
- [GnuTLS 3.8.14](https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/), using
  [leancrypto 1.9.1](https://github.com/smuellerDD/leancrypto/releases/tag/v1.9.1) for PQC.
- [Intel IPP Crypto 2.4.0](https://github.com/intel/cryptography-primitives/releases/tag/v2.4.0).

Re-run steps 1 and 2 to upgrade an existing checkout. The dependency script checks
installed version stamps, updates source tags, and rebuilds older or unknown
installations. GnuTLS also rebuilds when the pinned leancrypto version changes.
Set `NPROC=8` when running the dependency script to limit parallel build jobs.
Existing performance reports retain the versions originally measured; run a new
sweep for updated comparisons. Benchmark output records the linked library version;
BoringSSL uses its installed build stamp because it lacks a release version API.

### Dynamic Linking Note

All libraries are built as shared libraries (`.so`). Each benchmark binary has RPATH/RUNPATH set to resolve its own `libcrypto.so` / `libgnutls.so` / `libleancrypto.so` correctly. **Do not** set a global `LD_LIBRARY_PATH` containing multiple library paths — several libraries share the same `libcrypto.so` filename and will cross-load incorrectly. The `bench_gnutls` binary uses `DT_RPATH` (via `--disable-new-dtags`) to ensure the transitive leancrypto dependency is resolved correctly.

### Profiling-Ready Build Flags

All libraries and benchmark binaries are compiled with:

```
-O3 -g -fno-omit-frame-pointer
```

| Flag | Purpose | Runtime Cost |
| ---- | ------- | ------------ |
| `-O3` | Full optimization | — |
| `-g` | DWARF debug info (`.debug_info`, `.debug_line`) — enables `perf annotate` with source lines and richer flamegraphs | **Zero** (debug sections stored on disk but not loaded into RAM at runtime) |
| `-fno-omit-frame-pointer` | Preserves the `%rbp` frame pointer register, enabling reliable frame-pointer-based stack unwinding by `perf` | **<1%** (one fewer general register; standard on Fedora 40+, RHEL 10+ system packages) |

This combination ensures that `perf record -g` (frame-pointer mode) produces complete call stacks through both the benchmark harness and the crypto library internals. Without `-fno-omit-frame-pointer`, GCC at `-O3` omits frame pointers on x86_64, causing flat/broken flamegraphs where all library functions appear as disconnected leaf nodes.

The same flags are used by Google, Meta, and Netflix for production profiling builds, and match the Fedora/RHEL system package policy since Fedora 38+ ([Fedora Changes/fno-omit-frame-pointer](https://fedoraproject.org/wiki/Changes/fno-omit-frame-pointer)).

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


| Library        | Version      | PQC Backend    | ML-KEM-512 | ML-KEM-768  | ML-KEM-1024 | ML-DSA-44       | ML-DSA-65       | ML-DSA-87       | SLH-DSA               | In Sweep |
| -------------- | ------------ | -------------- | ---------- | ----------- | ----------- | --------------- | --------------- | --------------- | --------------------- | -------- |
| OpenSSL 4.x    | 4.1.0        | built-in       | all ops    | all ops     | all ops     | all ops         | all ops         | all ops         | all 12 variants       | Yes      |
| OpenSSL 3.x    | 3.6.5        | built-in       | all ops    | all ops     | all ops     | all ops         | all ops         | all ops         | all 12 variants       | Yes      |
| AWS-LC         | 5.15.0        | built-in       | all ops    | all ops     | all ops     | all ops         | all ops         | all ops         | —                     | Yes      |
| BoringSSL      | 0.20261105.0 | built-in       | —          | all ops     | —           | —               | all ops         | —               | SHA2-128s, SHAKE-256f | Yes      |
| GnuTLS         | 3.8.14       | leancrypto 1.9.1 | —          | keygen only | —           | keygen/sign/ver | keygen/sign/ver | keygen/sign/ver | —                     | Yes      |
| IPP Crypto     | 2.4.0        | built-in       | all ops    | all ops     | all ops     | all ops         | all ops         | all ops         | —                     | Yes      |




### Library Limitations

- **BoringSSL**: Only supports ML-KEM-768, ML-DSA-65, SLH-DSA-SHA2-128s, and SLH-DSA-SHAKE-256f (limited parameter sets). Returns exit code 2 for unsupported combinations.
- **AWS-LC**: Full ML-KEM and ML-DSA support. **No SLH-DSA support** in this version.
- **Intel IPP Crypto**: Full ML-KEM (512/768/1024) and ML-DSA (44/65/87) support with FIPS 203/204 compliant implementations. **No SLH-DSA support**. Uses CPU-specific optimizations (AVX-512, AVX2, SSE4.2) dispatched at runtime. ML-KEM and ML-DSA APIs are stable as of 2.4.0; preview macros are no longer required. Requires RDRAND/RDSEED hardware RNG support (Intel Ivy Bridge+ / AMD Zen+). Version **2.3.0+** is required (fixes a multi-threaded ML-KEM `decaps` stack-smashing bug present in 2.2.0).
- **ML-KEM-512**: Not supported by BoringSSL.



### GnuTLS PQC Support (leancrypto)

GnuTLS 3.8.14 requires the `--with-leancrypto` configure flag to enable PQC algorithms. Without it, keygen calls are no-ops (completing in nanoseconds instead of microseconds) and sign/verify return empty results. The `build-deps.sh` script builds leancrypto 1.9.1 from source and configures GnuTLS with PQC enabled.

**Supported operations** (verified via dry-run):


| Algorithm   | keygen | sign | verify | encaps/decaps                |
| ----------- | ------ | ---- | ------ | ---------------------------- |
| ML-KEM-768  | yes    | —    | —      | Not in public API (TLS-only) |
| ML-KEM-1024 | no-op  | —    | —      | Not in public API (TLS-only) |
| ML-DSA-44   | yes    | yes  | yes    | —                            |
| ML-DSA-65   | yes    | yes  | yes    | —                            |
| ML-DSA-87   | yes    | yes  | yes    | —                            |


**Key findings:**

- ML-DSA sign/verify requires `gnutls_privkey_sign_data2()` / `gnutls_pubkey_verify_data2()` with `GNUTLS_SIGN_MLDSA{44,65,87}`. The generic `gnutls_privkey_sign_data(..., GNUTLS_DIG_SHA256, ...)` does not work for ML-DSA.
- ML-KEM-768 keygen works (10.9 µs/op); ML-KEM-1024 keygen is a stub in this version.
- ML-KEM encaps/decaps are not exposed in the GnuTLS public API — ML-KEM is only available as TLS hybrid key exchange (e.g., X25519MLKEM768).
- Reference: [RHEL 10 PQC Interoperability](https://access.redhat.com/articles/7119430) confirms GnuTLS supports ML-DSA-44/65/87 signing and ML-KEM hybrid TLS groups.



## Scripts


| Script                                                | Purpose                                                                                                                               |
| ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| `scripts/build-deps.sh`                               | Builds all crypto libraries (OpenSSL 4.x/3.x, leancrypto, GnuTLS, BoringSSL, AWS-LC, IPP Crypto) from source into `third_party/install/` |
| `scripts/run-pqc-bench.sh [outdir]`                   | Full benchmark sweep across all libraries/algorithms/threads                                                                          |
| `scripts/perf-profile.sh [lib] [algo] [op] [seconds]` | Profile a specific combination with `perf stat`, `perf record`, and flamegraph                                                        |




## Performance Profiling & Flamegraphs

The `scripts/perf-profile.sh` script performs detailed profiling of a single library/algorithm/operation combination using the Linux `perf` tool. It generates three outputs:

1. **perf stat** — hardware counter summary (cycles, instructions, cache misses, IPC)
2. **perf report** — text-based call-graph profile showing hottest functions
3. **flamegraph** — interactive HTML visualization of CPU time distribution

### Dependencies

```bash
sudo dnf install perf js-d3-flame-graph
```

| Package | Purpose |
| ------- | ------- |
| `perf` | Linux performance counters — `perf stat`, `perf record`, `perf report` |
| `js-d3-flame-graph` | D3.js flamegraph templates used by `perf script report flamegraph` |

On Fedora the packages are the same as on RHEL 9+ (see [Red Hat — Getting Started with Flamegraphs](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/9/html/monitoring_and_managing_system_status_and_performance/getting-started-with-flamegraphs_monitoring-and-managing-system-status-and-performance)).

### Usage

```bash
# Basic: profile OpenSSL ML-KEM-768 encaps for 10 seconds (default)
bash scripts/perf-profile.sh openssl ML-KEM-768 encaps

# Multi-threaded profiling with custom sampling frequency
bash scripts/perf-profile.sh --threads 4 --freq 4999 aws_lc ML-DSA-65 sign 15

# Skip flamegraph (only stat + report)
bash scripts/perf-profile.sh --no-flamegraph boringssl ML-KEM-1024 keygen 5
```

### Options

| Flag | Default | Description |
| ---- | ------- | ----------- |
| `--threads N` | 1 | Number of benchmark threads |
| `--freq N` | 997 | Sampling frequency (Hz); use a prime to avoid aliasing |
| `--no-flamegraph` | — | Skip HTML flamegraph generation |

### Output Files

All outputs are written to `results/perf/` with the naming pattern:

```
<lib>_<algo>_<op>_t<threads>_<timestamp>_stat.txt
<lib>_<algo>_<op>_t<threads>_<timestamp>_perf.data
<lib>_<algo>_<op>_t<threads>_<timestamp>_report.txt
<lib>_<algo>_<op>_t<threads>_<timestamp>_flamegraph.html
```

### Viewing Flamegraphs

```bash
xdg-open results/perf/openssl_ML-KEM-768_encaps_t1_20260826_120000_flamegraph.html
```

The flamegraph is self-contained HTML — open it in any browser. Key interactions:
- **Click** a box to zoom into that subtree
- **Reset Zoom** button returns to the full view
- **Width** of each box represents time spent in/under that function
- **Y-axis** represents stack depth (callers below, leaf functions on top)

### Interpreting Results

**perf stat** key metrics:
- **IPC** (instructions-per-cycle): PQC algorithms should achieve >2.0 IPC on modern CPUs
- **Cache references/misses**: ML-KEM/ML-DSA should have very low miss rates (data fits in L1/L2)
- **Branch mispredictions**: constant-time code may show higher misprediction rates by design

**Flamegraph** patterns to look for:
- Wide boxes at the top indicate hot leaf functions (e.g., polynomial multiplication, NTT)
- "Towers" (deep thin stacks) indicate call overhead — candidates for inlining
- Compare flamegraphs across libraries to understand architectural differences

### Troubleshooting

| Symptom | Fix |
| ------- | --- |
| `perf` permission denied | Run with `sudo`, or set `kernel.perf_event_paranoid=1` in sysctl |
| Flamegraph shows `[unknown]` | Libraries must be built with `-g` (already done by `build-deps.sh`) |
| `js-d3-flame-graph` not found | `sudo dnf install js-d3-flame-graph` (script falls back to CDN download) |
| Flat flamegraph (no stacks) | Libraries need `-fno-omit-frame-pointer` — rebuild with `build-deps.sh` |
| Only OS symbols, not ours | Verify RPATH with `ldd build/bench_openssl` — should point to `third_party/install/` |

---

## Interpreting Benchmark Results

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
│   ├── gnutls/              # GnuTLS (abstract API + leancrypto PQC)
│   └── ipp_crypto/          # Intel IPP Crypto (native FIPS 203/204 API)
├── scripts/
│   ├── build-deps.sh        # Build ALL dependencies from source (incl. leancrypto)
│   ├── run-pqc-bench.sh     # Full benchmark sweep
│   └── perf-profile.sh      # perf profiling
├── third_party/             # Built dependencies (gitignored)
└── results/                 # Benchmark output (gitignored)
```

