# Benchmark Client: Motivation and Design

## Why Build a Custom Benchmark Client?

### The Problem

With NIST's standardization of post-quantum cryptographic algorithms (ML-KEM in FIPS 203, ML-DSA in FIPS 204, SLH-DSA in FIPS 205), organizations need to evaluate the performance implications of migrating to PQC. Multiple cryptographic libraries implement these algorithms, each with different optimization strategies, API designs, and maturity levels. A fair, reproducible comparison requires a purpose-built measurement tool.

### Existing Alternatives and Their Limitations


| Tool                         | Scope                      | Limitation for our use case                                                                                                                                                  |
| ---------------------------- | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `openssl speed`              | Single library (OpenSSL)   | Cannot compare across libraries. No ML-KEM encaps/decaps. No thread-scaling analysis. Results format not easily composable.                                                  |
| `bssl speed` (BoringSSL)     | Single library (BoringSSL) | Proprietary benchmarking harness tied to BoringSSL internals. No multi-library comparison.                                                                                   |
| AWS-LC `speed.cc`            | Single library (AWS-LC)    | AWS-LC internal tool. Reports different metrics. Not designed for cross-library comparison.                                                                                  |
| liboqs speed                 | OQS provider only          | Measures algorithms via liboqs abstraction layer, not the native library implementations. Adds overhead from the OQS wrapper. Does not test vendor-optimized code paths.     |
| SUPERCOP                     | Standalone implementations | Tests reference/optimized standalone code, not production library implementations. Complex setup. Not relevant for evaluating actual deployment libraries.                   |
| PQCrypto-LWEKE / PQM4        | Embedded targets           | Focused on constrained environments, not server-class hardware.                                                                                                              |
| Custom `openssl speed -evp`  | Single library             | Limited to OpenSSL's EVP interface. No SLH-DSA support in older versions. No thread-scaling beyond OpenSSL's internal multi-mode.                                            |
| Google Benchmark / Criterion | Generic framework          | Provides timing infrastructure but not the crypto-specific harness (key setup, encaps/decaps lifecycle, signature verification flow). Would still need all the backend code. |


**Key gap**: No existing tool provides a uniform comparison of the same PQC primitive operations across OpenSSL, BoringSSL, AWS-LC, and GnuTLS from a single harness with consistent methodology, thread scaling, and output format.

### What This Client Provides

1. **Multi-library comparison** — same algorithm, same operation, same measurement methodology across 5 libraries (OpenSSL 4.x, OpenSSL 3.x, BoringSSL, AWS-LC, GnuTLS)
2. **Thread-scaling analysis** — isolates whether library internals have lock contention under concurrent PQC workloads
3. **Reproducible builds** — all libraries are built from source at pinned versions, eliminating distro-specific patches and version skew
4. **Composable CSV output** — single consistent format for automated analysis and reporting

---



## Requirements and Constraints



### Functional Requirements


| ID   | Requirement                                                                                                       |
| ---- | ----------------------------------------------------------------------------------------------------------------- |
| FR-1 | Benchmark ML-KEM (512, 768, 1024) keygen, encaps, and decaps operations                                           |
| FR-2 | Benchmark ML-DSA (44, 65, 87) keygen, sign, and verify operations                                                 |
| FR-3 | Benchmark SLH-DSA (all 12 SHA2/SHAKE × 128/192/256 × s/f variants) keygen, sign, and verify                       |
| FR-4 | Support configurable thread counts (1, 2, 4, 8, ...)                                                              |
| FR-5 | Produce machine-readable CSV output with library name+version, algorithm, operation, threads, ops, ops/sec, ns/op |
| FR-6 | Print human-readable banner with library version and configuration before each run                                |
| FR-7 | Gracefully report "unsupported" (exit code 2) for algorithm/library combinations not available                    |




### Non-Functional Requirements


| ID    | Requirement                                                | Rationale                                                                                                              |
| ----- | ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| NFR-1 | Zero lock contention in the measurement path               | Locks would introduce synchronization artifacts, measuring OS scheduler behavior rather than algorithm performance     |
| NFR-2 | All computation in memory (no file I/O during measurement) | Disk I/O latency variance would dwarf the sub-microsecond crypto operations                                            |
| NFR-3 | Warmup phase before measurement                            | JIT effects, CPU frequency scaling, and cache warming affect initial iterations                                        |
| NFR-4 | Per-thread isolated state                                  | Threads must not share mutable key material or buffers                                                                 |
| NFR-5 | Minimal clock-check overhead                               | Checking elapsed time every iteration would dominate fast operations like ML-KEM encaps (~15µs)                        |
| NFR-6 | All libraries built from source at pinned versions         | Ensures reproducibility and eliminates distro patches                                                                  |
| NFR-7 | Dynamic linking for all libraries                          | Matches real deployment conditions; avoids LTO artifacts that wouldn't exist in production                             |
| NFR-8 | Single source file per backend                             | Keeps library-specific code isolated; no risk of header/symbol conflicts between libraries that export identical names |




