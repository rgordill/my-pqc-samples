#include "../../common/bench_common.h"

#define IPPCP_PREVIEW_ML_KEM
#define IPPCP_PREVIEW_ML_DSA
#include <ippcp.h>
#include <ippcpversion.h>

#define XSTR(x) #x
#define STR(x) XSTR(x)
#define IPPCRYPTO_VER_STRING \
    STR(CRYPTO_LIB_VERSION_MAJOR) "." STR(CRYPTO_LIB_VERSION_MINOR) "." STR(CRYPTO_LIB_VERSION_PATCH)

#include <cstdio>
#include <vector>

static IppsMLKEMParamSet kem_paramset(bench::Algorithm algo) {
    switch (algo) {
        case bench::Algorithm::ML_KEM_512:  return IPPCP_ML_KEM_512;
        case bench::Algorithm::ML_KEM_768:  return IPPCP_ML_KEM_768;
        case bench::Algorithm::ML_KEM_1024: return IPPCP_ML_KEM_1024;
        default: return static_cast<IppsMLKEMParamSet>(0);
    }
}

static IppsMLDSAParamSet dsa_paramset(bench::Algorithm algo) {
    switch (algo) {
        case bench::Algorithm::ML_DSA_44: return ML_DSA_44;
        case bench::Algorithm::ML_DSA_65: return ML_DSA_65;
        case bench::Algorithm::ML_DSA_87: return ML_DSA_87;
        default: return static_cast<IppsMLDSAParamSet>(0);
    }
}

static bool is_supported(bench::Algorithm algo) {
    switch (algo) {
        case bench::Algorithm::ML_KEM_512:
        case bench::Algorithm::ML_KEM_768:
        case bench::Algorithm::ML_KEM_1024:
        case bench::Algorithm::ML_DSA_44:
        case bench::Algorithm::ML_DSA_65:
        case bench::Algorithm::ML_DSA_87:
            return true;
        default:
            return false;
    }
}

struct KemState {
    std::vector<Ipp8u> state_buf;
    IppsMLKEMState* ctx = nullptr;
    std::vector<Ipp8u> enc_key;
    std::vector<Ipp8u> dec_key;
    std::vector<Ipp8u> ciphertext;
    std::vector<Ipp8u> shared_secret;
    std::vector<Ipp8u> scratch_keygen;
    std::vector<Ipp8u> scratch_encaps;
    std::vector<Ipp8u> scratch_decaps;
};

struct DsaState {
    std::vector<Ipp8u> state_buf;
    IppsMLDSAState* ctx = nullptr;
    std::vector<Ipp8u> public_key;
    std::vector<Ipp8u> private_key;
    std::vector<Ipp8u> signature;
    std::vector<Ipp8u> message;
    std::vector<Ipp8u> scratch_keygen;
    std::vector<Ipp8u> scratch_sign;
    std::vector<Ipp8u> scratch_verify;
};

struct ThreadState {
    bench::Algorithm algorithm;
    bench::Operation operation;
    KemState kem;
    DsaState dsa;
};

static std::vector<ThreadState> g_states;

static bool setup_kem(KemState& ks, bench::Algorithm algo, bench::Operation op) {
    auto ps = kem_paramset(algo);

    int state_size = 0;
    if (ippsMLKEM_GetSize(&state_size, ps) != ippStsNoErr) return false;
    ks.state_buf.resize(state_size);
    ks.ctx = reinterpret_cast<IppsMLKEMState*>(ks.state_buf.data());
    if (ippsMLKEM_Init(ks.ctx, ps) != ippStsNoErr) return false;

    IppsMLKEMInfo info;
    if (ippsMLKEM_GetInfo(&info, ps) != ippStsNoErr) return false;
    ks.enc_key.resize(info.encapsKeySize);
    ks.dec_key.resize(info.decapsKeySize);
    ks.ciphertext.resize(info.cipherTextSize);
    ks.shared_secret.resize(info.sharedSecretSize);

    int buf_sz = 0;
    ippsMLKEM_KeyGenBufferGetSize(&buf_sz, ks.ctx);
    ks.scratch_keygen.resize(buf_sz);

    if (ippsMLKEM_KeyGen(ks.enc_key.data(), ks.dec_key.data(), ks.ctx,
                         ks.scratch_keygen.data(), nullptr, nullptr) != ippStsNoErr)
        return false;

    ippsMLKEM_EncapsBufferGetSize(&buf_sz, ks.ctx);
    ks.scratch_encaps.resize(buf_sz);

    if (op == bench::Operation::ENCAPS || op == bench::Operation::DECAPS) {
        if (ippsMLKEM_Encaps(ks.enc_key.data(), ks.ciphertext.data(),
                             ks.shared_secret.data(), ks.ctx,
                             ks.scratch_encaps.data(), nullptr, nullptr) != ippStsNoErr)
            return false;
    }

    ippsMLKEM_DecapsBufferGetSize(&buf_sz, ks.ctx);
    ks.scratch_decaps.resize(buf_sz);
    return true;
}

