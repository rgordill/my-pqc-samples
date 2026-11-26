#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY="${SCRIPT_DIR}/../third_party"
INSTALL_DIR="${THIRD_PARTY}/install"

NPROC=${NPROC:-$(nproc 2>/dev/null || echo 4)}

OPENSSL4_VERSION="openssl-4.1.0"
OPENSSL3_VERSION="openssl-3.6.5"
BORINGSSL_VERSION="0.20261105.0"
AWSLC_VERSION="v5.15.0"
LEANCRYPTO_VERSION="v1.9.1"
GNUTLS_VERSION="3.8.14"
IPPCRYPTO_VERSION="v2.4.0"

mkdir -p "${THIRD_PARTY}" "${INSTALL_DIR}"

# Profiling-ready flags: frame pointers for perf call-graph, debug symbols for annotation.
# -fno-omit-frame-pointer: <1% overhead; enables frame-pointer based stack unwinding.
# -g: zero runtime cost (debug sections not loaded at execution time).
PROFILE_CFLAGS="-fno-omit-frame-pointer -g"
PROFILE_CXXFLAGS="-fno-omit-frame-pointer -g"

# Minimal Time::Piece stub for OpenSSL 4.x build on minimal Perl installs
PERL_STUB="${THIRD_PARTY}/_perlstub"
if [ -d "${PERL_STUB}" ]; then
    export PERL5LIB="${PERL_STUB}${PERL5LIB:+:$PERL5LIB}"
fi

# --- Pre-flight: check Perl module used by OpenSSL configdata.pm ---
if ! perl -MFile::Copy -e 1 >/dev/null 2>&1; then
    echo "ERROR: OpenSSL requires the Perl File::Copy module."
    echo "Install with: sudo dnf install perl-File-Copy"
    echo "Then rerun this script in the same build environment."
    exit 1
fi

# --- Pre-flight: check GnuTLS build dependencies ---
MISSING_DEPS=()
for pkg in nettle hogweed gmp libtasn1; do
    if ! pkg-config --exists "${pkg}" 2>/dev/null; then
        MISSING_DEPS+=("${pkg}")
    fi
done

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo "ERROR: Missing development libraries required to build GnuTLS: ${MISSING_DEPS[*]}"
    echo "Install them with:"
    echo "  sudo dnf install nettle-devel gmp-devel libtasn1-devel libunistring-devel"
    exit 1
fi

# --- Pre-flight: check meson/ninja for leancrypto ---
if ! command -v meson &>/dev/null || ! command -v ninja &>/dev/null; then
    echo "WARNING: meson/ninja not found. Required for leancrypto build."
    echo "Install with: pip install --user meson ninja"
    echo ""
fi

# A library alone is insufficient: old installations must not bypass upgrades.
installed_version_matches() {
    local prefix="$1" version="$2"
    shift 2
    [ -f "${prefix}/.version" ] && [ "$(cat "${prefix}/.version")" = "${version}" ] || return 1
    local library
    for library in "$@"; do
        [ ! -f "${prefix}/${library}" ] || return 0
    done
    return 1
}

checkout_release() {
    local source_dir="$1" version="$2" clean_make="${3:-false}"
    if [ "$(git -C "${source_dir}" describe --tags --exact-match HEAD 2>/dev/null || true)" != "${version}" ]; then
        if ! git -C "${source_dir}" diff --quiet || ! git -C "${source_dir}" diff --cached --quiet; then
            echo "ERROR: ${source_dir} has local changes; commit or stash them before upgrading."
            exit 1
        fi
        if ! git -C "${source_dir}" rev-parse --verify "refs/tags/${version}" >/dev/null 2>&1; then
            git -C "${source_dir}" fetch --depth 1 origin "refs/tags/${version}:refs/tags/${version}"
        fi
        if [ "${clean_make}" = true ] && [ -f "${source_dir}/Makefile" ]; then
            make -C "${source_dir}" clean
        fi
        git -C "${source_dir}" checkout --detach "${version}"
    fi
}

# --- OpenSSL 4.x ---
OPENSSL4_DIR="${THIRD_PARTY}/openssl4"
OPENSSL4_INSTALL="${INSTALL_DIR}/openssl4"
OPENSSL4_VERSION_FILE="${OPENSSL4_INSTALL}/.version"

