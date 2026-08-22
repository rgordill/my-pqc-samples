#include "../../common/bench_common.h"

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <cstdio>
#include <cstring>
#include <vector>

static const char* ossl_algo_name(bench::Algorithm algo) {
    switch (algo) {
        case bench::Algorithm::ML_KEM_512:  return "ML-KEM-512";
        case bench::Algorithm::ML_KEM_768:  return "ML-KEM-768";
        case bench::Algorithm::ML_KEM_1024: return "ML-KEM-1024";
        case bench::Algorithm::ML_DSA_44:   return "ML-DSA-44";
        case bench::Algorithm::ML_DSA_65:   return "ML-DSA-65";
        case bench::Algorithm::ML_DSA_87:   return "ML-DSA-87";
        case bench::Algorithm::SLH_DSA_SHA2_128s:  return "SLH-DSA-SHA2-128s";
        case bench::Algorithm::SLH_DSA_SHA2_128f:  return "SLH-DSA-SHA2-128f";
        case bench::Algorithm::SLH_DSA_SHA2_192s:  return "SLH-DSA-SHA2-192s";
        case bench::Algorithm::SLH_DSA_SHA2_192f:  return "SLH-DSA-SHA2-192f";
        case bench::Algorithm::SLH_DSA_SHA2_256s:  return "SLH-DSA-SHA2-256s";
        case bench::Algorithm::SLH_DSA_SHA2_256f:  return "SLH-DSA-SHA2-256f";
        case bench::Algorithm::SLH_DSA_SHAKE_128s: return "SLH-DSA-SHAKE-128s";
        case bench::Algorithm::SLH_DSA_SHAKE_128f: return "SLH-DSA-SHAKE-128f";
        case bench::Algorithm::SLH_DSA_SHAKE_192s: return "SLH-DSA-SHAKE-192s";
        case bench::Algorithm::SLH_DSA_SHAKE_192f: return "SLH-DSA-SHAKE-192f";
        case bench::Algorithm::SLH_DSA_SHAKE_256s: return "SLH-DSA-SHAKE-256s";
        case bench::Algorithm::SLH_DSA_SHAKE_256f: return "SLH-DSA-SHAKE-256f";
    }
    return nullptr;
}

// Per-thread state: avoids any shared mutable data
struct ThreadState {
    EVP_PKEY* key = nullptr;
    EVP_PKEY* peer_key = nullptr;  // for encaps (public key extracted from peer)
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> shared_secret;
    std::vector<uint8_t> signature;
    std::vector<uint8_t> message;
    bench::Algorithm algorithm;
    bench::Operation operation;
};

static std::vector<ThreadState> g_states;

static EVP_PKEY* generate_key(const char* alg_name) {
    EVP_PKEY* key = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, alg_name, nullptr);
    if (!ctx) return nullptr;
    if (EVP_PKEY_keygen_init(ctx) <= 0) { EVP_PKEY_CTX_free(ctx); return nullptr; }
    if (EVP_PKEY_keygen(ctx, &key) <= 0) { EVP_PKEY_CTX_free(ctx); return nullptr; }
    EVP_PKEY_CTX_free(ctx);
    return key;
}

