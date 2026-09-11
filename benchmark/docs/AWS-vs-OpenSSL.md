## AWS-LC vs OpenSSL: PQC Implementation Analysis

This section analyses **why** AWS-LC (v5.5.0) is 1.6–3.9× faster than OpenSSL (4.0.1 / 3.6.3) for ML-KEM and ML-DSA, based on a source-level comparison. It proposes simple, architecturally consistent enhancements to OpenSSL.

### Performance Gap Summary

| Algorithm   | Operation | OpenSSL | AWS-LC  | Factor |
| ----------- | --------- | ------: | ------: | -----: |
| ML-KEM-512  | keygen    |  47,890 | 109,940 |  2.3×  |
| ML-KEM-768  | encaps    |  42,942 |  67,346 |  1.6×  |
| ML-KEM-768  | decaps    |  28,547 |  58,996 |  2.1×  |
| ML-DSA-65   | sign      |   1,487 |   5,828 |  3.9×  |
| ML-DSA-65   | verify    |   7,705 |  18,265 |  2.4×  |
| ML-DSA-65   | keygen    |   6,648 |  14,400 |  2.2×  |


### Root Cause 1: Scalar NTT vs AVX2 Hand-Optimized Assembly

**OpenSSL** implements ML-KEM NTT and ML-DSA NTT in **pure portable C**, using 16-bit (ML-KEM) or 32-bit (ML-DSA) scalar Barrett/Montgomery reduction:

```c
/* OpenSSL ML-KEM NTT — scalar, one coefficient at a time */
static void scalar_ntt(scalar *s) {
    do {
        do {
            even = *curr;
            odd = reduce(*peer * zeta);       /* Barrett: mul + shift + mul + sub */
            *peer++ = reduce_once(even - odd + kPrime);
            *curr++ = reduce_once(odd + even);
        } while (curr < pause);
    } while ((offset >>= 1) >= 2);
}
```

```c
/* OpenSSL ML-DSA NTT — scalar, 32-bit Montgomery */
void ossl_ml_dsa_poly_ntt(POLY *p) {
    for (j = k; j < k + offset; j++) {
        uint32_t w_even = p->coeff[j];
        uint32_t t_odd = reduce_montgomery(
            (uint64_t)z_step_root * (uint64_t)p->coeff[j + offset]);
        p->coeff[j] = reduce_once(w_even + t_odd);
        p->coeff[j + offset] = mod_sub(w_even, t_odd);
    }
}
```

**AWS-LC** uses **~6,700 lines of hand-written AVX2 x86-64 assembly** for ML-KEM and **~5,700 lines** for ML-DSA. These process 16 × 16-bit coefficients (ML-KEM) or 8 × 32-bit coefficients (ML-DSA) per `ymm` register instruction:

```asm
; AWS-LC ML-KEM NTT — AVX2, 16 coefficients per cycle
S2N_BN_SYMBOL(mlkem_ntt_avx2):
    movl $0xd010d01, %eax         ; Montgomery constant
    vmovd %eax, %xmm0
    vpbroadcastd %xmm0, %ymm0    ; broadcast to all lanes
    ; ... butterfly on 256-bit vectors ...
    vpmullw %ymm1, %ymm0, %ymm2  ; 16-wide multiply-low
    vpmulhw %ymm1, %ymm0, %ymm3  ; 16-wide multiply-high
    vpsubw  %ymm3, %ymm2, %ymm4  ; 16-wide subtract
```

In addition to NTT, AWS-LC implements the following in AVX2 assembly:
- **Inverse NTT** (`intt.S`)
- **Base multiplication with accumulation** (fused `basemul_acc` for k=2/3/4)
- **Compression/decompression** at all bit-widths (d4/d5/d10/d11)
- **Rejection uniform sampling** with lookup tables (`rej_uniform_asm.S`)
- **Byte serialization** (`ntttobytes.S`, `nttfrombytes.S`)
- **Pointwise polynomial multiplication** for ML-DSA (`pointwise_avx2_asm.S`)

**Impact estimate**: AVX2 processes 16 coefficients per instruction vs 1 in scalar C. The theoretical speedup is up to 16× for NTT-dominated operations, with real-world gains of 2–3× when amortized over the full operation (NTT is ~20–40% of total work).


### Root Cause 2: 4-Way Batched Keccak (SHA-3 / SHAKE)

ML-KEM and ML-DSA call SHAKE128/SHAKE256 dozens of times per operation for matrix expansion, secret sampling, and hashing. How these calls are batched is critical.

**OpenSSL** calls Keccak **sequentially, one at a time**, through the EVP digest API:

```c
/* OpenSSL matrix_expand — one polynomial at a time */
static int matrix_expand(EVP_MD_CTX *mdctx, ML_KEM_KEY *key) {
    for (i = 0; i < rank; i++) {
        for (j = 0; j < rank; j++) {
            /* One SHAKE128 stream per matrix entry */
            EVP_DigestInit_ex(mdctx, key->shake128_md, NULL);
            EVP_DigestUpdate(mdctx, input, sizeof(input));
            sample_scalar(out++, mdctx);  /* squeeze in a loop */
        }
    }
}
```

