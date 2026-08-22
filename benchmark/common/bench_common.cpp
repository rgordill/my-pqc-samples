#include "bench_common.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <numeric>
#include <time.h>

namespace bench {

static uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}

const char* algorithm_name(Algorithm algo) {
    switch (algo) {
        case Algorithm::ML_KEM_512:  return "ML-KEM-512";
        case Algorithm::ML_KEM_768:  return "ML-KEM-768";
        case Algorithm::ML_KEM_1024: return "ML-KEM-1024";
        case Algorithm::ML_DSA_44:   return "ML-DSA-44";
        case Algorithm::ML_DSA_65:   return "ML-DSA-65";
        case Algorithm::ML_DSA_87:   return "ML-DSA-87";
        case Algorithm::SLH_DSA_SHA2_128s:  return "SLH-DSA-SHA2-128s";
        case Algorithm::SLH_DSA_SHA2_128f:  return "SLH-DSA-SHA2-128f";
        case Algorithm::SLH_DSA_SHA2_192s:  return "SLH-DSA-SHA2-192s";
        case Algorithm::SLH_DSA_SHA2_192f:  return "SLH-DSA-SHA2-192f";
        case Algorithm::SLH_DSA_SHA2_256s:  return "SLH-DSA-SHA2-256s";
        case Algorithm::SLH_DSA_SHA2_256f:  return "SLH-DSA-SHA2-256f";
        case Algorithm::SLH_DSA_SHAKE_128s: return "SLH-DSA-SHAKE-128s";
        case Algorithm::SLH_DSA_SHAKE_128f: return "SLH-DSA-SHAKE-128f";
        case Algorithm::SLH_DSA_SHAKE_192s: return "SLH-DSA-SHAKE-192s";
        case Algorithm::SLH_DSA_SHAKE_192f: return "SLH-DSA-SHAKE-192f";
        case Algorithm::SLH_DSA_SHAKE_256s: return "SLH-DSA-SHAKE-256s";
        case Algorithm::SLH_DSA_SHAKE_256f: return "SLH-DSA-SHAKE-256f";
    }
    return "unknown";
}

const char* operation_name(Operation op) {
    switch (op) {
        case Operation::KEYGEN:  return "keygen";
        case Operation::ENCAPS:  return "encaps";
        case Operation::DECAPS:  return "decaps";
        case Operation::SIGN:    return "sign";
        case Operation::VERIFY:  return "verify";
    }
    return "unknown";
}

bool is_kem_algorithm(Algorithm algo) {
    return algo == Algorithm::ML_KEM_512 ||
           algo == Algorithm::ML_KEM_768 ||
           algo == Algorithm::ML_KEM_1024;
}

bool is_dsa_algorithm(Algorithm algo) {
    switch (algo) {
        case Algorithm::ML_DSA_44:
        case Algorithm::ML_DSA_65:
        case Algorithm::ML_DSA_87:
        case Algorithm::SLH_DSA_SHA2_128s:
        case Algorithm::SLH_DSA_SHA2_128f:
        case Algorithm::SLH_DSA_SHA2_192s:
        case Algorithm::SLH_DSA_SHA2_192f:
        case Algorithm::SLH_DSA_SHA2_256s:
        case Algorithm::SLH_DSA_SHA2_256f:
        case Algorithm::SLH_DSA_SHAKE_128s:
        case Algorithm::SLH_DSA_SHAKE_128f:
        case Algorithm::SLH_DSA_SHAKE_192s:
        case Algorithm::SLH_DSA_SHAKE_192f:
        case Algorithm::SLH_DSA_SHAKE_256s:
        case Algorithm::SLH_DSA_SHAKE_256f:
            return true;
        default:
            return false;
    }
}

bool operation_valid_for_algorithm(Algorithm algo, Operation op) {
    if (is_kem_algorithm(algo)) {
        return op == Operation::KEYGEN || op == Operation::ENCAPS || op == Operation::DECAPS;
    }
    return op == Operation::KEYGEN || op == Operation::SIGN || op == Operation::VERIFY;
}

