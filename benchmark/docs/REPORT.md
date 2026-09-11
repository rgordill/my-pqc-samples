# PQC Benchmark Report

## Environment


| Property  | Value                                            |
| --------- | ------------------------------------------------ |
| CPU       | Intel Core Ultra 7 165U (14 threads, P+E hybrid) |
| Kernel    | 7.1.5-200.fc44.x86_64                            |
| OS        | Fedora 44                                        |
| OpenSSL   | 3.5.7 (9 Jun 2026) — system shared library       |
| BoringSSL | 0.20260803.0 (shared library build from source)  |
| AWS-LC    | v5.5.0 (shared library build from source)        |
| Linking   | Dynamic (.so) for all libraries                  |
| Build     | cmake Release, -O3 -march=native                 |
| Date      | 2026-08-18                                       |




## Summary Table (1 thread, ops/sec)



### ML-KEM (Key Encapsulation)


| Algorithm   | Operation | OpenSSL | BoringSSL | AWS-LC  |
| ----------- | --------- | ------- | --------- | ------- |
| ML-KEM-512  | keygen    | 47,890  | —         | 109,940 |
| ML-KEM-512  | encaps    | 59,610  | —         | 113,796 |
| ML-KEM-512  | decaps    | 40,749  | —         | 94,438  |
| ML-KEM-768  | keygen    | 26,207  | 36,260    | 78,734  |
| ML-KEM-768  | encaps    | 42,942  | 55,993    | 67,346  |
| ML-KEM-768  | decaps    | 28,547  | 39,136    | 58,996  |
| ML-KEM-1024 | keygen    | 17,602  | —         | 54,257  |
| ML-KEM-1024 | encaps    | 31,470  | —         | 49,971  |
| ML-KEM-1024 | decaps    | 21,588  | —         | 51,618  |




### ML-DSA (Digital Signatures)


| Algorithm | Operation | OpenSSL | BoringSSL | AWS-LC |
| --------- | --------- | ------- | --------- | ------ |
| ML-DSA-44 | keygen    | 12,259  | —         | 31,022 |
| ML-DSA-44 | sign      | 2,301   | —         | 9,522  |
| ML-DSA-44 | verify    | 10,866  | —         | 29,807 |
| ML-DSA-65 | keygen    | 6,648   | 8,144     | 14,400 |
| ML-DSA-65 | sign      | 1,487   | 2,809     | 5,828  |
| ML-DSA-65 | verify    | 7,705   | 11,074    | 18,265 |
| ML-DSA-87 | keygen    | 4,570   | —         | 10,488 |
| ML-DSA-87 | sign      | 1,260   | —         | 4,882  |
| ML-DSA-87 | verify    | 4,376   | —         | 11,118 |




### SLH-DSA (Stateless Hash-Based Signatures)


| Algorithm          | Operation | OpenSSL | BoringSSL | AWS-LC |
| ------------------ | --------- | ------- | --------- | ------ |
| SLH-DSA-SHA2-128s  | keygen    | 28.5    | 33.3      | —      |
| SLH-DSA-SHA2-128s  | sign      | 3.9     | 4.5       | —      |
| SLH-DSA-SHA2-128s  | verify    | 3,912   | 4,079     | —      |
| SLH-DSA-SHA2-128f  | keygen    | 1,807   | —         | —      |
| SLH-DSA-SHA2-128f  | sign      | 80.8    | —         | —      |
| SLH-DSA-SHA2-128f  | verify    | 1,311   | —         | —      |
| SLH-DSA-SHA2-192f  | keygen    | 1,171   | —         | —      |
| SLH-DSA-SHA2-192f  | sign      | 42.0    | —         | —      |
| SLH-DSA-SHA2-192f  | verify    | 768     | —         | —      |
| SLH-DSA-SHA2-256f  | keygen    | 450     | —         | —      |
| SLH-DSA-SHA2-256f  | sign      | 21.1    | —         | —      |
| SLH-DSA-SHA2-256f  | verify    | 817     | —         | —      |
| SLH-DSA-SHAKE-128f | keygen    | 624     | —         | —      |
| SLH-DSA-SHAKE-128f | sign      | 27.4    | —         | —      |
| SLH-DSA-SHAKE-128f | verify    | 449     | —         | —      |
| SLH-DSA-SHAKE-256f | keygen    | 164     | 152       | —      |
| SLH-DSA-SHAKE-256f | sign      | 8.5     | 7.8       | —      |
| SLH-DSA-SHAKE-256f | verify    | 333     | 306       | —      |