For ML-KEM-768 (rank=3), this means **9 sequential** SHAKE128 streams, each going through EVP dispatch on every `Init`/`Update`/`Squeeze` call.

**AWS-LC** batches **4 SHAKE128 streams at once** using a dedicated 4-way Keccak-f1600 implementation:

```c
/* AWS-LC gen_matrix — 4 polynomials at a time using x4 Keccak */
void mlk_gen_matrix(mlk_polymat *a, const uint8_t seed[MLKEM_SYMBYTES], int transposed) {
    for (i = 0; i < (MLKEM_K * MLKEM_K / 4) * 4; i += 4) {
        /* Batch 4 seeds, run 4 SHAKE128 streams in parallel */
        mlk_poly_rej_uniform_x4(&a->vec[...], &a->vec[...],
                                &a->vec[...], &a->vec[...], seed_ext);
    }
}
```

On x86-64 with AVX2, this calls `sha3_keccak4_f1600_alt()` which interleaves 4 Keccak states across `ymm` registers, amortizing the permutation cost:

```c
/* AWS-LC Keccak x4 dispatch (keccak1600.c) */
static void Keccak1600_x4(uint64_t A[4][5][5]) {
    if (CRYPTO_is_AVX2_capable()) {
        sha3_keccak4_f1600_alt((uint64_t *)A, iotas, keccak_rho8, keccak_rho56);
        return;
    }
    /* Fallback: 4x individual calls */
}
```

**Impact estimate**: For ML-KEM-768 matrix expansion (9 entries), batching cuts the number of Keccak-f1600 permutations by ~4×. Since Keccak is 30–40% of total CPU time in OpenSSL (see flamegraphs), this alone explains ~1.3–1.5× of the gap.


### Root Cause 3: EVP Abstraction Layer Overhead

