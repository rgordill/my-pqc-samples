#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_DIR="${SCRIPT_DIR}/.."
BUILD_DIR="${BENCH_DIR}/build"

usage() {
    echo "Usage: $0 [OPTIONS] <library> <algorithm> <operation> [seconds]"
    echo ""
    echo "Profile a specific PQC benchmark combination using perf."
    echo "Generates: perf stat, perf report, and flamegraph (HTML)."
    echo ""
    echo "Arguments:"
    echo "  library     Backend name: openssl, openssl3, boringssl, aws_lc, gnutls, ipp_crypto"
    echo "  algorithm   e.g. ML-KEM-768, ML-DSA-65, SLH-DSA-SHA2-128f"
    echo "  operation   keygen, encaps, decaps, sign, verify"
    echo "  seconds     Duration of profiling run (default: 10)"
    echo ""
    echo "Options:"
    echo "  --threads N      Number of threads (default: 1)"
    echo "  --freq N         Sampling frequency in Hz (default: 997)"
    echo "  --no-flamegraph  Skip flamegraph generation"
    echo "  -h, --help       Show this help message"
    echo ""
    echo "Prerequisites:"
    echo "  sudo dnf install perf js-d3-flame-graph"
    echo ""
    echo "Examples:"
    echo "  $0 openssl ML-KEM-768 encaps"
    echo "  $0 --threads 4 aws_lc ML-DSA-65 sign 15"
    echo "  $0 gnutls ML-DSA-87 verify 10"
    exit 0
}

THREADS=1
FREQ=997
FLAMEGRAPH=true
POSITIONAL=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --threads)
            THREADS="$2"
            shift 2
            ;;
        --freq)
            FREQ="$2"
            shift 2
            ;;
        --no-flamegraph)
            FLAMEGRAPH=false
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            POSITIONAL+=("$1")
            shift
            ;;
    esac
done

