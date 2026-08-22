#include "../../common/bench_common.h"

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/base.h>
#include <openssl/rand.h>
#include <openssl/nid.h>

#include <cstdio>
#include <cstring>
#include <vector>

static int awslc_kem_nid(bench::Algorithm algo) {
    switch (algo) {
        case bench::Algorithm::ML_KEM_512:  return NID_MLKEM512;
        case bench::Algorithm::ML_KEM_768:  return NID_MLKEM768;
        case bench::Algorithm::ML_KEM_1024: return NID_MLKEM1024;
        default: return 0;
    }
}

static int awslc_dsa_nid(bench::Algorithm algo) {
    switch (algo) {
        case bench::Algorithm::ML_DSA_44: return NID_MLDSA44;
        case bench::Algorithm::ML_DSA_65: return NID_MLDSA65;
        case bench::Algorithm::ML_DSA_87: return NID_MLDSA87;
        default: return 0;
    }
}

struct ThreadState {
    EVP_PKEY* key = nullptr;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> shared_secret;
    std::vector<uint8_t> signature;
    std::vector<uint8_t> message;
    bench::Algorithm algorithm;
    bench::Operation operation;
};

static std::vector<ThreadState> g_states;

static EVP_PKEY* generate_kem_key(bench::Algorithm algo) {
    EVP_PKEY* key = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_KEM, nullptr);
    if (!ctx) return nullptr;
    if (EVP_PKEY_CTX_kem_set_params(ctx, awslc_kem_nid(algo)) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }
    if (EVP_PKEY_keygen_init(ctx) <= 0) { EVP_PKEY_CTX_free(ctx); return nullptr; }
    if (EVP_PKEY_keygen(ctx, &key) <= 0) { EVP_PKEY_CTX_free(ctx); return nullptr; }
    EVP_PKEY_CTX_free(ctx);
    return key;
}

static EVP_PKEY* generate_dsa_key(bench::Algorithm algo) {
    EVP_PKEY* key = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_PQDSA, nullptr);
    if (!ctx) return nullptr;
    if (EVP_PKEY_CTX_pqdsa_set_params(ctx, awslc_dsa_nid(algo)) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }
    if (EVP_PKEY_keygen_init(ctx) <= 0) { EVP_PKEY_CTX_free(ctx); return nullptr; }
    if (EVP_PKEY_keygen(ctx, &key) <= 0) { EVP_PKEY_CTX_free(ctx); return nullptr; }
    EVP_PKEY_CTX_free(ctx);
    return key;
}

static bool setup_thread(int tid) {
    auto& st = g_states[tid];

    if (st.operation == bench::Operation::KEYGEN) return true;

    if (bench::is_kem_algorithm(st.algorithm)) {
        st.key = generate_kem_key(st.algorithm);
        if (!st.key) {
            fprintf(stderr, "Thread %d: KEM keygen failed\n", tid);
            ERR_print_errors_fp(stderr);
            return false;
        }

        if (st.operation == bench::Operation::ENCAPS || st.operation == bench::Operation::DECAPS) {
            EVP_PKEY_CTX* enc_ctx = EVP_PKEY_CTX_new(st.key, nullptr);
            if (!enc_ctx) return false;
            if (EVP_PKEY_encapsulate(enc_ctx, nullptr, nullptr, nullptr, nullptr) <= 0) {
                // Query sizes
            }
            size_t ct_len = 0, ss_len = 0;
            EVP_PKEY_encapsulate(enc_ctx, nullptr, &ct_len, nullptr, &ss_len);
            st.ciphertext.resize(ct_len);
            st.shared_secret.resize(ss_len);
            if (EVP_PKEY_encapsulate(enc_ctx, st.ciphertext.data(), &ct_len,
                                     st.shared_secret.data(), &ss_len) <= 0) {
                fprintf(stderr, "Thread %d: encapsulate failed in setup\n", tid);
                ERR_print_errors_fp(stderr);
                EVP_PKEY_CTX_free(enc_ctx);
                return false;
            }
            EVP_PKEY_CTX_free(enc_ctx);
        }
    } else {
        st.key = generate_dsa_key(st.algorithm);
        if (!st.key) {
            fprintf(stderr, "Thread %d: DSA keygen failed\n", tid);
            ERR_print_errors_fp(stderr);
            return false;
        }

        st.message.resize(64);
        RAND_bytes(st.message.data(), st.message.size());

        if (st.operation == bench::Operation::VERIFY || st.operation == bench::Operation::SIGN) {
            EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
            if (!md_ctx) return false;
            if (EVP_DigestSignInit(md_ctx, nullptr, nullptr, nullptr, st.key) <= 0) {
                EVP_MD_CTX_free(md_ctx);
                fprintf(stderr, "Thread %d: DigestSignInit failed in setup\n", tid);
                ERR_print_errors_fp(stderr);
                return false;
            }
            size_t sig_len = 0;
            EVP_DigestSign(md_ctx, nullptr, &sig_len, st.message.data(), st.message.size());
            st.signature.resize(sig_len);
            if (EVP_DigestSign(md_ctx, st.signature.data(), &sig_len,
                              st.message.data(), st.message.size()) <= 0) {
                EVP_MD_CTX_free(md_ctx);
                fprintf(stderr, "Thread %d: DigestSign failed in setup\n", tid);
                ERR_print_errors_fp(stderr);
                return false;
            }
            st.signature.resize(sig_len);
            EVP_MD_CTX_free(md_ctx);
        }
    }
    return true;
}