## SLH-DSA Performance Characteristics

SLH-DSA is orders of magnitude slower than ML-DSA due to its hash-based construction:


| Comparison    | ML-DSA-65 sign | SLH-DSA-SHA2-128s sign | SLH-DSA-SHA2-128f sign | Factor (vs ML-DSA) |
| ------------- | -------------- | ---------------------- | ---------------------- | ------------------ |
| OpenSSL ops/s | 1,487          | 3.9                    | 80.8                   | 381x / 18x slower  |
| ns/op         | 672 µs         | 255 ms                 | 12.4 ms                | —                  |


Key observations:

- **'s' (small) variants trade speed for smaller signatures** — SHA2-128s sign takes 255ms per operation (3.9 ops/s). This makes it unsuitable for latency-sensitive applications.
- **'f' (fast) variants are ~20x faster** than 's' variants for sign, but still 10-20x slower than ML-DSA.
- **Verify is relatively fast** for all SLH-DSA variants (768-4,079 ops/s), making SLH-DSA viable when signatures are verified much more often than generated.
- **SHAKE variants are slower than SHA2** variants of the same security level (~2x for sign), as SHAKE is computationally heavier.
- **Higher security levels scale steeply** — SHA2-256f sign is 3.8x slower than SHA2-128f sign.
- SLH-DSA's main value is as a **conservative fallback** — it relies only on hash function security (no lattice assumptions), providing insurance against future cryptanalytic advances against structured lattices.



## Thread Scaling Analysis



### ML-KEM-768 encaps — scaling factor (ops/sec relative to 1 thread)


| Threads | OpenSSL | BoringSSL | AWS-LC |
| ------- | ------- | --------- | ------ |
| 1       | 1.00x   | 1.00x     | 1.00x  |
| 2       | 2.02x   | 2.05x     | 1.78x  |
| 4       | 3.24x   | 3.52x     | 2.58x  |
| 8       | 5.12x   | 5.86x     | 3.75x  |




### ML-DSA-65 sign — scaling factor


| Threads | OpenSSL | BoringSSL | AWS-LC |
| ------- | ------- | --------- | ------ |
| 1       | 1.00x   | 1.00x     | 1.00x  |
| 2       | 1.64x   | 1.65x     | 1.75x  |
| 4       | 2.65x   | 2.70x     | 2.54x  |
| 8       | 5.06x   | 3.79x     | 3.39x  |




### SLH-DSA-SHA2-128f sign — scaling factor (OpenSSL)


| Threads | OpenSSL | BoringSSL (SHA2-128s) |
| ------- | ------- | --------------------- |
| 1       | 1.00x   | 1.00x                 |
| 2       | 1.62x   | 1.87x                 |
| 4       | 4.03x   | 3.16x                 |
| 8       | 5.97x   | 4.89x                 |


**Observations:**

- ML-KEM encaps scales well (3.2-3.5x at 4 threads, 5-6x at 8 threads). AWS-LC scales less linearly than OpenSSL/BoringSSL at high thread counts.
- ML-DSA sign scales moderately (2.5-2.7x at 4 threads) due to rejection sampling introducing variable execution time.
- SLH-DSA sign scales very well (4-6x at 8 threads) — the long per-operation time means thread management overhead is negligible relative to computation.
- BoringSSL and OpenSSL show similar scaling characteristics with dynamic linking.