if { [ -f "${OPENSSL4_INSTALL}/lib64/libcrypto.so" ] || [ -f "${OPENSSL4_INSTALL}/lib/libcrypto.so" ]; } \
    && [ -f "${OPENSSL4_VERSION_FILE}" ] \
    && [ "$(cat "${OPENSSL4_VERSION_FILE}")" = "${OPENSSL4_VERSION}" ]; then
    echo "OpenSSL 4.x (${OPENSSL4_VERSION}) already built, skipping."
else
    echo "=== Building OpenSSL 4.x (${OPENSSL4_VERSION}) ==="
    if [ ! -d "${OPENSSL4_DIR}" ]; then
        git clone --branch "${OPENSSL4_VERSION}" --depth 1 \
            https://github.com/openssl/openssl.git "${OPENSSL4_DIR}"
    fi
    cd "${OPENSSL4_DIR}"
    checkout_release "${OPENSSL4_DIR}" "${OPENSSL4_VERSION}" true
    # Record success only after installation; interrupted upgrades must rebuild.
    rm -f "${OPENSSL4_VERSION_FILE}"
    ./Configure --prefix="${OPENSSL4_INSTALL}" shared -O3 ${PROFILE_CFLAGS}
    make -j"${NPROC}"
    make install_sw
    echo "${OPENSSL4_VERSION}" > "${OPENSSL4_VERSION_FILE}"
    echo "OpenSSL 4.x installed to ${OPENSSL4_INSTALL}"
fi

# --- OpenSSL 3.x ---
OPENSSL3_DIR="${THIRD_PARTY}/openssl3"
OPENSSL3_INSTALL="${INSTALL_DIR}/openssl3"

if installed_version_matches "${OPENSSL3_INSTALL}" "${OPENSSL3_VERSION}" lib64/libcrypto.so lib/libcrypto.so; then
    echo "OpenSSL 3.x already built, skipping."
else
    rm -f "${OPENSSL3_INSTALL}/.version"
    echo "=== Building OpenSSL 3.x (${OPENSSL3_VERSION}) ==="
    if [ ! -d "${OPENSSL3_DIR}" ]; then
        git clone --branch "${OPENSSL3_VERSION}" --depth 1 \
            https://github.com/openssl/openssl.git "${OPENSSL3_DIR}"
    fi
    checkout_release "${OPENSSL3_DIR}" "${OPENSSL3_VERSION}" true
    cd "${OPENSSL3_DIR}"
    ./Configure --prefix="${OPENSSL3_INSTALL}" shared -O3 ${PROFILE_CFLAGS}
    make -j"${NPROC}"
    make install_sw
    echo "${OPENSSL3_VERSION}" > "${OPENSSL3_INSTALL}/.version"
    echo "OpenSSL 3.x installed to ${OPENSSL3_INSTALL}"
fi

# --- leancrypto (PQC backend for GnuTLS) ---
LEANCRYPTO_DIR="${THIRD_PARTY}/leancrypto"
LEANCRYPTO_INSTALL="${INSTALL_DIR}/leancrypto"

if installed_version_matches "${LEANCRYPTO_INSTALL}" "${LEANCRYPTO_VERSION}" lib64/libleancrypto.so lib/libleancrypto.so; then
    echo "leancrypto already built, skipping."
else
    rm -f "${LEANCRYPTO_INSTALL}/.version"
    echo "=== Building leancrypto ${LEANCRYPTO_VERSION} ==="
    if ! command -v meson &>/dev/null; then
        echo "ERROR: meson is required to build leancrypto. Install with: pip install --user meson ninja"
        exit 1
    fi
    if [ ! -d "${LEANCRYPTO_DIR}" ]; then
        git clone --branch "${LEANCRYPTO_VERSION}" --depth 1 \
            https://github.com/smuellerDD/leancrypto.git "${LEANCRYPTO_DIR}"
    fi
    checkout_release "${LEANCRYPTO_DIR}" "${LEANCRYPTO_VERSION}"
    cd "${LEANCRYPTO_DIR}"
    MESON_SETUP_ARGS=()
    if [ -f builddir/meson-private/coredata.dat ]; then
        MESON_SETUP_ARGS+=(--wipe)
    fi
    CFLAGS="${PROFILE_CFLAGS}" CXXFLAGS="${PROFILE_CXXFLAGS}" \
    meson setup "${MESON_SETUP_ARGS[@]}" builddir --prefix="${LEANCRYPTO_INSTALL}" --default-library=shared \
        -Dbuildtype=release -Db_ndebug=true
    meson compile -C builddir
    meson install -C builddir
    echo "${LEANCRYPTO_VERSION}" > "${LEANCRYPTO_INSTALL}/.version"
    echo "leancrypto installed to ${LEANCRYPTO_INSTALL}"
