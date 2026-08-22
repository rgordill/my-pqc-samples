#include "../../common/bench_common.h"

#include <gnutls/gnutls.h>
#include <gnutls/abstract.h>
#include <gnutls/crypto.h>

#include <cstdio>
#include <cstring>
#include <vector>

static gnutls_pk_algorithm_t gnutls_pk_for_algo(bench::Algorithm algo) {
    switch (algo) {
        case bench::Algorithm::ML_KEM_512:  return static_cast<gnutls_pk_algorithm_t>(0);
        case bench::Algorithm::ML_KEM_768:  return GNUTLS_PK_MLKEM768;
        case bench::Algorithm::ML_KEM_1024: return GNUTLS_PK_MLKEM1024;
        case bench::Algorithm::ML_DSA_44:   return GNUTLS_PK_MLDSA44;
        case bench::Algorithm::ML_DSA_65:   return GNUTLS_PK_MLDSA65;
        case bench::Algorithm::ML_DSA_87:   return GNUTLS_PK_MLDSA87;
        default:                            return static_cast<gnutls_pk_algorithm_t>(0);
    }
}

struct ThreadState {
    gnutls_privkey_t privkey = nullptr;
    gnutls_pubkey_t pubkey = nullptr;
    gnutls_datum_t signature = {nullptr, 0};
    gnutls_datum_t message = {nullptr, 0};
    bench::Algorithm algorithm;
    bench::Operation operation;
};

static std::vector<ThreadState> g_states;

static bool generate_keypair(bench::Algorithm algo, gnutls_privkey_t* priv, gnutls_pubkey_t* pub) {
    gnutls_pk_algorithm_t pk = gnutls_pk_for_algo(algo);
    if (pk == 0) return false;

    int ret = gnutls_privkey_init(priv);
    if (ret < 0) return false;

    ret = gnutls_privkey_generate(*priv, pk, 0, 0);
    if (ret < 0) {
        gnutls_privkey_deinit(*priv);
        *priv = nullptr;
        return false;
    }

    if (pub) {
        ret = gnutls_pubkey_init(pub);
        if (ret < 0) return false;
        ret = gnutls_pubkey_import_privkey(*pub, *priv, 0, 0);
        if (ret < 0) {
            gnutls_pubkey_deinit(*pub);
            *pub = nullptr;
            return false;
        }
    }
    return true;
}

static bool setup_thread(int tid) {
    auto& st = g_states[tid];
    gnutls_pk_algorithm_t pk = gnutls_pk_for_algo(st.algorithm);

    if (pk == 0) {
        fprintf(stderr, "Thread %d: algorithm %s not supported by GnuTLS\n",
                tid, bench::algorithm_name(st.algorithm));
        return false;
    }

    if (st.operation == bench::Operation::KEYGEN) return true;

    if (!generate_keypair(st.algorithm, &st.privkey, &st.pubkey)) {
        fprintf(stderr, "Thread %d: keygen failed for %s\n",
                tid, bench::algorithm_name(st.algorithm));
        return false;
    }

    st.message.data = static_cast<unsigned char*>(gnutls_malloc(64));
    st.message.size = 64;
    gnutls_rnd(GNUTLS_RND_NONCE, st.message.data, st.message.size);

    if (st.operation == bench::Operation::VERIFY) {
        gnutls_datum_t sig = {nullptr, 0};
        int ret = gnutls_privkey_sign_data(st.privkey, GNUTLS_DIG_SHA256, 0,
                                          &st.message, &sig);
        if (ret < 0) {
            fprintf(stderr, "Thread %d: initial sign failed: %s\n", tid, gnutls_strerror(ret));
            return false;
        }
        st.signature = sig;
    }

    return true;
}

static void bench_thread(int tid) {
    auto& st = g_states[tid];

    switch (st.operation) {
        case bench::Operation::KEYGEN: {
            gnutls_privkey_t priv = nullptr;
            if (generate_keypair(st.algorithm, &priv, nullptr)) {
                gnutls_privkey_deinit(priv);
            }
            break;
        }
        case bench::Operation::SIGN: {
            gnutls_datum_t sig = {nullptr, 0};
            gnutls_privkey_sign_data(st.privkey, GNUTLS_DIG_SHA256, 0,
                                    &st.message, &sig);
            gnutls_free(sig.data);
            break;
        }
        case bench::Operation::VERIFY: {
            gnutls_pubkey_verify_data2(st.pubkey, GNUTLS_SIGN_UNKNOWN, 0,
                                      &st.message, &st.signature);
            break;
        }
        case bench::Operation::ENCAPS:
        case bench::Operation::DECAPS:
            break;
    }
}

static void teardown_thread(int tid) {
    auto& st = g_states[tid];
    if (st.privkey) { gnutls_privkey_deinit(st.privkey); st.privkey = nullptr; }
    if (st.pubkey) { gnutls_pubkey_deinit(st.pubkey); st.pubkey = nullptr; }
    if (st.signature.data) { gnutls_free(st.signature.data); st.signature.data = nullptr; }
    if (st.message.data) { gnutls_free(st.message.data); st.message.data = nullptr; }
}

int main(int argc, char* argv[]) {
    bench::Config cfg = bench::parse_args(argc, argv);

    gnutls_global_init();

    bench::LibraryInfo info;
    info.name = "GnuTLS";
    info.version = gnutls_check_version(nullptr);
    info.build_info = "";

    gnutls_pk_algorithm_t pk = gnutls_pk_for_algo(cfg.algorithm);
    if (pk == 0) {
        fprintf(stderr, "ERROR: Algorithm %s is not supported by this GnuTLS build\n",
                bench::algorithm_name(cfg.algorithm));
        gnutls_global_deinit();
        return 2;
    }

    if (bench::is_kem_algorithm(cfg.algorithm) &&
        (cfg.operation == bench::Operation::ENCAPS || cfg.operation == bench::Operation::DECAPS)) {
        fprintf(stderr, "ERROR: ML-KEM encaps/decaps not supported via GnuTLS public API "
                "(only available internally for TLS key exchange)\n");
        gnutls_global_deinit();
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

    gnutls_global_deinit();
    return 0;
}