## Cross-Library Comparison



### Performance Ranking (single-thread ops/sec)

**ML-KEM-768 encaps:**

1. AWS-LC: 67,346 ops/s (1.00x baseline)
2. BoringSSL: 55,993 ops/s (0.83x)
3. OpenSSL: 42,942 ops/s (0.64x)

**ML-DSA-65 sign:**

1. AWS-LC: 5,828 ops/s (1.00x baseline)
2. BoringSSL: 2,809 ops/s (0.48x)
3. OpenSSL: 1,487 ops/s (0.26x)

**ML-DSA-65 verify:**

1. AWS-LC: 18,265 ops/s (1.00x baseline)
2. BoringSSL: 11,074 ops/s (0.61x)
3. OpenSSL: 7,705 ops/s (0.42x)

**SLH-DSA-SHA2-128s verify (where comparable):**

1. BoringSSL: 4,079 ops/s (1.00x baseline)
2. OpenSSL: 3,912 ops/s (0.96x)

**Key findings:**

- **AWS-LC is consistently fastest** for ML-KEM and ML-DSA, 1.6-3.9x faster than OpenSSL. This reflects hand-optimized assembly implementations.
- **BoringSSL** is 1.3-1.9x faster than OpenSSL for supported algorithms, positioned between AWS-LC and OpenSSL.
- **OpenSSL** is slowest for ML-KEM/ML-DSA but has the broadest SLH-DSA coverage (all 12 variants).
- **SLH-DSA performance is similar** between OpenSSL and BoringSSL for the overlapping variant (SHA2-128s), suggesting both use comparable hash-based implementations without architecture-specific optimizations.



### Cost of Security Level Increase (OpenSSL, 1 thread)


| Transition               | keygen      | encaps/sign | decaps/verify |
| ------------------------ | ----------- | ----------- | ------------- |
| ML-KEM-512 → 768         | 1.8x slower | 1.4x slower | 1.4x slower   |
| ML-KEM-768 → 1024        | 1.5x slower | 1.4x slower | 1.3x slower   |
| ML-DSA-44 → 65           | 1.8x slower | 1.5x slower | 1.4x slower   |
| ML-DSA-65 → 87           | 1.5x slower | 1.2x slower | 1.8x slower   |
| SLH-DSA-SHA2-128f → 192f | 1.5x slower | 1.9x slower | 1.7x slower   |
| SLH-DSA-SHA2-192f → 256f | 2.6x slower | 2.0x slower | 1.1x slower   |




## perf Findings



### ML-KEM-768 encaps — perf stat (1 thread, 5 seconds)


| Metric                    | OpenSSL | AWS-LC |
| ------------------------- | ------- | ------ |
| IPC (P-core)              | 4.7     | 3.2    |
| Branch miss rate (P-core) | 0.4%    | 0.3%   |
| LLC miss rate (P-core)    | 66.1%   | 71.5%  |
| Backend bound (P-core)    | 18.0%   | 30.9%  |
| Retiring (P-core)         | 75.4%   | 63.4%  |
| Frontend bound (P-core)   | 4.6%    | 4.0%   |
| Bad speculation (P-core)  | 2.0%    | 1.7%   |


**Analysis:**

- OpenSSL achieves **higher IPC** (4.7 vs 3.2) in this dynamic-link run, indicating good instruction-level parallelism.
- AWS-LC is more **backend-bound** (30.9% vs 18.0%), suggesting it spends more time waiting for memory/execution resources — possibly the dynamic dispatch overhead or different code layout.
- Both have very low branch miss rates (0.3-0.4%), confirming the algorithms are highly predictable.
- Despite lower IPC, AWS-LC is 1.6x faster in ops/sec due to fewer total instructions needed per operation (algorithmic efficiency beats micro-architectural efficiency).
- Near-zero context switches and page faults confirm this is a pure compute benchmark.