if [ ${#POSITIONAL[@]} -lt 3 ]; then
    echo "ERROR: Missing required arguments."
    echo ""
    usage
fi

LIB="${POSITIONAL[0]}"
ALGO="${POSITIONAL[1]}"
OPERATION="${POSITIONAL[2]}"
SECONDS_RUN="${POSITIONAL[3]:-10}"

BINARY="${BUILD_DIR}/bench_${LIB}"
if [ ! -x "${BINARY}" ]; then
    echo "ERROR: Binary not found: ${BINARY}"
    echo "Available binaries:"
    ls "${BUILD_DIR}"/bench_* 2>/dev/null || echo "  (none - build first)"
    exit 1
fi

if ! command -v perf &>/dev/null; then
    echo "ERROR: 'perf' not found. Install with: sudo dnf install perf"
    exit 1
fi

FLAMEGRAPH_TEMPLATE="/usr/share/d3-flame-graph/d3-flamegraph-base.html"
FLAMEGRAPH_SCRIPT="/usr/libexec/perf-core/scripts/python/flamegraph.py"
HAS_LOCAL_TEMPLATE=false

if [ -f "${FLAMEGRAPH_TEMPLATE}" ]; then
    HAS_LOCAL_TEMPLATE=true
fi

if [ "${FLAMEGRAPH}" = true ] && [ ! -f "${FLAMEGRAPH_SCRIPT}" ]; then
    echo "WARNING: perf flamegraph script not found at ${FLAMEGRAPH_SCRIPT}"
    echo "  Flamegraph generation will be skipped."
    FLAMEGRAPH=false
fi

OUTDIR="${BENCH_DIR}/results/perf"
mkdir -p "${OUTDIR}"

STAMP=$(date +%Y%m%d_%H%M%S)
PREFIX="${OUTDIR}/${LIB}_${ALGO}_${OPERATION}_t${THREADS}_${STAMP}"

BENCH_ARGS=(
    --algorithm "${ALGO}"
    --operation "${OPERATION}"
    --threads "${THREADS}"
    --seconds "${SECONDS_RUN}"
    --warmup-seconds 1
)

echo "=== PQC Perf Profile ==="
echo "  Library:    ${LIB}"
echo "  Algorithm:  ${ALGO}"
echo "  Operation:  ${OPERATION}"
echo "  Threads:    ${THREADS}"
echo "  Duration:   ${SECONDS_RUN}s"
echo "  Frequency:  ${FREQ} Hz"
echo "  Flamegraph: ${FLAMEGRAPH}"
echo "  Output:     ${PREFIX}*"
echo ""

# --- perf stat ---
echo "--- [1/3] perf stat -d ---"
STAT_FILE="${PREFIX}_stat.txt"
perf stat -d -- "${BINARY}" "${BENCH_ARGS[@]}" 2>&1 | tee "${STAT_FILE}"
echo ""
echo "  Saved: ${STAT_FILE}"
echo ""

# --- perf record ---
echo "--- [2/3] perf record -g (frame-pointer call-graph at ${FREQ} Hz) ---"
PERF_DATA="${PREFIX}_perf.data"
REPORT_FILE="${PREFIX}_report.txt"

perf record -g --call-graph fp -F "${FREQ}" -o "${PERF_DATA}" -- "${BINARY}" "${BENCH_ARGS[@]}"

perf report --stdio -i "${PERF_DATA}" > "${REPORT_FILE}"
echo "  Saved: ${PERF_DATA}"
echo "  Saved: ${REPORT_FILE}"
echo ""

# --- Flamegraph ---
if [ "${FLAMEGRAPH}" = true ]; then
    echo "--- [3/3] Flamegraph generation ---"
    FLAMEGRAPH_HTML="${PREFIX}_flamegraph.html"

    FLAME_ARGS=(-o "${FLAMEGRAPH_HTML}")
    if [ "${HAS_LOCAL_TEMPLATE}" = true ]; then
        FLAME_ARGS+=(--template "${FLAMEGRAPH_TEMPLATE}")
    else
        FLAME_ARGS+=(--allow-download)
        echo "  (js-d3-flame-graph not installed; downloading template from CDN)"
    fi

    # perf script -i <data> -s flamegraph.py -- <flamegraph args>
    if perf script -i "${PERF_DATA}" -s "${FLAMEGRAPH_SCRIPT}" -- "${FLAME_ARGS[@]}" 2>/dev/null; then
        : # success
    elif command -v stackcollapse-perf.pl &>/dev/null && command -v flamegraph.pl &>/dev/null; then
        # Fallback: Brendan Gregg's FlameGraph tools
        perf script -i "${PERF_DATA}" | stackcollapse-perf.pl | flamegraph.pl > "${FLAMEGRAPH_HTML}"
    else
        echo "  WARNING: Could not generate flamegraph."
        echo "  Install the template package: sudo dnf install js-d3-flame-graph"
        FLAMEGRAPH=false
    fi

    if [ "${FLAMEGRAPH}" = true ] && [ -f "${FLAMEGRAPH_HTML}" ]; then
        echo "  Saved: ${FLAMEGRAPH_HTML}"
        echo ""
        echo "  View with: xdg-open ${FLAMEGRAPH_HTML}"
    fi
    echo ""
fi

echo "=== Profiling Complete ==="
echo ""
echo "Output files:"
echo "  perf stat:     ${STAT_FILE}"
echo "  perf data:     ${PERF_DATA}"
echo "  perf report:   ${REPORT_FILE}"
if [ "${FLAMEGRAPH}" = true ] && [ -f "${FLAMEGRAPH_HTML:-}" ]; then
    echo "  flamegraph:    ${FLAMEGRAPH_HTML}"
fi
echo ""
echo "Quick analysis:"
echo "  perf report -i ${PERF_DATA}"
echo "  perf annotate -i ${PERF_DATA}"
if [ "${FLAMEGRAPH}" = true ] && [ -f "${FLAMEGRAPH_HTML:-}" ]; then
    echo "  xdg-open ${FLAMEGRAPH_HTML}"
fi
