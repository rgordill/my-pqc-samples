#include "../../common/bench_common.h"

#include <openssl/mlkem.h>
#include <openssl/mldsa.h>
#include <openssl/slhdsa.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>

struct ThreadState {
    struct MLKEM768_private_key kem_priv;
    struct MLKEM768_public_key kem_pub;
    uint8_t kem_ciphertext[MLKEM768_CIPHERTEXT_BYTES];
    uint8_t kem_shared_secret[MLKEM_SHARED_SECRET_BYTES];
    uint8_t kem_encoded_pubkey[MLKEM768_PUBLIC_KEY_BYTES];

    struct MLDSA65_private_key dsa_priv;
    struct MLDSA65_public_key dsa_pub;
    uint8_t dsa_encoded_pubkey[MLDSA65_PUBLIC_KEY_BYTES];
    uint8_t dsa_seed[MLDSA_SEED_BYTES];
    uint8_t dsa_signature[MLDSA65_SIGNATURE_BYTES];

    uint8_t slh_sha2_128s_pub[SLHDSA_SHA2_128S_PUBLIC_KEY_BYTES];
    uint8_t slh_sha2_128s_priv[SLHDSA_SHA2_128S_PRIVATE_KEY_BYTES];
    uint8_t slh_sha2_128s_sig[SLHDSA_SHA2_128S_SIGNATURE_BYTES];

    uint8_t slh_shake_256f_pub[SLHDSA_SHAKE_256F_PUBLIC_KEY_BYTES];
    uint8_t slh_shake_256f_priv[SLHDSA_SHAKE_256F_PRIVATE_KEY_BYTES];
    uint8_t slh_shake_256f_sig[SLHDSA_SHAKE_256F_SIGNATURE_BYTES];

    std::vector<uint8_t> message;

    bench::Algorithm algorithm;
    bench::Operation operation;
};

static std::vector<ThreadState> g_states;

static bool is_supported(bench::Algorithm algo) {
    return algo == bench::Algorithm::ML_KEM_768 ||
           algo == bench::Algorithm::ML_DSA_65 ||
           algo == bench::Algorithm::SLH_DSA_SHA2_128s ||
           algo == bench::Algorithm::SLH_DSA_SHAKE_256f;
}

static bool setup_thread(int tid) {
    auto& st = g_states[tid];

    if (st.operation == bench::Operation::KEYGEN) return true;

    if (st.algorithm == bench::Algorithm::ML_KEM_768) {
        MLKEM768_generate_key(st.kem_encoded_pubkey, nullptr, &st.kem_priv);
        MLKEM768_public_from_private(&st.kem_pub, &st.kem_priv);

        if (st.operation == bench::Operation::DECAPS) {
            MLKEM768_encap(st.kem_ciphertext, st.kem_shared_secret, &st.kem_pub);
        }
    } else if (st.algorithm == bench::Algorithm::ML_DSA_65) {
        st.message.resize(64);
        RAND_bytes(st.message.data(), st.message.size());

        if (!MLDSA65_generate_key(st.dsa_encoded_pubkey, st.dsa_seed, &st.dsa_priv)) {
            fprintf(stderr, "Thread %d: ML-DSA-65 keygen failed\n", tid);
            return false;
        }
        if (!MLDSA65_public_from_private(&st.dsa_pub, &st.dsa_priv)) {
            fprintf(stderr, "Thread %d: ML-DSA-65 public_from_private failed\n", tid);
            return false;
        }

        if (st.operation == bench::Operation::VERIFY) {
            if (!MLDSA65_sign(st.dsa_signature, &st.dsa_priv,
                              st.message.data(), st.message.size(), nullptr, 0)) {
                fprintf(stderr, "Thread %d: ML-DSA-65 sign failed in setup\n", tid);
                return false;
            }
        }
    } else if (st.algorithm == bench::Algorithm::SLH_DSA_SHA2_128s) {
        st.message.resize(64);
        RAND_bytes(st.message.data(), st.message.size());
        SLHDSA_SHA2_128S_generate_key(st.slh_sha2_128s_pub, st.slh_sha2_128s_priv);
        if (st.operation == bench::Operation::VERIFY) {
            if (!SLHDSA_SHA2_128S_sign(st.slh_sha2_128s_sig, st.slh_sha2_128s_priv,
                                       st.message.data(), st.message.size(), nullptr, 0)) {
                fprintf(stderr, "Thread %d: SLH-DSA-SHA2-128s sign failed in setup\n", tid);
                return false;
            }
        }
    } else if (st.algorithm == bench::Algorithm::SLH_DSA_SHAKE_256f) {
        st.message.resize(64);
        RAND_bytes(st.message.data(), st.message.size());
        SLHDSA_SHAKE_256F_generate_key(st.slh_shake_256f_pub, st.slh_shake_256f_priv);
        if (st.operation == bench::Operation::VERIFY) {
            if (!SLHDSA_SHAKE_256F_sign(st.slh_shake_256f_sig, st.slh_shake_256f_priv,
                                        st.message.data(), st.message.size(), nullptr, 0)) {
                fprintf(stderr, "Thread %d: SLH-DSA-SHAKE-256f sign failed in setup\n", tid);
                return false;
            }
        }
    }
    return true;
}