## Intel IPP Crypto vs OpenSSL 4.0.1 (ML-KEM-768)

This section compares Intel Cryptography Primitives Library 2.3.0 against OpenSSL 4.0.1 for ML-KEM-768 on an AVX-512-capable server.


### Environment


| Property       | Value                                              |
| -------------- | -------------------------------------------------- |
| Host           | demo-0.sandbox5415.opentlc.com                     |
| CPU            | Intel Xeon Platinum 8488C (Sapphire Rapids)        |
| PMU            | sapphire_rapids                                    |
| ISA extensions | AVX-512F/DQ/BW/VL, AVX-512 IFMA, VBMI, VAES, GFNI |
| Threads        | 1 (single-thread benchmark)                        |
| OpenSSL        | 4.0.1 (built from source)                          |
| IPP Crypto     | 2.3.0 (`v2.3.0`)                                   |
| Date           | 2026-08-31                                         |


IPP Crypto dispatches to its `k1` code path (AVX-512 IFMA) on this CPU. Profiling confirms hot functions use the `k1_cp_*` prefix (`k1_cp_NTT`, `k1_cp_samplePolyCBD`, `k1_cp_byteEncode`, etc.).

> **Note:** Performance data below was collected with IPP Crypto 2.2.0. Version 2.3.0 fixes a multi-threaded ML-KEM `decaps` crash (stack-smashing when `threads > 1`); the benchmark now requires 2.3.0+ and runs all thread counts for decaps.



### Benchmark Results (ops/sec)


| Operation | OpenSSL 4.0.1 | IPP Crypto 2.3.0 | IPP vs OpenSSL |
| --------- | -------------:| ----------------:| --------------:|
| keygen    |        28,751 |           22,775 |     1.26× slower |
| encaps    |        50,222 |           21,499 |     2.33× slower |
| decaps    |        32,227 |           15,433 |     2.09× slower |


IPP Crypto is competitive on keygen (~79% of OpenSSL throughput) but still roughly 2× slower on encaps and decaps.



### perf stat Comparison (3-second runs, 1 thread)


| Metric (keygen)   | OpenSSL | IPP Crypto |
| ----------------- | -------:| ----------:|
| Instructions      |  39.1 B |      41.5 B |
| Cycles            |  12.9 B |      12.9 B |
| Ops completed     |  70,912 |      45,056 |
| Cycles/op         |  ~182k  |      ~287k  |
| Branch misses     |  53.2 M |      42.8 M |


| Metric (encaps)   | OpenSSL | IPP Crypto |
| ----------------- | -------:| ----------:|
| Instructions      |  54.7 B |      63.5 B |
| Cycles            |  12.9 B |      13.0 B |
| Ops completed     | 150,784 |      64,512 |
| Cycles/op         |   ~86k  |      ~201k  |


Both libraries consume similar total cycles over the same wall time, but OpenSSL completes significantly more operations — indicating lower per-operation overhead rather than a missing SIMD code path.



### Hot Function Profile (keygen)


**IPP Crypto (`k1` / AVX-512 IFMA path):**


| Function              | % CPU  |
| --------------------- | ------:|
| `k1_cp_samplePolyCBD` |   24.7 |
| `k1_cp_NTT`           |   19.2 |
| `cp_SampleNTT_MB4`    |   17.5 |
| `k1_cp_byteEncode`    |   17.0 |
| `keccak_rnd_loop`     |   12.7 |
| `k1_cp_multiplyNTT`   |    4.8 |


**OpenSSL 4.0.1:**


| Function         | % CPU  |
| ---------------- | ------:|
| `__KeccakF1600`  |   39.0 |
| `scalar_ntt`     |   21.3 |
| `matrix_expand`  |   15.1 |
| `ossl_ml_kem_genkey` | 9.8 |



### Hot Function Profile (encaps)


**IPP Crypto:**


