#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_DIR="${SCRIPT_DIR}/.."
BUILD_DIR="${BENCH_DIR}/build"
THIRD_PARTY="${BENCH_DIR}/third_party/install"

OUTDIR="${1:-${BENCH_DIR}/results/$(date +%Y%m%d_%H%M%S)}"
mkdir -p "${OUTDIR}"

# NOTE: BoringSSL and AWS-LC both install as libcrypto.so — RUNPATH on each
# binary resolves to the correct library.  Do NOT set a global LD_LIBRARY_PATH
# here (it takes precedence over RUNPATH and causes cross-loading).

echo "=== PQC Benchmark Sweep ==="
echo "Output directory: ${OUTDIR}"
echo ""

# Build the project
echo "--- Building benchmarks ---"
cmake -B "${BUILD_DIR}" -S "${BENCH_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j"$(nproc)"
echo ""

ALGORITHMS=(ML-KEM-512 ML-KEM-768 ML-KEM-1024 ML-DSA-44 ML-DSA-65 ML-DSA-87
            SLH-DSA-SHA2-128s SLH-DSA-SHA2-128f SLH-DSA-SHA2-192f SLH-DSA-SHA2-256f
            SLH-DSA-SHAKE-128f SLH-DSA-SHAKE-256f)
THREADS=(1 2 4 8)

BINARIES=()
for bin in bench_openssl bench_openssl3 bench_boringssl bench_aws_lc; do
    if [ -x "${BUILD_DIR}/${bin}" ]; then
        BINARIES+=("${bin}")
    fi
done

if [ ${#BINARIES[@]} -eq 0 ]; then
    echo "ERROR: No benchmark binaries found in ${BUILD_DIR}"
    exit 1
fi

echo "Found binaries: ${BINARIES[*]}"
echo "Algorithms: ${ALGORITHMS[*]}"
echo "Thread counts: ${THREADS[*]}"
echo ""

CSV_FILE="${OUTDIR}/summary.csv"
echo "library,algorithm,operation,threads,total_ops,ops_per_sec,ns_per_op" > "${CSV_FILE}"

TOTAL_RUNS=0
SKIPPED=0
FAILED=0
SUCCEEDED=0

for bin in "${BINARIES[@]}"; do
    LIB_NAME="${bin#bench_}"
    echo "=== Library: ${LIB_NAME} ==="

    for algo in "${ALGORITHMS[@]}"; do
        # Determine valid operations for this algorithm
        if [[ "${algo}" == ML-KEM-* ]]; then
            OPERATIONS=(keygen encaps decaps)
        else
            OPERATIONS=(keygen sign verify)
        fi

        for op in "${OPERATIONS[@]}"; do
            for threads in "${THREADS[@]}"; do
                TOTAL_RUNS=$((TOTAL_RUNS + 1))
                printf "  %-12s %-12s %-8s threads=%-2d ... " "${LIB_NAME}" "${algo}" "${op}" "${threads}"

                EXIT_CODE=0
                OUTPUT=$("${BUILD_DIR}/${bin}" \
                    --algorithm "${algo}" \
                    --operation "${op}" \
                    --threads "${threads}" \
                    --seconds 3 \
                    --warmup-seconds 1 2>&1) || EXIT_CODE=$?

                if [ "${EXIT_CODE}" -eq 2 ]; then
                    echo "SKIP (unsupported)"
                    SKIPPED=$((SKIPPED + 1))
                    continue
                elif [ "${EXIT_CODE}" -ne 0 ]; then
                    echo "FAIL (exit ${EXIT_CODE})"
                    FAILED=$((FAILED + 1))
                    continue
                fi

                CSV_LINE=$(echo "${OUTPUT}" | grep "^CSV:" | head -1 | sed 's/^CSV: //')
                if [ -n "${CSV_LINE}" ]; then
                    echo "${CSV_LINE}" >> "${CSV_FILE}"
                    OPS_SEC=$(echo "${CSV_LINE}" | cut -d',' -f6)
                    echo "OK (${OPS_SEC} ops/s)"
                    SUCCEEDED=$((SUCCEEDED + 1))
                else
                    echo "FAIL (no CSV output)"
                    FAILED=$((FAILED + 1))
                fi
            done
        done
    done
    echo ""
done

echo "=== Benchmark Sweep Complete ==="
echo "  Total runs:  ${TOTAL_RUNS}"
echo "  Succeeded:   ${SUCCEEDED}"
echo "  Skipped:     ${SKIPPED}"
echo "  Failed:      ${FAILED}"
echo "  Results:     ${CSV_FILE}"
echo ""
echo "CSV summary:"
column -t -s',' "${CSV_FILE}" 2>/dev/null || cat "${CSV_FILE}"