static void bench_thread(int tid) {
    auto& st = g_states[tid];

    switch (st.operation) {
        case bench::Operation::KEYGEN: {
            EVP_PKEY* k;
            if (bench::is_kem_algorithm(st.algorithm)) {
                k = generate_kem_key(st.algorithm);
            } else {
                k = generate_dsa_key(st.algorithm);
            }
            if (k) EVP_PKEY_free(k);
            break;
        }
        case bench::Operation::ENCAPS: {
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(st.key, nullptr);
            size_t ct_len = st.ciphertext.size(), ss_len = st.shared_secret.size();
            EVP_PKEY_encapsulate(ctx, st.ciphertext.data(), &ct_len,
                                st.shared_secret.data(), &ss_len);
            EVP_PKEY_CTX_free(ctx);
            break;
        }
        case bench::Operation::DECAPS: {
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(st.key, nullptr);
            size_t ss_len = st.shared_secret.size();
            EVP_PKEY_decapsulate(ctx, st.shared_secret.data(), &ss_len,
                                st.ciphertext.data(), st.ciphertext.size());
            EVP_PKEY_CTX_free(ctx);
            break;
        }
        case bench::Operation::SIGN: {
            EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
            EVP_DigestSignInit(md_ctx, nullptr, nullptr, nullptr, st.key);
            size_t sig_len = st.signature.size();
            EVP_DigestSign(md_ctx, st.signature.data(), &sig_len,
                          st.message.data(), st.message.size());
            EVP_MD_CTX_free(md_ctx);
            break;
        }
        case bench::Operation::VERIFY: {
            EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
            EVP_DigestVerifyInit(md_ctx, nullptr, nullptr, nullptr, st.key);
            EVP_DigestVerify(md_ctx, st.signature.data(), st.signature.size(),
                           st.message.data(), st.message.size());
            EVP_MD_CTX_free(md_ctx);
            break;
        }
    }
}

static void teardown_thread(int tid) {
    auto& st = g_states[tid];
    if (st.key) { EVP_PKEY_free(st.key); st.key = nullptr; }
}

int main(int argc, char* argv[]) {
    bench::Config cfg = bench::parse_args(argc, argv);

    bench::LibraryInfo info;
    info.name = "AWS-LC";
    info.version = AWSLC_VERSION_NUMBER_STRING;
    info.build_info = "shared library build from source";

    if (!bench::is_kem_algorithm(cfg.algorithm) && !bench::is_dsa_algorithm(cfg.algorithm)) {
        fprintf(stderr, "ERROR: Unsupported algorithm %s\n", bench::algorithm_name(cfg.algorithm));
        return 2;
    }
    if (bench::is_dsa_algorithm(cfg.algorithm) && awslc_dsa_nid(cfg.algorithm) == 0) {
        fprintf(stderr, "ERROR: Algorithm %s is not supported by AWS-LC\n", bench::algorithm_name(cfg.algorithm));
        return 2;
    }

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
