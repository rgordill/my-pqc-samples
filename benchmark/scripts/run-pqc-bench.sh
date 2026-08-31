#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_DIR="${SCRIPT_DIR}/.."
BUILD_DIR="${BENCH_DIR}/build"
THIRD_PARTY="${BENCH_DIR}/third_party/install"

# --- Options ---
PIN_CPUS=""
OUTDIR=""
FILTER_LIBS=""
FILTER_ALGOS=""

usage() {
    echo "Usage: $0 [OPTIONS] [output_dir]"
    echo ""
    echo "Options:"
    echo "  --pin-cpus CPUSET       Pin benchmark processes to specific CPUs using taskset."
    echo "                          CPUSET uses taskset syntax: e.g. '0-3', '0,2,4', '0-7'."
    echo "  --libraries LIBS        Comma-separated list of libraries to test."
    echo "                          Valid names: openssl, openssl3, boringssl, aws_lc, gnutls, ipp_crypto"
    echo "  --algorithms ALGOS      Comma-separated list of algorithms to test."
    echo "                          e.g. ML-KEM-768,ML-DSA-65,SLH-DSA-SHA2-128f"
    echo "  -h, --help              Show this help message."
    echo ""
    echo "Examples:"
    echo "  $0                                                   # Run all libraries and algorithms"
    echo "  $0 --libraries openssl,aws_lc                        # Only OpenSSL 4.x and AWS-LC"
    echo "  $0 --algorithms ML-KEM-768,ML-DSA-65                 # Only these two algorithms"
    echo "  $0 --libraries ipp_crypto --algorithms ML-KEM-512    # IPP Crypto, ML-KEM-512 only"
    echo "  $0 --pin-cpus 0-3                                    # Pin to CPUs 0-3"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --pin-cpus)
            PIN_CPUS="$2"
            shift 2
            ;;
        --libraries)
            FILTER_LIBS="$2"
            shift 2
            ;;
        --algorithms)
            FILTER_ALGOS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            OUTDIR="$1"
            shift
            ;;
    esac
done

OUTDIR="${OUTDIR:-${BENCH_DIR}/results/$(date +%Y%m%d_%H%M%S)}"
mkdir -p "${OUTDIR}"

# Build the taskset prefix command if CPU pinning is requested
TASKSET_CMD=()
if [ -n "${PIN_CPUS}" ]; then
    if ! command -v taskset &>/dev/null; then
        echo "ERROR: --pin-cpus requires 'taskset' (from util-linux) but it's not installed."
        exit 1
    fi
    TASKSET_CMD=(taskset -c "${PIN_CPUS}")
fi

# NOTE: BoringSSL and AWS-LC both install as libcrypto.so — RUNPATH on each
# binary resolves to the correct library.  Do NOT set a global LD_LIBRARY_PATH
# here (it takes precedence over RUNPATH and causes cross-loading).

# --- Log file setup ---
LOG_FILE="${OUTDIR}/run.log"

log() {
    echo "$@" | tee -a "${LOG_FILE}"
}

# Initialize log with system information
{
    echo "==============================================================="
    echo "PQC Benchmark Run Log"
    echo "==============================================================="
    echo ""
    echo "Date:        $(date -Iseconds)"
    echo "Hostname:    $(hostname)"
    echo "Kernel:      $(uname -r)"
    echo "Arch:        $(uname -m)"
    echo ""
    echo "--- CPU Information ---"
    echo "Online CPUs: $(nproc)"
    if [ -f /proc/cpuinfo ]; then
        echo "Model:       $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')"
        SOCKETS=$(grep 'physical id' /proc/cpuinfo | sort -u | wc -l)
        CORES=$(grep 'cpu cores' /proc/cpuinfo | sort -u | head -1 | cut -d: -f2 | sed 's/^ //')
        echo "Sockets:     ${SOCKETS}"
        echo "Cores/sock:  ${CORES:-N/A}"
    fi
    if [ -f /sys/devices/cpu/caps/pmu_name ]; then
        echo "PMU:         $(cat /sys/devices/cpu/caps/pmu_name)"
    fi
    if lscpu 2>/dev/null | grep -q "Hybrid"; then
        echo "Hybrid:      yes (P-core + E-core)"
    fi
    echo ""
    echo "--- Pinning ---"
    if [ -n "${PIN_CPUS}" ]; then
        echo "CPU affinity: taskset -c ${PIN_CPUS}"
    else
        echo "CPU affinity: none (OS scheduler)"
    fi
    echo ""
    echo "==============================================================="
    echo ""
} > "${LOG_FILE}"

log "=== PQC Benchmark Sweep ==="
log "Output directory: ${OUTDIR}"
if [ -n "${PIN_CPUS}" ]; then
    log "CPU pinning:     taskset -c ${PIN_CPUS}"
