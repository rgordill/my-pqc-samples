#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace bench {

enum class Algorithm {
    ML_KEM_512,
    ML_KEM_768,
    ML_KEM_1024,
    ML_DSA_44,
    ML_DSA_65,
    ML_DSA_87,
    SLH_DSA_SHA2_128s,
    SLH_DSA_SHA2_128f,
    SLH_DSA_SHA2_192s,
    SLH_DSA_SHA2_192f,
    SLH_DSA_SHA2_256s,
    SLH_DSA_SHA2_256f,
    SLH_DSA_SHAKE_128s,
    SLH_DSA_SHAKE_128f,
    SLH_DSA_SHAKE_192s,
    SLH_DSA_SHAKE_192f,
    SLH_DSA_SHAKE_256s,
    SLH_DSA_SHAKE_256f,
};

enum class Operation {
    KEYGEN,
    ENCAPS,
    DECAPS,
    SIGN,
    VERIFY,
};

struct Config {
    Algorithm algorithm;
    Operation operation;
    int threads = 1;
    int seconds = 3;
    int warmup_seconds = 1;
};

struct ThreadResult {
    uint64_t operations = 0;
    uint64_t elapsed_ns = 0;
};

struct LibraryInfo {
    std::string name;
    std::string version;
    std::string build_info;
};

// Callback that each backend implements: perform one iteration of the operation.
// Called repeatedly in the hot loop. Must be self-contained (no shared mutable state).
using SetupFn = std::function<bool(int thread_id)>;
using BenchFn = std::function<void(int thread_id)>;
using TeardownFn = std::function<void(int thread_id)>;

Config parse_args(int argc, char* argv[]);
const char* algorithm_name(Algorithm algo);
const char* operation_name(Operation op);
bool is_kem_algorithm(Algorithm algo);
bool is_dsa_algorithm(Algorithm algo);
bool operation_valid_for_algorithm(Algorithm algo, Operation op);

void print_banner(const LibraryInfo& info, const Config& cfg);
void print_results(const std::vector<ThreadResult>& results, const Config& cfg, const LibraryInfo& info);

// Run the benchmark: spawns cfg.threads threads, each calling bench_fn in a tight loop.
// setup_fn is called once per thread before timing. teardown_fn after.
std::vector<ThreadResult> run_benchmark(
    const Config& cfg,
    SetupFn setup_fn,
    BenchFn bench_fn,
    TeardownFn teardown_fn);

} // namespace bench