static void bench_thread(int tid) {
    auto& st = g_states[tid];

    if (st.algorithm == bench::Algorithm::SLH_DSA_SHA2_128s) {
        switch (st.operation) {
            case bench::Operation::KEYGEN: {
                uint8_t pub[SLHDSA_SHA2_128S_PUBLIC_KEY_BYTES];
                uint8_t priv[SLHDSA_SHA2_128S_PRIVATE_KEY_BYTES];
                SLHDSA_SHA2_128S_generate_key(pub, priv);
                break;
            }
            case bench::Operation::SIGN: {
                uint8_t sig[SLHDSA_SHA2_128S_SIGNATURE_BYTES];
                SLHDSA_SHA2_128S_sign(sig, st.slh_sha2_128s_priv,
                                      st.message.data(), st.message.size(), nullptr, 0);
                break;
            }
            case bench::Operation::VERIFY:
                SLHDSA_SHA2_128S_verify(st.slh_sha2_128s_sig, SLHDSA_SHA2_128S_SIGNATURE_BYTES,
                                        st.slh_sha2_128s_pub,
                                        st.message.data(), st.message.size(), nullptr, 0);
                break;
            default:
                break;
        }
        return;
    }

    if (st.algorithm == bench::Algorithm::SLH_DSA_SHAKE_256f) {
        switch (st.operation) {
            case bench::Operation::KEYGEN: {
                uint8_t pub[SLHDSA_SHAKE_256F_PUBLIC_KEY_BYTES];
                uint8_t priv[SLHDSA_SHAKE_256F_PRIVATE_KEY_BYTES];
                SLHDSA_SHAKE_256F_generate_key(pub, priv);
                break;
            }
            case bench::Operation::SIGN: {
                uint8_t sig[SLHDSA_SHAKE_256F_SIGNATURE_BYTES];
                SLHDSA_SHAKE_256F_sign(sig, st.slh_shake_256f_priv,
                                       st.message.data(), st.message.size(), nullptr, 0);
                break;
            }
            case bench::Operation::VERIFY:
                SLHDSA_SHAKE_256F_verify(st.slh_shake_256f_sig, SLHDSA_SHAKE_256F_SIGNATURE_BYTES,
                                         st.slh_shake_256f_pub,
                                         st.message.data(), st.message.size(), nullptr, 0);
                break;
            default:
                break;
        }
        return;
    }

    switch (st.operation) {
        case bench::Operation::KEYGEN: {
            if (st.algorithm == bench::Algorithm::ML_KEM_768) {
                uint8_t pub[MLKEM768_PUBLIC_KEY_BYTES];
                struct MLKEM768_private_key priv;
                MLKEM768_generate_key(pub, nullptr, &priv);
            } else {
                uint8_t pub[MLDSA65_PUBLIC_KEY_BYTES];
                uint8_t seed[MLDSA_SEED_BYTES];
                struct MLDSA65_private_key priv;
                MLDSA65_generate_key(pub, seed, &priv);
            }
            break;
        }
        case bench::Operation::ENCAPS: {
            uint8_t ct[MLKEM768_CIPHERTEXT_BYTES];
            uint8_t ss[MLKEM_SHARED_SECRET_BYTES];
            MLKEM768_encap(ct, ss, &st.kem_pub);
            break;
        }
        case bench::Operation::DECAPS: {
            uint8_t ss[MLKEM_SHARED_SECRET_BYTES];
            MLKEM768_decap(ss, st.kem_ciphertext, MLKEM768_CIPHERTEXT_BYTES,
                           &st.kem_priv);
            break;
        }
        case bench::Operation::SIGN: {
            uint8_t sig[MLDSA65_SIGNATURE_BYTES];
            MLDSA65_sign(sig, &st.dsa_priv,
                         st.message.data(), st.message.size(), nullptr, 0);
            break;
        }
        case bench::Operation::VERIFY: {
            MLDSA65_verify(&st.dsa_pub, st.dsa_signature, MLDSA65_SIGNATURE_BYTES,
                           st.message.data(), st.message.size(), nullptr, 0);
            break;
        }
    }
}

static void teardown_thread(int /*tid*/) {
}

int main(int argc, char* argv[]) {
    bench::Config cfg = bench::parse_args(argc, argv);

    if (!is_supported(cfg.algorithm)) {
        fprintf(stderr, "Error: BoringSSL backend only supports ML-KEM-768, ML-DSA-65, SLH-DSA-SHA2-128s, SLH-DSA-SHAKE-256f\n");
        return 2;
    }

    bench::LibraryInfo info;
    info.name = "BoringSSL";
#ifdef BENCH_BORINGSSL_VERSION
    info.version = BENCH_BORINGSSL_VERSION;
#else
    info.version = SSLeay_version(0);
#endif
    info.build_info = "shared library build from source";

    bench::print_banner(info, cfg);

    g_states.resize(cfg.threads);
    for (auto& st : g_states) {
        st.algorithm = cfg.algorithm;
        st.operation = cfg.operation;
    }

    auto results = bench::run_benchmark(cfg, setup_thread, bench_thread, teardown_thread);
    bench::print_results(results, cfg, info);

    return 0;
}