fi
log ""

# Build the project
log "--- Building benchmarks ---"
cmake -B "${BUILD_DIR}" -S "${BENCH_DIR}" -DCMAKE_BUILD_TYPE=Release >> "${LOG_FILE}" 2>&1
cmake --build "${BUILD_DIR}" -j"$(nproc)" >> "${LOG_FILE}" 2>&1
log "Build complete."
log ""

ALGORITHMS=(ML-KEM-512 ML-KEM-768 ML-KEM-1024 ML-DSA-44 ML-DSA-65 ML-DSA-87
            SLH-DSA-SHA2-128s SLH-DSA-SHA2-128f SLH-DSA-SHA2-192f SLH-DSA-SHA2-256f
            SLH-DSA-SHAKE-128f SLH-DSA-SHAKE-256f)

# GnuTLS PQC coverage (verified via dry-run against leancrypto 1.8.0):
#   ML-KEM-768:           keygen only (encaps/decaps not in public API)
#   ML-DSA-44/65/87:      keygen, sign, verify
GNUTLS_ALGORITHMS=(ML-KEM-768 ML-DSA-44 ML-DSA-65 ML-DSA-87)

# Intel IPP Crypto: ML-KEM and ML-DSA only (no SLH-DSA support)
IPPCRYPTO_ALGORITHMS=(ML-KEM-512 ML-KEM-768 ML-KEM-1024 ML-DSA-44 ML-DSA-65 ML-DSA-87)

# Parse --algorithms filter into an associative set for O(1) lookup
declare -A ALGOS_FILTER
if [ -n "${FILTER_ALGOS}" ]; then
    IFS=',' read -ra _ALGOS <<< "${FILTER_ALGOS}"
    for _algo in "${_ALGOS[@]}"; do
        ALGOS_FILTER["${_algo}"]=1
    done
fi

algorithms_for() {
    local bin="$1"
    local candidates=()
    if [[ "${bin}" == bench_gnutls ]]; then
        candidates=("${GNUTLS_ALGORITHMS[@]}")
    elif [[ "${bin}" == bench_ipp_crypto ]]; then
        candidates=("${IPPCRYPTO_ALGORITHMS[@]}")
    else
        candidates=("${ALGORITHMS[@]}")
    fi

    for a in "${candidates[@]}"; do
        if [ -n "${FILTER_ALGOS}" ] && [ -z "${ALGOS_FILTER[${a}]+_}" ]; then
            continue
        fi
        printf '%s\n' "${a}"
    done
}

operations_for() {
    local bin="$1"
    local algo="$2"

    if [[ "${bin}" == bench_gnutls ]]; then
        if [[ "${algo}" == ML-KEM-* ]]; then
            echo keygen
        else
            echo keygen sign verify
        fi
        return
    fi

    if [[ "${algo}" == ML-KEM-* ]]; then
        echo keygen encaps decaps
    else
        echo keygen sign verify
    fi
}

# Compute max threads: largest power of 2 <= (nproc - 2), minimum 1
NCPUS=$(nproc)
AVAILABLE=$((NCPUS - 2))
if [ "${AVAILABLE}" -lt 1 ]; then
    AVAILABLE=1
fi
MAX_THREADS=1
while [ $((MAX_THREADS * 2)) -le "${AVAILABLE}" ]; do
    MAX_THREADS=$((MAX_THREADS * 2))
done

THREADS=()
T=1
while [ "${T}" -le "${MAX_THREADS}" ]; do
    THREADS+=("${T}")
    T=$((T * 2))
done

# Parse --libraries filter into an associative set for O(1) lookup
declare -A LIBS_FILTER
if [ -n "${FILTER_LIBS}" ]; then
    IFS=',' read -ra _LIBS <<< "${FILTER_LIBS}"
    for _lib in "${_LIBS[@]}"; do
        LIBS_FILTER["bench_${_lib}"]=1
    done
fi

BINARIES=()
for bin in bench_openssl bench_openssl3 bench_boringssl bench_aws_lc bench_gnutls bench_ipp_crypto; do
    if [ -x "${BUILD_DIR}/${bin}" ]; then
        if [ -n "${FILTER_LIBS}" ] && [ -z "${LIBS_FILTER[${bin}]+_}" ]; then
            continue
        fi
        BINARIES+=("${bin}")
    fi
done