static bool setup_dsa(DsaState& ds, bench::Algorithm algo, bench::Operation op) {
    auto ps = dsa_paramset(algo);
    static const Ipp8u msg[] = "PQC benchmark test message for ML-DSA signing";
    int msg_len = sizeof(msg) - 1;

    int state_size = 0;
    if (ippsMLDSA_GetSize(&state_size) != ippStsNoErr) return false;
    ds.state_buf.resize(state_size);
    ds.ctx = reinterpret_cast<IppsMLDSAState*>(ds.state_buf.data());
    if (ippsMLDSA_Init(ds.ctx, msg_len, ps) != ippStsNoErr) return false;

    IppsMLDSAInfo info;
    if (ippsMLDSA_GetInfo(&info, ps) != ippStsNoErr) return false;
    ds.public_key.resize(info.publicKeySize);
    ds.private_key.resize(info.privateKeySize);
    ds.signature.resize(info.signatureSize);
    ds.message.assign(msg, msg + msg_len);

    int buf_sz = 0;
    ippsMLDSA_KeyGenBufferGetSize(&buf_sz, ds.ctx);
    ds.scratch_keygen.resize(buf_sz);
    ippsMLDSA_SignBufferGetSize(&buf_sz, ds.ctx);
    ds.scratch_sign.resize(buf_sz);
    ippsMLDSA_VerifyBufferGetSize(&buf_sz, ds.ctx);
    ds.scratch_verify.resize(buf_sz);

    if (ippsMLDSA_KeyGen(ds.public_key.data(), ds.private_key.data(), ds.ctx,
                         ds.scratch_keygen.data(), nullptr, nullptr) != ippStsNoErr)
        return false;

    if (op == bench::Operation::SIGN || op == bench::Operation::VERIFY) {
        if (ippsMLDSA_Sign(ds.message.data(), static_cast<Ipp32s>(ds.message.size()),
                           nullptr, 0,
                           ds.private_key.data(), ds.signature.data(), ds.ctx,
                           ds.scratch_sign.data(), nullptr, nullptr) != ippStsNoErr)
            return false;
    }
    return true;
}

static bool setup_thread(int tid) {
    auto& st = g_states[tid];
    if (bench::is_kem_algorithm(st.algorithm))
        return setup_kem(st.kem, st.algorithm, st.operation);
    else
        return setup_dsa(st.dsa, st.algorithm, st.operation);
}

static void bench_thread(int tid) {
    auto& st = g_states[tid];

    if (bench::is_kem_algorithm(st.algorithm)) {
        auto& ks = st.kem;
        switch (st.operation) {
            case bench::Operation::KEYGEN:
                ippsMLKEM_KeyGen(ks.enc_key.data(), ks.dec_key.data(), ks.ctx,
                                 ks.scratch_keygen.data(), nullptr, nullptr);
                break;
            case bench::Operation::ENCAPS:
                ippsMLKEM_Encaps(ks.enc_key.data(), ks.ciphertext.data(),
                                 ks.shared_secret.data(), ks.ctx,
                                 ks.scratch_encaps.data(), nullptr, nullptr);
                break;
            case bench::Operation::DECAPS:
                ippsMLKEM_Decaps(ks.dec_key.data(), ks.ciphertext.data(),
                                 ks.shared_secret.data(), ks.ctx,
                                 ks.scratch_decaps.data());
                break;
            default: break;
        }
    } else {
        auto& ds = st.dsa;
        switch (st.operation) {
            case bench::Operation::KEYGEN:
                ippsMLDSA_KeyGen(ds.public_key.data(), ds.private_key.data(), ds.ctx,
                                 ds.scratch_keygen.data(), nullptr, nullptr);
                break;
            case bench::Operation::SIGN:
                ippsMLDSA_Sign(ds.message.data(), static_cast<Ipp32s>(ds.message.size()),
                               nullptr, 0,
                               ds.private_key.data(), ds.signature.data(), ds.ctx,
                               ds.scratch_sign.data(), nullptr, nullptr);
                break;
            case bench::Operation::VERIFY: {
                int valid = 0;
                ippsMLDSA_Verify(ds.message.data(), static_cast<Ipp32s>(ds.message.size()),
                                 nullptr, 0,
                                 ds.public_key.data(), ds.signature.data(), &valid,
                                 ds.ctx, ds.scratch_verify.data());
                break;
            }
            default: break;
        }
    }
}

static void teardown_thread(int) {}

int main(int argc, char* argv[]) {
    bench::Config cfg = bench::parse_args(argc, argv);

    bench::LibraryInfo info;
    info.name = "IPP Crypto";
    info.version = IPPCRYPTO_VER_STRING;
    info.build_info = "Intel Cryptography Primitives Library (shared)";

    if (!is_supported(cfg.algorithm)) {
        fprintf(stderr, "ERROR: Algorithm %s not supported by IPP Crypto backend\n",
                bench::algorithm_name(cfg.algorithm));
        return 2;
    }

    if (cfg.threads > 1 && bench::is_kem_algorithm(cfg.algorithm)
        && cfg.operation == bench::Operation::DECAPS) {
        fprintf(stderr, "SKIP: ML-KEM decaps with threads>1 triggers a stack-smashing bug "
                        "in IPP Crypto %s (single-thread works correctly)\n", IPPCRYPTO_VER_STRING);
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