**OpenSSL** routes every hash call through the `EVP_MD_CTX` / `EVP_DigestInit_ex` / `EVP_DigestUpdate` / `EVP_DigestSqueeze` API. This is a dynamic dispatch mechanism involving:
- Provider lookup (even if cached, it's an indirection through `evp_md_ctx->digest->dinit`)
- Parameter validation and error-path bookkeeping
- Generic state management (supports arbitrary algorithms)

In ML-KEM alone there are **21 EVP Digest API calls** in the implementation, and many more during a keygen/encaps/decaps cycle as they are inside loops. In ML-DSA, every SHAKE call goes through `shake_xof()`:

```c
/* OpenSSL ML-DSA hash wrapper — 3 EVP calls per SHAKE invocation */
static inline int shake_xof(EVP_MD_CTX *ctx, const EVP_MD *md,
                            const uint8_t *in, size_t in_len,
                            uint8_t *out, size_t out_len) {
    return (EVP_DigestInit_ex2(ctx, md, NULL) == 1
        && EVP_DigestUpdate(ctx, in, in_len) == 1
        && EVP_DigestSqueeze(ctx, out, out_len) == 1);
}
```

**AWS-LC** calls Keccak **directly** through thin static-inline wrappers:

```c
/* AWS-LC — direct Keccak call, no dynamic dispatch */
static MLK_INLINE void mlk_shake128x4_absorb_once(mlk_shake128x4ctx *state,
    const uint8_t *in0, const uint8_t *in1, const uint8_t *in2,
    const uint8_t *in3, size_t inlen) {
    SHAKE128_Absorb_once_x4(state, in0, in1, in2, in3, inlen);
}
```

**Impact estimate**: Each EVP round-trip adds ~50–100 ns of overhead (function pointer indirection, parameter setup, error checks). With dozens of SHAKE calls per ML-KEM operation (~10–15 calls for keygen), this can add ~1–2 µs per operation, which is ~5–10% of a 15–25 µs ML-KEM keygen.


### Root Cause 4: No Architecture-Specific PQC Assembly in OpenSSL

OpenSSL has **zero lines of architecture-specific assembly** for ML-KEM or ML-DSA:

```
$ find openssl4/crypto/ml_kem openssl4/crypto/ml_dsa -name '*.S' -o -name '*.pl'
(no results)
```

All NTT, polynomial multiplication, compression, decompression, and rejection sampling is in **portable C**, compiled with `-O3 -march=native`. The compiler auto-vectorizes some loops, but achieves nowhere near the efficiency of hand-tuned AVX2 assembly.

OpenSSL **does** have excellent hand-tuned Keccak-f1600 assembly (`keccak1600-avx2.pl`, `keccak1600-avx512.pl`), but it is called one state at a time (no x4 batching), which is a significant limitation.

AWS-LC, by contrast, carries `~12,400 lines of hand-written x86-64 AVX2 assembly` across ML-KEM and ML-DSA, covering every performance-critical operation.


### Proposed Enhancements to OpenSSL

The following proposals are ordered by **impact/effort ratio** and are **architecturally consistent** with OpenSSL's design patterns.


#### 1. Add 4-Way Batched Keccak API and Use It in Matrix Expansion (High Impact, Medium Effort)

**Problem**: OpenSSL has hand-optimized Keccak assembly (`keccak1600-avx2.pl`) but calls it one state at a time.

**Proposal**: Introduce a `SHA3_Absorb_x4()` / `SHA3_Squeeze_x4()` internal API that interleaves 4 Keccak states across AVX2 registers. Use it in `matrix_expand()` for ML-KEM and `rej_ntt_poly()` loops for ML-DSA.

This is consistent with OpenSSL's architecture: the Keccak assembly is already generated by `.pl` scripts for each architecture. Adding a `keccak1600x4-avx2.pl` variant that processes 4 states follows the same pattern.

**Expected gain**: 1.3–1.5× for keygen/encaps (Keccak is 30–40% of the profile).


#### 2. Add AVX2 NTT for ML-KEM (High Impact, Medium Effort)

**Problem**: ML-KEM NTT processes one 16-bit coefficient per cycle in scalar C. AVX2 can process 16 at once.

**Proposal**: Add an `ml_kem_ntt_avx2.pl` (or `.S`) that implements the butterfly NTT on `ymm` registers, following the well-known approach from CRYSTALS-Kyber AVX2 (Seiler, 2018). OpenSSL already has a pattern for architecture-specific assembly with `.pl` generator scripts.

The NTT is a self-contained function with a clear interface (`scalar *s → scalar *s`), making it possible to add without touching the rest of the ML-KEM code. A runtime `OPENSSL_ia32cap_P` check would select between the C and AVX2 paths.

**Expected gain**: 1.3–1.5× for all ML-KEM operations (NTT is ~20% of the profile).


#### 3. Bypass EVP for Internal SHAKE Calls in ML-KEM/ML-DSA (Medium Impact, Low Effort)

**Problem**: Every SHAKE call inside ML-KEM goes through 3 EVP API calls (Init + Update + Squeeze), each involving provider dispatch and validation.

**Proposal**: Replace internal SHAKE calls with direct invocations of the `KECCAK1600_CTX` API (already available as `internal/sha3.h`). This avoids the EVP dispatch overhead while keeping the same Keccak implementation. The EVP API remains the public-facing interface; only the internal ML-KEM/ML-DSA code changes.

This is architecturally safe: OpenSSL's own ML-KEM code already `#include`s `"internal/sha3.h"` and has access to the `SHA3_BLOCKSIZE` macro. The change would replace:

```c
EVP_DigestInit_ex(mdctx, key->shake128_md, NULL);
EVP_DigestUpdate(mdctx, input, sizeof(input));
/* ... squeeze loop ... */
```

with direct `KECCAK1600_CTX` calls, removing the provider lookup per-call.

**Expected gain**: 5–10% overall (removes ~1–2 µs per operation from dispatch overhead).


#### 4. Add AVX2 NTT for ML-DSA (Medium Impact, Medium Effort)

**Problem**: ML-DSA NTT uses 32-bit Montgomery reduction in scalar C. AVX2 can process 8 × 32-bit values per instruction.

**Proposal**: Same approach as ML-KEM NTT, but using `vpmulld` / `vpmuludq` for 32-bit Montgomery multiplication. The CRYSTALS-Dilithium AVX2 reference implementation provides a ready template.

**Expected gain**: 1.2–1.4× for ML-DSA keygen/verify (NTT + polynomial multiply is ~25% of total).


#### 5. AVX2 Rejection Sampling with Lookup Tables (Low-Medium Impact, Low Effort)

**Problem**: Rejection sampling in `sample_scalar()` (ML-KEM) and `rej_ntt_poly()` (ML-DSA) processes one candidate at a time in a scalar loop.

**Proposal**: Use AVX2 comparison and shuffle instructions with a 256-entry lookup table to process 16 candidates per cycle, as done in AWS-LC's `rej_uniform_asm.S`. This is a self-contained optimization that doesn't change the algorithm.

**Expected gain**: 5–10% for keygen (rejection sampling is ~10% of keygen profile).


### Summary: Contribution of Each Factor

| Factor                        | Estimated contribution | Difficulty |
| ----------------------------- | ---------------------: | ---------- |
| AVX2 NTT + polynomial math   |              1.3–1.5×  | Medium     |
| 4-way batched Keccak          |              1.3–1.5×  | Medium     |
| Direct Keccak (bypass EVP)    |              1.05–1.1× | Low        |
| AVX2 rejection sampling       |              1.05–1.1× | Low        |
| **Combined theoretical**      |          **2.0–2.5×**  | —          |
| **Observed gap (ML-KEM-768)** |          **1.6–2.1×**  | —          |
| **Observed gap (ML-DSA-65)**  |          **2.4–3.9×**  | —          |

The combined impact of proposals 1–5 would close the majority of the performance gap between OpenSSL and AWS-LC, particularly for ML-KEM. The ML-DSA gap is larger because signing involves a rejection loop with many SHAKE calls per attempt, amplifying both the EVP overhead and the lack of x4 batching.