if [ ${#BINARIES[@]} -eq 0 ]; then
    echo "ERROR: No benchmark binaries found in ${BUILD_DIR}"
    if [ -n "${FILTER_LIBS}" ]; then
        echo "  (--libraries filter: ${FILTER_LIBS})"
        echo "  Valid names: openssl, openssl3, boringssl, aws_lc, gnutls, ipp_crypto"
    fi
    exit 1
fi

# Probe each binary to get its versioned library label
declare -A BIN_LABELS
for bin in "${BINARIES[@]}"; do
    PROBE_OUT=$("${BUILD_DIR}/${bin}" -a ML-KEM-768 -o keygen -t 1 -s 1 --warmup-seconds 0 2>&1) || true
    LABEL=$(echo "${PROBE_OUT}" | grep "^CSV:" | head -1 | sed 's/^CSV: //' | cut -d',' -f1)
    if [ -z "${LABEL}" ]; then
        LABEL="${bin#bench_}"
    fi
    BIN_LABELS["${bin}"]="${LABEL}"
done

log "--- Libraries ---"
for bin in "${BINARIES[@]}"; do
    log "  ${bin} -> ${BIN_LABELS[${bin}]}"
done
if [ -n "${FILTER_LIBS}" ]; then
    log "  (filtered: ${FILTER_LIBS})"
fi
log ""
log "Algorithms:    ${ALGORITHMS[*]}"
if [ -n "${FILTER_ALGOS}" ]; then
    log "  (filtered: ${FILTER_ALGOS})"
fi
log "Thread counts: ${THREADS[*]} (max=${MAX_THREADS}, host CPUs=${NCPUS})"
log ""

CSV_FILE="${OUTDIR}/summary.csv"
echo "library,algorithm,operation,threads,total_ops,ops_per_sec,ns_per_op" > "${CSV_FILE}"

TOTAL_RUNS=0
SKIPPED=0
FAILED=0
SUCCEEDED=0

for bin in "${BINARIES[@]}"; do
    LIB_LABEL="${BIN_LABELS[${bin}]}"
    log "=== Library: ${LIB_LABEL} ==="

    mapfile -t BIN_ALGOS < <(algorithms_for "${bin}")

    for algo in "${BIN_ALGOS[@]}"; do
        read -r -a OPERATIONS <<< "$(operations_for "${bin}" "${algo}")"

        for op in "${OPERATIONS[@]}"; do
            for threads in "${THREADS[@]}"; do
                TOTAL_RUNS=$((TOTAL_RUNS + 1))
                MSG=$(printf "  %-24s %-20s %-8s threads=%-2d ... " "${LIB_LABEL}" "${algo}" "${op}" "${threads}")
                printf "%s" "${MSG}"

                EXIT_CODE=0
                OUTPUT=$("${TASKSET_CMD[@]}" "${BUILD_DIR}/${bin}" \
                    --algorithm "${algo}" \
                    --operation "${op}" \
                    --threads "${threads}" \
                    --seconds 3 \
                    --warmup-seconds 1 2>&1) || EXIT_CODE=$?

                if [ "${EXIT_CODE}" -eq 2 ]; then
                    RESULT="SKIP (unsupported)"
                    echo "${RESULT}"
                    echo "${MSG}${RESULT}" >> "${LOG_FILE}"
                    SKIPPED=$((SKIPPED + 1))
                    continue
                elif [ "${EXIT_CODE}" -ne 0 ]; then
                    RESULT="FAIL (exit ${EXIT_CODE})"
                    echo "${RESULT}"
                    echo "${MSG}${RESULT}" >> "${LOG_FILE}"
                    FAILED=$((FAILED + 1))
                    continue
                fi

                CSV_LINE=$(echo "${OUTPUT}" | grep "^CSV:" | head -1 | sed 's/^CSV: //')
                if [ -n "${CSV_LINE}" ]; then
                    echo "${CSV_LINE}" >> "${CSV_FILE}"
                    OPS_SEC=$(echo "${CSV_LINE}" | cut -d',' -f6)
                    RESULT="OK (${OPS_SEC} ops/s)"
                    echo "${RESULT}"
                    echo "${MSG}${RESULT}" >> "${LOG_FILE}"
                    SUCCEEDED=$((SUCCEEDED + 1))
                else
                    RESULT="FAIL (no CSV output)"
                    echo "${RESULT}"
                    echo "${MSG}${RESULT}" >> "${LOG_FILE}"
                    FAILED=$((FAILED + 1))
                fi
            done
        done
    done
    log ""
done

log "=== Benchmark Sweep Complete ==="
log "  Total runs:  ${TOTAL_RUNS}"
log "  Succeeded:   ${SUCCEEDED}"
log "  Skipped:     ${SKIPPED}"
log "  Failed:      ${FAILED}"
log "  Results:     ${CSV_FILE}"
log "  Log:         ${LOG_FILE}"
log ""
echo "CSV summary:"
column -t -s',' "${CSV_FILE}" 2>/dev/null || cat "${CSV_FILE}"