fi

# --- GnuTLS 3.8.14 (with leancrypto for PQC) ---
GNUTLS_TARBALL="gnutls-${GNUTLS_VERSION}.tar.xz"
GNUTLS_SRC_DIR="${THIRD_PARTY}/gnutls-${GNUTLS_VERSION}"
GNUTLS_INSTALL="${INSTALL_DIR}/gnutls"

GNUTLS_BUILD_VERSION="${GNUTLS_VERSION}+leancrypto-${LEANCRYPTO_VERSION}"

if installed_version_matches "${GNUTLS_INSTALL}" "${GNUTLS_BUILD_VERSION}" lib/libgnutls.so lib64/libgnutls.so; then
    echo "GnuTLS already built, skipping."
else
    rm -f "${GNUTLS_INSTALL}/.version"
    echo "=== Building GnuTLS ${GNUTLS_VERSION} (with --with-leancrypto) ==="
    if [ ! -d "${GNUTLS_SRC_DIR}" ]; then
        cd "${THIRD_PARTY}"
        if [ ! -f "${GNUTLS_TARBALL}" ]; then
            curl -fSL -o "${GNUTLS_TARBALL}" \
                "https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/${GNUTLS_TARBALL}"
        fi
        tar xf "${GNUTLS_TARBALL}"
    fi

    # leancrypto pkg-config path for GnuTLS configure
    if [ -d "${LEANCRYPTO_INSTALL}/lib64/pkgconfig" ]; then
        LEANCRYPTO_PKG="${LEANCRYPTO_INSTALL}/lib64/pkgconfig"
    else
        LEANCRYPTO_PKG="${LEANCRYPTO_INSTALL}/lib/pkgconfig"
    fi

    cd "${GNUTLS_SRC_DIR}"
    PKG_CONFIG_PATH="${LEANCRYPTO_PKG}:${PKG_CONFIG_PATH:-}" \
    CFLAGS="-O3 ${PROFILE_CFLAGS}" CXXFLAGS="-O3 ${PROFILE_CXXFLAGS}" \
    ./configure --prefix="${GNUTLS_INSTALL}" \
        --disable-doc --disable-tests --disable-tools \
        --without-p11-kit --enable-shared --disable-static \
        --with-leancrypto
    make -j"${NPROC}"
    make install
    echo "${GNUTLS_BUILD_VERSION}" > "${GNUTLS_INSTALL}/.version"
    echo "GnuTLS installed to ${GNUTLS_INSTALL}"
fi

# --- BoringSSL ---
BORINGSSL_DIR="${THIRD_PARTY}/boringssl"
BORINGSSL_INSTALL="${INSTALL_DIR}/boringssl"

if installed_version_matches "${BORINGSSL_INSTALL}" "${BORINGSSL_VERSION}" lib/libcrypto.so; then
    echo "BoringSSL already built, skipping."
else
    rm -f "${BORINGSSL_INSTALL}/.version"
    echo "=== Building BoringSSL ${BORINGSSL_VERSION} ==="
    if [ ! -d "${BORINGSSL_DIR}" ]; then
        git clone --branch "${BORINGSSL_VERSION}" --depth 1 \
            https://boringssl.googlesource.com/boringssl "${BORINGSSL_DIR}"
    fi
    checkout_release "${BORINGSSL_DIR}" "${BORINGSSL_VERSION}"
    cmake -S "${BORINGSSL_DIR}" -B "${BORINGSSL_DIR}/build" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -fno-omit-frame-pointer" \
        -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -fno-omit-frame-pointer" \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    cmake --build "${BORINGSSL_DIR}/build" -j "${NPROC}" --target crypto

    mkdir -p "${BORINGSSL_INSTALL}/lib" "${BORINGSSL_INSTALL}/include"
    cp "${BORINGSSL_DIR}/build/libcrypto.so" "${BORINGSSL_INSTALL}/lib/"
    cp -r "${BORINGSSL_DIR}/include/openssl" "${BORINGSSL_INSTALL}/include/"
    echo "${BORINGSSL_VERSION}" > "${BORINGSSL_INSTALL}/.version"
    echo "BoringSSL installed to ${BORINGSSL_INSTALL}"
fi