### Constraints


| Constraint                                     | Impact                                                                                                     |
| ---------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Libraries share the `libcrypto.so` symbol name | Cannot link multiple libraries into one binary. Requires separate executables with RUNPATH isolation.      |
| BoringSSL has no stable version API            | Version must be passed as a compile-time definition from the build system.                                 |
| GnuTLS PQC API is incomplete in 3.8.x          | Sign/verify does not perform real work. Backend is built but excluded from automated sweeps.               |
| BoringSSL exposes limited parameter sets       | Only ML-KEM-768, ML-DSA-65, and two SLH-DSA variants. Cannot test full security-level sweep.               |
| SLH-DSA sign is extremely slow (~100ms–250ms)  | Clock-check amortization (every 256 iterations) is less relevant; each iteration is naturally long enough. |


---



## Architectural Decisions



### AD-1: One executable per library

**Decision**: Each library gets its own compiled binary (e.g., `bench_openssl`, `bench_boringssl`, `bench_aws_lc`).

**Rationale**: OpenSSL, BoringSSL, and AWS-LC all export symbols under the same names (`EVP_PKEY_keygen`, `libcrypto.so`, etc.). Linking more than one into a single binary is not possible without symbol renaming or dlopen tricks that would add unmeasured overhead. Separate binaries with RUNPATH isolation is the cleanest solution.

**Consequence**: A sweep script orchestrates running each binary and aggregates their CSV output.

### AD-2: Common harness library with callback-based architecture

**Decision**: A shared static library (`bench_common`) implements argument parsing, timing infrastructure, thread management, and result formatting. Backend code provides three callbacks: `SetupFn`, `BenchFn`, and `TeardownFn`.

**Rationale**: This separates measurement concerns from crypto-library-specific code. The hot loop is identical across all backends, ensuring fair comparison. Adding a new backend requires only implementing the three callbacks.

**Consequence**: `std::function<>` callbacks add a small (< 1ns) virtual dispatch overhead per iteration, but this is constant across all backends and negligible relative to crypto operations (typically 10µs–250ms).

### AD-3: Time-bounded measurement (not iteration-bounded)

**Decision**: The benchmark runs for a fixed duration (default 3 seconds) rather than a fixed number of iterations.

**Rationale**: PQC operations vary enormously in cost (ML-KEM-768 encaps ≈ 15µs vs SLH-DSA-SHA2-128s sign ≈ 250ms). A fixed iteration count would either run too briefly for fast operations or take hours for slow ones. Time-bounded runs naturally adapt: fast operations accumulate millions of samples, slow ones accumulate dozens — both statistically sufficient.

**Consequence**: Reporting uses ops/sec (a rate) rather than total time for N operations.

### AD-4: Clock-check amortization (every 256 iterations)

**Decision**: The elapsed-time check (`clock_gettime`) is performed only every 256 iterations during the measurement loop.

**Rationale**: `clock_gettime(CLOCK_MONOTONIC)` costs ~~20–25ns per call. For fast operations like ML-KEM-768 encaps (~~15µs), checking every iteration would add 0.15% overhead — acceptable but unnecessary. Checking every 256 iterations reduces this to effectively zero while keeping measurement duration accurate to within 256 × 15µs ≈ 4ms (0.1% of a 3-second run).

**Consequence**: The actual run duration may exceed the target by up to 256 iterations of the slowest operation. For SLH-DSA-SHA2-128s sign (250ms/op) this could mean up to 64 seconds of overshoot. In practice this doesn't occur because at 4 ops/sec, the 256-iteration check fires rarely and `ops & 0xFF == 0` matches on the first or second iteration after the deadline.

### AD-5: Per-thread state, no shared mutable data

**Decision**: Each thread owns its own key material, ciphertext buffers, signature buffers, and message data. No mutexes, atomics, or shared-memory communication exists in the hot loop.

**Rationale**: The benchmark must measure algorithm throughput, not lock contention. Any synchronization primitive would introduce artificial serialization and non-deterministic wait times. Even atomic increments create cache-line bouncing on multi-socket systems.