static Algorithm parse_algorithm(const char* s) {
    if (strcmp(s, "ML-KEM-512") == 0)  return Algorithm::ML_KEM_512;
    if (strcmp(s, "ML-KEM-768") == 0)  return Algorithm::ML_KEM_768;
    if (strcmp(s, "ML-KEM-1024") == 0) return Algorithm::ML_KEM_1024;
    if (strcmp(s, "ML-DSA-44") == 0)   return Algorithm::ML_DSA_44;
    if (strcmp(s, "ML-DSA-65") == 0)   return Algorithm::ML_DSA_65;
    if (strcmp(s, "ML-DSA-87") == 0)   return Algorithm::ML_DSA_87;
    if (strcmp(s, "SLH-DSA-SHA2-128s") == 0)  return Algorithm::SLH_DSA_SHA2_128s;
    if (strcmp(s, "SLH-DSA-SHA2-128f") == 0)  return Algorithm::SLH_DSA_SHA2_128f;
    if (strcmp(s, "SLH-DSA-SHA2-192s") == 0)  return Algorithm::SLH_DSA_SHA2_192s;
    if (strcmp(s, "SLH-DSA-SHA2-192f") == 0)  return Algorithm::SLH_DSA_SHA2_192f;
    if (strcmp(s, "SLH-DSA-SHA2-256s") == 0)  return Algorithm::SLH_DSA_SHA2_256s;
    if (strcmp(s, "SLH-DSA-SHA2-256f") == 0)  return Algorithm::SLH_DSA_SHA2_256f;
    if (strcmp(s, "SLH-DSA-SHAKE-128s") == 0) return Algorithm::SLH_DSA_SHAKE_128s;
    if (strcmp(s, "SLH-DSA-SHAKE-128f") == 0) return Algorithm::SLH_DSA_SHAKE_128f;
    if (strcmp(s, "SLH-DSA-SHAKE-192s") == 0) return Algorithm::SLH_DSA_SHAKE_192s;
    if (strcmp(s, "SLH-DSA-SHAKE-192f") == 0) return Algorithm::SLH_DSA_SHAKE_192f;
    if (strcmp(s, "SLH-DSA-SHAKE-256s") == 0) return Algorithm::SLH_DSA_SHAKE_256s;
    if (strcmp(s, "SLH-DSA-SHAKE-256f") == 0) return Algorithm::SLH_DSA_SHAKE_256f;
    fprintf(stderr, "Unknown algorithm: %s\n", s);
    fprintf(stderr, "Supported: ML-KEM-512, ML-KEM-768, ML-KEM-1024, ML-DSA-44, ML-DSA-65, ML-DSA-87,\n");
    fprintf(stderr, "           SLH-DSA-SHA2-128s, SLH-DSA-SHA2-128f, SLH-DSA-SHA2-192s, SLH-DSA-SHA2-192f,\n");
    fprintf(stderr, "           SLH-DSA-SHA2-256s, SLH-DSA-SHA2-256f, SLH-DSA-SHAKE-128s, SLH-DSA-SHAKE-128f,\n");
    fprintf(stderr, "           SLH-DSA-SHAKE-192s, SLH-DSA-SHAKE-192f, SLH-DSA-SHAKE-256s, SLH-DSA-SHAKE-256f\n");
    exit(1);
}

static Operation parse_operation(const char* s) {
    if (strcmp(s, "keygen") == 0) return Operation::KEYGEN;
    if (strcmp(s, "encaps") == 0) return Operation::ENCAPS;
    if (strcmp(s, "decaps") == 0) return Operation::DECAPS;
    if (strcmp(s, "sign") == 0)   return Operation::SIGN;
    if (strcmp(s, "verify") == 0) return Operation::VERIFY;
    fprintf(stderr, "Unknown operation: %s\n", s);
    fprintf(stderr, "Supported: keygen, encaps, decaps, sign, verify\n");
    exit(1);
}

Config parse_args(int argc, char* argv[]) {
    Config cfg;
    bool has_algo = false, has_op = false;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--algorithm") == 0 || strcmp(argv[i], "-a") == 0) && i + 1 < argc) {
            cfg.algorithm = parse_algorithm(argv[++i]);
            has_algo = true;
        } else if ((strcmp(argv[i], "--operation") == 0 || strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            cfg.operation = parse_operation(argv[++i]);
            has_op = true;
        } else if ((strcmp(argv[i], "--threads") == 0 || strcmp(argv[i], "-t") == 0) && i + 1 < argc) {
            cfg.threads = atoi(argv[++i]);
            if (cfg.threads < 1) cfg.threads = 1;
        } else if ((strcmp(argv[i], "--seconds") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc) {
            cfg.seconds = atoi(argv[++i]);
            if (cfg.seconds < 1) cfg.seconds = 1;
        } else if (strcmp(argv[i], "--warmup-seconds") == 0 && i + 1 < argc) {
            cfg.warmup_seconds = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s --algorithm <ALGO> --operation <OP> [--threads N] [--seconds N] [--warmup-seconds N]\n", argv[0]);
            printf("\nAlgorithms: ML-KEM-512, ML-KEM-768, ML-KEM-1024, ML-DSA-44, ML-DSA-65, ML-DSA-87,\n");
            printf("            SLH-DSA-SHA2-128s/128f/192s/192f/256s/256f,\n");
            printf("            SLH-DSA-SHAKE-128s/128f/192s/192f/256s/256f\n");
            printf("Operations: keygen, encaps, decaps, sign, verify\n");
            exit(0);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            exit(1);
        }
    }

    if (!has_algo || !has_op) {
        fprintf(stderr, "Error: --algorithm and --operation are required\n");
        exit(1);
    }

    if (!operation_valid_for_algorithm(cfg.algorithm, cfg.operation)) {
        fprintf(stderr, "Error: operation '%s' not valid for algorithm '%s'\n",
                operation_name(cfg.operation), algorithm_name(cfg.algorithm));
        exit(1);
    }

    return cfg;
}