# --- AWS-LC ---
AWSLC_DIR="${THIRD_PARTY}/aws-lc"
AWSLC_INSTALL="${INSTALL_DIR}/aws-lc"

if installed_version_matches "${AWSLC_INSTALL}" "${AWSLC_VERSION}" lib/libcrypto.so lib64/libcrypto.so; then
    echo "AWS-LC already built, skipping."
else
    rm -f "${AWSLC_INSTALL}/.version"
    echo "=== Building AWS-LC ${AWSLC_VERSION} ==="
    if [ ! -d "${AWSLC_DIR}" ]; then
        git clone --branch "${AWSLC_VERSION}" --depth 1 \
            https://github.com/aws/aws-lc.git "${AWSLC_DIR}"
    fi
    checkout_release "${AWSLC_DIR}" "${AWSLC_VERSION}"
    cmake -S "${AWSLC_DIR}" -B "${AWSLC_DIR}/build" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -fno-omit-frame-pointer" \
        -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -fno-omit-frame-pointer" \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_TESTING=OFF \
        -DCMAKE_INSTALL_PREFIX="${AWSLC_INSTALL}" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    cmake --build "${AWSLC_DIR}/build" -j "${NPROC}"
    cmake --install "${AWSLC_DIR}/build"
    echo "${AWSLC_VERSION}" > "${AWSLC_INSTALL}/.version"
    echo "AWS-LC installed to ${AWSLC_INSTALL}"
fi

# --- Intel IPP Crypto (Cryptography Primitives Library) ---
IPPCRYPTO_DIR="${THIRD_PARTY}/ipp-crypto"
IPPCRYPTO_INSTALL="${INSTALL_DIR}/ipp-crypto"

if installed_version_matches "${IPPCRYPTO_INSTALL}" "${IPPCRYPTO_VERSION}" lib/intel64/libippcp.so lib/libippcp.so; then
    echo "Intel IPP Crypto ${IPPCRYPTO_VERSION} already built, skipping."
else
    echo "=== Building Intel IPP Crypto (${IPPCRYPTO_VERSION}) ==="
    if ! command -v nasm &>/dev/null; then
        echo "ERROR: nasm is required to build Intel IPP Crypto."
        echo "Install with: sudo dnf install nasm"
        exit 1
    fi
    rm -f "${IPPCRYPTO_INSTALL}/.version"
    if [ ! -d "${IPPCRYPTO_DIR}" ]; then
        git clone --recursive --branch "${IPPCRYPTO_VERSION}" --depth 1 \
            https://github.com/intel/cryptography-primitives.git "${IPPCRYPTO_DIR}"
    fi
    checkout_release "${IPPCRYPTO_DIR}" "${IPPCRYPTO_VERSION}"
    git -C "${IPPCRYPTO_DIR}" submodule update --init --recursive
    cmake -S "${IPPCRYPTO_DIR}" -B "${IPPCRYPTO_DIR}/_build" \
        -DARCH=intel64 \
        -DCMAKE_INSTALL_PREFIX="${IPPCRYPTO_INSTALL}" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -fno-omit-frame-pointer" \
        -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -fno-omit-frame-pointer" \
        -DOPENSSL_ROOT_DIR="${OPENSSL4_INSTALL}"
    cmake --build "${IPPCRYPTO_DIR}/_build" --target ippcp_dyn -j "${NPROC}"
    mkdir -p "${IPPCRYPTO_INSTALL}/lib/intel64" "${IPPCRYPTO_INSTALL}/include"
    cp "${IPPCRYPTO_DIR}/_build/sources/ippcp"/libippcp.so* "${IPPCRYPTO_INSTALL}/lib/intel64/"
    cp "${IPPCRYPTO_DIR}/include"/*.h "${IPPCRYPTO_INSTALL}/include/"
    echo "${IPPCRYPTO_VERSION}" > "${IPPCRYPTO_INSTALL}/.version"
    echo "Intel IPP Crypto installed to ${IPPCRYPTO_INSTALL}"
fi

echo ""
echo "All dependencies built successfully."
echo "  OpenSSL 4.x: ${OPENSSL4_INSTALL}"
echo "  OpenSSL 3.x: ${OPENSSL3_INSTALL}"
echo "  GnuTLS:      ${GNUTLS_INSTALL}"
echo "  BoringSSL:   ${BORINGSSL_INSTALL}"
echo "  AWS-LC:      ${AWSLC_INSTALL}"
echo "  IPP Crypto:  ${IPPCRYPTO_INSTALL}"