static bool setup_thread(int tid) {
    auto& st = g_states[tid];
    const char* alg_name = ossl_algo_name(st.algorithm);

    if (st.operation == bench::Operation::KEYGEN) {
        return true;  // keygen benchmark generates keys in the hot loop
    }

    st.key = generate_key(alg_name);
    if (!st.key) {
        fprintf(stderr, "Thread %d: keygen failed for %s\n", tid, alg_name);
        ERR_print_errors_fp(stderr);
        return false;
    }

    if (bench::is_kem_algorithm(st.algorithm)) {
        if (st.operation == bench::Operation::ENCAPS || st.operation == bench::Operation::DECAPS) {
            // For encaps we need a public key context; for decaps we need ciphertext
            // Generate a peer key and do one encaps to get ciphertext
            st.peer_key = generate_key(alg_name);
            if (!st.peer_key) return false;

            EVP_PKEY_CTX* enc_ctx = EVP_PKEY_CTX_new_from_pkey(nullptr, st.peer_key, nullptr);
            if (!enc_ctx) return false;
            if (EVP_PKEY_encapsulate_init(enc_ctx, nullptr) <= 0) {
                EVP_PKEY_CTX_free(enc_ctx);
                return false;
            }
            size_t ct_len = 0, ss_len = 0;
            EVP_PKEY_encapsulate(enc_ctx, nullptr, &ct_len, nullptr, &ss_len);
            st.ciphertext.resize(ct_len);
            st.shared_secret.resize(ss_len);
            if (EVP_PKEY_encapsulate(enc_ctx, st.ciphertext.data(), &ct_len,
                                     st.shared_secret.data(), &ss_len) <= 0) {
                EVP_PKEY_CTX_free(enc_ctx);
                return false;
            }
            EVP_PKEY_CTX_free(enc_ctx);
        }
    } else {
        // DSA: prepare message and signature for verify
        st.message.resize(64);
        RAND_bytes(st.message.data(), st.message.size());

        if (st.operation == bench::Operation::VERIFY) {
            EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
            if (EVP_DigestSignInit_ex(md_ctx, nullptr, nullptr, nullptr, nullptr, st.key, nullptr) <= 0) {
                EVP_MD_CTX_free(md_ctx);
                return false;
            }
            size_t sig_len = 0;
            EVP_DigestSign(md_ctx, nullptr, &sig_len, st.message.data(), st.message.size());
            st.signature.resize(sig_len);
            if (EVP_DigestSign(md_ctx, st.signature.data(), &sig_len,
                              st.message.data(), st.message.size()) <= 0) {
                EVP_MD_CTX_free(md_ctx);
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
    const char* alg_name = ossl_algo_name(st.algorithm);

    switch (st.operation) {
        case bench::Operation::KEYGEN: {
            EVP_PKEY* k = generate_key(alg_name);
            if (k) EVP_PKEY_free(k);
            break;
        }
        case bench::Operation::ENCAPS: {
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_pkey(nullptr, st.peer_key, nullptr);
            EVP_PKEY_encapsulate_init(ctx, nullptr);
            size_t ct_len = st.ciphertext.size(), ss_len = st.shared_secret.size();
            EVP_PKEY_encapsulate(ctx, st.ciphertext.data(), &ct_len,
                                st.shared_secret.data(), &ss_len);
            EVP_PKEY_CTX_free(ctx);
            break;
        }
        case bench::Operation::DECAPS: {
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_pkey(nullptr, st.key, nullptr);
            EVP_PKEY_decapsulate_init(ctx, nullptr);
            size_t ss_len = st.shared_secret.size();
            EVP_PKEY_decapsulate(ctx, st.shared_secret.data(), &ss_len,
                                st.ciphertext.data(), st.ciphertext.size());
            EVP_PKEY_CTX_free(ctx);
            break;
        }
        case bench::Operation::SIGN: {
            EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
            EVP_DigestSignInit_ex(md_ctx, nullptr, nullptr, nullptr, nullptr, st.key, nullptr);
            size_t sig_len = st.signature.size();
            if (sig_len == 0) {
                EVP_DigestSign(md_ctx, nullptr, &sig_len, st.message.data(), st.message.size());
                st.signature.resize(sig_len);
            }
            EVP_DigestSign(md_ctx, st.signature.data(), &sig_len,
                          st.message.data(), st.message.size());
            EVP_MD_CTX_free(md_ctx);
            break;
        }
        case bench::Operation::VERIFY: {
            EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
            EVP_DigestVerifyInit_ex(md_ctx, nullptr, nullptr, nullptr, nullptr, st.key, nullptr);
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
    if (st.peer_key) { EVP_PKEY_free(st.peer_key); st.peer_key = nullptr; }
}

int main(int argc, char* argv[]) {
    bench::Config cfg = bench::parse_args(argc, argv);

    bench::LibraryInfo info;
    info.name = "OpenSSL";
    info.version = OpenSSL_version(OPENSSL_VERSION_STRING);
    info.build_info = OpenSSL_version(OPENSSL_PLATFORM);

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