void print_banner(const LibraryInfo& info, const Config& cfg) {
    printf("=============================================================\n");
    printf("PQC Benchmark: %s\n", info.name.c_str());
    printf("  Version:   %s\n", info.version.c_str());
    if (!info.build_info.empty())
        printf("  Build:     %s\n", info.build_info.c_str());
    printf("  Algorithm: %s\n", algorithm_name(cfg.algorithm));
    printf("  Operation: %s\n", operation_name(cfg.operation));
    printf("  Threads:   %d\n", cfg.threads);
    printf("  Duration:  %d seconds (warmup: %d seconds)\n", cfg.seconds, cfg.warmup_seconds);
    printf("=============================================================\n\n");
}

void print_results(const std::vector<ThreadResult>& results, const Config& cfg, const LibraryInfo& info) {
    printf("\n--- Results ---\n");
    printf("%-8s %12s %12s %12s\n", "Thread", "Operations", "ops/sec", "ns/op");
    printf("%-8s %12s %12s %12s\n", "------", "----------", "-------", "-----");

    uint64_t total_ops = 0;
    double total_elapsed_s = 0.0;

    for (size_t i = 0; i < results.size(); i++) {
        const auto& r = results[i];
        double elapsed_s = r.elapsed_ns / 1e9;
        double ops_per_sec = (elapsed_s > 0) ? r.operations / elapsed_s : 0;
        double ns_per_op = (r.operations > 0) ? static_cast<double>(r.elapsed_ns) / r.operations : 0;
        printf("%-8zu %12lu %12.1f %12.1f\n", i, r.operations, ops_per_sec, ns_per_op);
        total_ops += r.operations;
        total_elapsed_s = std::max(total_elapsed_s, elapsed_s);
    }

    double aggregate_ops_per_sec = (total_elapsed_s > 0) ? total_ops / total_elapsed_s : 0;
    double aggregate_ns_per_op = (total_ops > 0) ? (total_elapsed_s * 1e9) / total_ops : 0;

    printf("%-8s %12s %12s %12s\n", "------", "----------", "-------", "-----");
    printf("%-8s %12lu %12.1f %12.1f\n", "TOTAL", total_ops, aggregate_ops_per_sec, aggregate_ns_per_op);

    std::string lib_label = info.name + " (" + info.version + ")";
    printf("\nCSV: %s,%s,%s,%d,%lu,%.1f,%.1f\n",
           lib_label.c_str(), algorithm_name(cfg.algorithm), operation_name(cfg.operation),
           cfg.threads, total_ops, aggregate_ops_per_sec, aggregate_ns_per_op);
}

std::vector<ThreadResult> run_benchmark(
    const Config& cfg,
    SetupFn setup_fn,
    BenchFn bench_fn,
    TeardownFn teardown_fn)
{
    std::vector<ThreadResult> results(cfg.threads);
    std::vector<std::thread> threads;
    threads.reserve(cfg.threads);

    uint64_t warmup_ns = static_cast<uint64_t>(cfg.warmup_seconds) * 1000000000ULL;
    uint64_t run_ns = static_cast<uint64_t>(cfg.seconds) * 1000000000ULL;

    for (int tid = 0; tid < cfg.threads; tid++) {
        threads.emplace_back([&, tid]() {
            if (!setup_fn(tid)) {
                return;
            }

            // Warmup phase: no recording
            uint64_t warmup_start = now_ns();
            while (now_ns() - warmup_start < warmup_ns) {
                bench_fn(tid);
            }

            // Measurement phase
            uint64_t start = now_ns();
            uint64_t ops = 0;
            while (true) {
                bench_fn(tid);
                ops++;
                if ((ops & 0xFF) == 0) {  // check clock every 256 iterations
                    if (now_ns() - start >= run_ns) break;
                }
            }
            uint64_t end = now_ns();

            results[tid].operations = ops;
            results[tid].elapsed_ns = end - start;

            teardown_fn(tid);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    return results;
}

} // namespace bench