**Consequence**: Memory usage scales linearly with thread count. For PQC algorithms (key sizes 1–5 KB, signatures 2–50 KB), this is negligible even at high thread counts.

### AD-6: All libraries built from source at pinned tags

**Decision**: The `build-deps.sh` script builds all six comparison libraries (OpenSSL 4.1.0, OpenSSL 3.6.5, BoringSSL 0.20261105.0, AWS-LC v5.15.0, GnuTLS 3.8.14, Intel IPP Crypto 2.4.0) from release tags or official release archives, plus leancrypto 1.9.1 for GnuTLS PQC support. Installed version stamps trigger rebuilds when pins change.

**Rationale**: 

- System packages vary by distro, version, and applied patches (Fedora, Ubuntu, RHEL all ship different OpenSSL builds with different optimization flags)
- Ensures all libraries are built with the same compiler and flags (`-O3 -march=native`)
- Pinned versions guarantee reproducibility — re-running the benchmark months later produces comparable results
- Eliminates the "which OpenSSL?" ambiguity

**Consequence**: First-time setup takes 10–15 minutes to compile all dependencies. A `third_party/` directory (~500 MB) holds the build artifacts.

### AD-7: Dynamic linking (not static)

**Decision**: All libraries are built as shared objects (`.so`) and dynamically linked to the benchmark binaries.

**Rationale**: Static linking with LTO can inline across the library boundary, producing performance numbers unachievable in real deployments where applications link dynamically against system libraries. Dynamic linking with `-O3 -march=native` represents the best realistic performance.

**Consequence**: Binaries require RUNPATH to find their specific `libcrypto.so` at runtime. A global `LD_LIBRARY_PATH` cannot be used because multiple libraries export the same `libcrypto.so` filename.

### AD-8: Same source file for OpenSSL 3.x and 4.x

**Decision**: `bench_openssl.cpp` is compiled twice — once linked against OpenSSL 4.1.0 and once against OpenSSL 3.6.5 — producing two distinct binaries (`bench_openssl` and `bench_openssl3`).

**Rationale**: OpenSSL 4.x maintains backward compatibility with the 3.x EVP API for PQC operations. Maintaining a single source file ensures any API-level differences are immediately visible as compilation failures rather than subtle behavioral differences.

**Consequence**: If OpenSSL 4.x introduces new PQC APIs not in 3.x, conditional compilation (`#if OPENSSL_VERSION_MAJOR >= 4`) would be needed. Currently unnecessary.

---



## Implementation Details



### Thread Model

```
main()
  ├── parse_args() → Config{algorithm, operation, threads, seconds, warmup}
  ├── setup: allocate per-thread state vector
  ├── spawn N threads:
  │     ├── setup_fn(tid) → generate keys, pre-compute ciphertext/signature
  │     ├── warmup loop (no recording, cfg.warmup_seconds)
  │     ├── measurement loop:
  │     │     ├── bench_fn(tid)  ← one crypto operation
  │     │     ├── ops++
  │     │     └── if (ops & 0xFF == 0) check clock → break if elapsed ≥ target
  │     ├── record: results[tid] = {ops, elapsed_ns}
  │     └── teardown_fn(tid) → free keys
  ├── join all threads
  └── print_results() → per-thread table + aggregate + CSV line
```



### Timing Infrastructure

- **Clock**: `clock_gettime(CLOCK_MONOTONIC)` — monotonic, not affected by NTP adjustments, ~20ns resolution on Linux
- **Warmup**: Runs the operation loop for `warmup_seconds` (default 1s) without recording, ensuring CPU caches are hot and frequency governors have scaled up
- **Measurement**: Records wall-clock time and operation count per thread independently



### Backend Interface

Each backend implements three functions with thread-ID-indexed state:

```cpp
using SetupFn    = std::function<bool(int thread_id)>;   // Pre-allocate keys/buffers
using BenchFn    = std::function<void(int thread_id)>;   // One atomic operation
using TeardownFn = std::function<void(int thread_id)>;   // Free resources
```

The `BenchFn` must perform exactly one complete operation per call. It must not perform I/O, allocation (beyond stack), or access shared state.

### Output Format

Each run produces a CSV line:

```
Library (version),algorithm,operation,threads,total_ops,ops_per_sec,ns_per_op
```

Example:

```
OpenSSL (4.0.1),ML-KEM-768,encaps,4,602112,150528.0,6643.3
AWS-LC (5.15.0),ML-DSA-65,sign,1,17484,5828.0,171602.5
BoringSSL (0.20261105.0),SLH-DSA-SHA2-128s,verify,1,12237,4079.0,245159.1
```



### Error Handling Strategy