| Function              | % CPU  |
| --------------------- | ------:|
| `k1_cp_samplePolyCBD` |   27.2 |
| `k1_cp_inverseNTT`    |   11.6 |
| `k1_cp_byteDecode`    |   10.3 |
| `k1_cp_NTT`           |    9.0 |
| `cp_SampleNTT_MB4`    |    8.1 |
| `k1_cp_byteEncode`    |    7.7 |
| `keccak_rnd_loop`     |   11.9 |
| `k1_cp_Compress`      |    3.6 |


OpenSSL concentrates work in a few tight routines; IPP Crypto spreads CPU time across CBD sampling, byte encode/decode, compress, and multi-buffer sampling helpers.



### Analysis


1. **AVX-512 IFMA dispatch is working.** IPP Crypto uses the `k1` platform code on this CPU. Keygen instruction count is within ~6% of OpenSSL, and the keygen performance gap is only ~1.3×.

2. **Encaps/decaps remain ~2× slower** because IPP spends significant time in serialization and sampling paths that OpenSSL handles more efficiently:
   - `k1_cp_byteEncode` / `k1_cp_byteDecode` / `k1_cp_Compress` — not prominent in OpenSSL's hot path
   - `cp_SampleNTT_MB4` — multi-buffer sampling overhead (~17% on keygen, ~8% on encaps)
   - `k1_cp_samplePolyCBD` — largest single cost on both keygen and encaps (~25–27%)

3. **Keccak/SHA-3:** OpenSSL's hand-optimized `__KeccakF1600` assembly is extremely cycle-efficient despite appearing as a large fraction of its profile. IPP uses C-based `keccak_rnd_loop` (~13% on keygen).

4. **RDRAND is not a significant factor** on this host. `k1_ippsPRNGenRDRAND` accounts for <0.5% of profile time. OpenSSL's software AES-CTR DRBG is similarly negligible.

5. **Per-operation efficiency is the gap.** With similar total cycles and instruction counts on keygen, OpenSSL completes ~58% more operations. On encaps, IPP uses ~16% more instructions and ~2.3× more cycles per operation due to additional encode/decode/compress work in the hot path.



### Conclusion


On an AVX-512 IFMA server, Intel IPP Crypto uses the optimal `k1` code path and is competitive for ML-KEM-768 keygen (~79% of OpenSSL throughput). It remains slower for encaps and decaps because OpenSSL 4.0.1 has a leaner implementation with tighter Keccak assembly, less byte serialization overhead, and fewer abstraction layers between the API and the hot loops. IPP Crypto's advantage is more likely to appear in workloads that benefit from its multi-buffer primitives or on operations where IFMA-accelerated NTT dominates total cost.

## Limitations

1. **GnuTLS excluded from benchmarks**: GnuTLS 3.8.13 PQC support is non-functional for benchmarking — ML-DSA sign/verify returns immediately without performing real work, ML-KEM encaps/decaps is not exposed in the public API, and ML-KEM-1024 keygen is a no-op. The backend is still built but excluded from the benchmark sweep. See README for details.
2. **BoringSSL limited algorithm coverage**: Only supports ML-KEM-768, ML-DSA-65, SLH-DSA-SHA2-128s, and SLH-DSA-SHAKE-256f. Cannot compare across security levels.
3. **AWS-LC no SLH-DSA support**: This version of AWS-LC does not implement SLH-DSA.
4. **Hybrid CPU variance**: The Intel Core Ultra 7 165U has P-cores + E-cores. Thread scheduling varies between runs, causing 10-30% variance in single-thread results depending on which core type is assigned.
5. **Shared library name collision**: BoringSSL and AWS-LC both export `libcrypto.so`. Binaries must use RPATH/RUNPATH for correct resolution. A global `LD_LIBRARY_PATH` covering both will cause symbol resolution failures.
6. **Not TLS performance**: This measures raw primitive operations, not protocol performance. Real TLS handshake cost includes serialization, network I/O, certificate parsing, and composite operations.

