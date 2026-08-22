#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_DIR="${SCRIPT_DIR}/.."
BUILD_DIR="${BENCH_DIR}/build"

LIB="${1:-openssl}"
ALGO="${2:-ML-KEM-768}"
OPERATION="${3:-encaps}"
SECONDS_RUN="${4:-5}"

BINARY="${BUILD_DIR}/bench_${LIB}"
if [ ! -x "${BINARY}" ]; then
    echo "ERROR: Binary not found: ${BINARY}"
    echo "Available binaries:"
    ls "${BUILD_DIR}"/bench_* 2>/dev/null || echo "  (none - build first)"
    exit 1
fi

OUTDIR="${BENCH_DIR}/results/perf"
mkdir -p "${OUTDIR}"

STAMP=$(date +%Y%m%d_%H%M%S)
PREFIX="${OUTDIR}/${LIB}_${ALGO}_${OPERATION}_${STAMP}"

echo "=== PQC Perf Profile ==="
echo "  Library:    ${LIB}"
echo "  Algorithm:  ${ALGO}"
echo "  Operation:  ${OPERATION}"
echo "  Duration:   ${SECONDS_RUN}s"
echo "  Output:     ${PREFIX}*"
echo ""

# perf stat
echo "--- perf stat -d ---"
STAT_FILE="${PREFIX}_stat.txt"
perf stat -d -- "${BINARY}" \
    --algorithm "${ALGO}" \
    --operation "${OPERATION}" \
    --threads 1 \
    --seconds "${SECONDS_RUN}" \
    --warmup-seconds 1 \
    2>&1 | tee "${STAT_FILE}"
echo ""
echo "Saved: ${STAT_FILE}"
echo ""

# perf record + report
echo "--- perf record -g ---"
PERF_DATA="${PREFIX}_perf.data"
REPORT_FILE="${PREFIX}_report.txt"

perf record -g -o "${PERF_DATA}" -- "${BINARY}" \
    --algorithm "${ALGO}" \
    --operation "${OPERATION}" \
    --threads 1 \
    --seconds "${SECONDS_RUN}" \
    --warmup-seconds 1

perf report --stdio -i "${PERF_DATA}" > "${REPORT_FILE}"
echo "Saved: ${REPORT_FILE}"
echo ""

echo "=== Profiling Complete ==="
echo "  stat output:   ${STAT_FILE}"
echo "  perf data:     ${PERF_DATA}"
echo "  perf report:   ${REPORT_FILE}"