| Condition                                              | Behavior                                                                            |
| ------------------------------------------------------ | ----------------------------------------------------------------------------------- |
| Unsupported algorithm/operation/library combination    | Exit code 2, stderr message. Sweep script records as "SKIP".                        |
| Setup failure (keygen fails, API error)                | `setup_fn` returns `false`, thread produces 0 ops. Reported in per-thread results.  |
| Compilation failure (missing header, undefined symbol) | CMake conditionally disables the backend with a warning.                            |
| Library not built                                      | CMake emits a warning; binary is not produced. Sweep script skips missing binaries. |




### Build System Design

```
CMakeLists.txt
  ├── bench_common (STATIC) — timing, args, output
  ├── bench_openssl  — links openssl4/lib64/libcrypto.so (RUNPATH set)
  ├── bench_openssl3 — links openssl3/lib64/libcrypto.so (RUNPATH set)
  ├── bench_boringssl — links boringssl/lib/libcrypto.so (RUNPATH set)
  ├── bench_aws_lc   — links aws-lc/lib64/libcrypto.so (RUNPATH set)
  └── bench_gnutls   — links gnutls/lib/libgnutls.so (RUNPATH set)
```

Each target:

1. Sets `target_include_directories` to its library's install prefix
2. Links directly against the `.so` file path (not via `-l` search)
3. Sets `BUILD_RPATH` so the runtime linker finds the correct library without `LD_LIBRARY_PATH`

This ensures that even though four libraries export `libcrypto.so`, each binary resolves to exactly the right one.



## Why GPU-Accelerated PQC Is Not Included

Libraries such as [NVIDIA cuPQC](https://docs.nvidia.com/cuda/cupqc/index.html) offer GPU-accelerated ML-KEM and ML-DSA implementations. cuPQC was evaluated as a potential backend but is not included because its execution model is fundamentally incompatible with this benchmark's design. Adding it would require changing the harness, the metrics, and the output format — not just adding another backend.

### Execution Model Mismatch

| Aspect | This benchmark (CPU) | cuPQC (GPU) |
| --- | --- | --- |
| **API surface** | Host-side C/C++ function calls | CUDA `__device__` functions called from inside user-written kernels |
| **Unit of work** | Single operation per call | Batch of thousands of operations per kernel launch |
| **Threading** | `std::thread` with per-thread state | CUDA blocks and warps; `BlockDim` configurable per launch |
| **Memory** | Per-thread heap-allocated buffers | GPU global memory, shared memory, and registers; no host-side state during computation |
| **Timing** | Wall-clock `clock_gettime` in a tight loop | Must account for kernel launch overhead (~5–10 µs), host-device synchronization, and batch amortization |

The current harness calls a single crypto operation per iteration in a CPU loop, measuring `ns/op` directly. cuPQC operations are designed to run fused inside a CUDA kernel — calling a single ML-KEM encaps on a GPU would be *slower* than CPU due to kernel launch latency. The value of GPU acceleration appears only when batching 10,000+ concurrent operations, which is a different measurement question.

### Why the Metrics Are Not Comparable

- **CPU `ops/sec` at 1 thread** answers: *"How fast is one operation end-to-end?"* — relevant for latency-sensitive paths like TLS handshakes.
- **GPU `ops/sec` in a batch of 65,536** answers: *"What is the maximum throughput when amortizing launch overhead across many operations?"* — relevant for batch certificate issuance, bulk signature verification, or HSM offload.

Placing both numbers in the same CSV column (`ops_per_sec`) with the same schema would be misleading. A GPU reporting 500,000 ML-KEM-768 encaps/sec in a batch vs a CPU reporting 50,000/sec single-threaded are answering fundamentally different questions about system performance.

### Build System Impact

cuPQC requires the CUDA toolkit (`nvcc`), `enable_language(CUDA)` in CMake, and an NVIDIA GPU at runtime. Adding this as a dependency would break the build on any machine without a GPU — currently the benchmark compiles and runs on any x86_64 Linux host with a C++17 compiler.

### What a GPU Benchmark Would Require

A proper GPU PQC benchmark would need:

1. A separate harness that measures batch throughput with configurable batch sizes
2. GPU warmup and cooldown phases (JIT compilation, memory pool initialization)
3. An extended CSV schema with a `batch_size` column
4. Separate kernel-time vs wall-time metrics (to isolate launch overhead)
5. Memory transfer accounting (or confirmation that keys/ciphertexts stay on-device)

This is a valid and useful measurement — but it is a separate project with different design goals, not an additional backend for this CPU-focused harness.