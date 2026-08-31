#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY="${SCRIPT_DIR}/../third_party"
INSTALL_DIR="${THIRD_PARTY}/install"

NPROC=$(nproc 2>/dev/null || echo 4)

OPENSSL4_VERSION="openssl-4.0.1"
OPENSSL3_VERSION="openssl-3.6.3"
BORINGSSL_VERSION="0.20260803.0"
AWSLC_VERSION="v5.5.0"
LEANCRYPTO_VERSION="v1.8.0"
GNUTLS_VERSION="3.8.13"
IPPCRYPTO_VERSION="cryptography-primitives_2_2_0"

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

# --- OpenSSL 4.0.1 ---
OPENSSL4_DIR="${THIRD_PARTY}/openssl4"
OPENSSL4_INSTALL="${INSTALL_DIR}/openssl4"

if [ -f "${OPENSSL4_INSTALL}/lib64/libcrypto.so" ] || [ -f "${OPENSSL4_INSTALL}/lib/libcrypto.so" ]; then
    echo "OpenSSL 4.x already built, skipping."
else
    echo "=== Building OpenSSL 4.x (${OPENSSL4_VERSION}) ==="
    if [ ! -d "${OPENSSL4_DIR}" ]; then
        git clone --branch "${OPENSSL4_VERSION}" --depth 1 \
            https://github.com/openssl/openssl.git "${OPENSSL4_DIR}"
    fi
    cd "${OPENSSL4_DIR}"
    ./Configure --prefix="${OPENSSL4_INSTALL}" shared -O3 ${PROFILE_CFLAGS}
    make -j"${NPROC}"
    make install_sw
    echo "OpenSSL 4.x installed to ${OPENSSL4_INSTALL}"
fi

# --- OpenSSL 3.6.3 ---
OPENSSL3_DIR="${THIRD_PARTY}/openssl3"
OPENSSL3_INSTALL="${INSTALL_DIR}/openssl3"

if [ -f "${OPENSSL3_INSTALL}/lib64/libcrypto.so" ] || [ -f "${OPENSSL3_INSTALL}/lib/libcrypto.so" ]; then
    echo "OpenSSL 3.x already built, skipping."
else
    echo "=== Building OpenSSL 3.x (${OPENSSL3_VERSION}) ==="
    if [ ! -d "${OPENSSL3_DIR}" ]; then
        git clone --branch "${OPENSSL3_VERSION}" --depth 1 \
            https://github.com/openssl/openssl.git "${OPENSSL3_DIR}"
    fi
    cd "${OPENSSL3_DIR}"
    ./Configure --prefix="${OPENSSL3_INSTALL}" shared -O3 ${PROFILE_CFLAGS}
    make -j"${NPROC}"
    make install_sw
    echo "OpenSSL 3.x installed to ${OPENSSL3_INSTALL}"
fi

# --- leancrypto 1.8.0 (PQC backend for GnuTLS) ---
LEANCRYPTO_DIR="${THIRD_PARTY}/leancrypto"
LEANCRYPTO_INSTALL="${INSTALL_DIR}/leancrypto"

if [ -f "${LEANCRYPTO_INSTALL}/lib64/libleancrypto.so" ] || [ -f "${LEANCRYPTO_INSTALL}/lib/libleancrypto.so" ]; then
    echo "leancrypto already built, skipping."
else
    echo "=== Building leancrypto ${LEANCRYPTO_VERSION} ==="
    if ! command -v meson &>/dev/null; then
        echo "ERROR: meson is required to build leancrypto. Install with: pip install --user meson ninja"
        exit 1
    fi
    if [ ! -d "${LEANCRYPTO_DIR}" ]; then
        git clone --branch "${LEANCRYPTO_VERSION}" --depth 1 \
            https://github.com/smuellerDD/leancrypto.git "${LEANCRYPTO_DIR}"
    fi
    cd "${LEANCRYPTO_DIR}"
    CFLAGS="${PROFILE_CFLAGS}" CXXFLAGS="${PROFILE_CXXFLAGS}" \
    meson setup builddir --prefix="${LEANCRYPTO_INSTALL}" --default-library=shared \
        -Dbuildtype=release -Db_ndebug=true
    meson compile -C builddir
    meson install -C builddir
    echo "leancrypto installed to ${LEANCRYPTO_INSTALL}"
fi

# --- GnuTLS 3.8.13 (with leancrypto for PQC) ---
GNUTLS_TARBALL="gnutls-${GNUTLS_VERSION}.tar.xz"
GNUTLS_SRC_DIR="${THIRD_PARTY}/gnutls-${GNUTLS_VERSION}"
GNUTLS_INSTALL="${INSTALL_DIR}/gnutls"

if [ -f "${GNUTLS_INSTALL}/lib/libgnutls.so" ] || [ -f "${GNUTLS_INSTALL}/lib64/libgnutls.so" ]; then
    echo "GnuTLS already built, skipping."
else
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
    echo "GnuTLS installed to ${GNUTLS_INSTALL}"
fi

# --- BoringSSL ---
BORINGSSL_DIR="${THIRD_PARTY}/boringssl"
BORINGSSL_INSTALL="${INSTALL_DIR}/boringssl"

if [ -f "${BORINGSSL_INSTALL}/lib/libcrypto.so" ]; then
    echo "BoringSSL already built, skipping."
else
    echo "=== Building BoringSSL ${BORINGSSL_VERSION} ==="
    if [ ! -d "${BORINGSSL_DIR}" ]; then
        git clone --branch "${BORINGSSL_VERSION}" --depth 1 \
            https://boringssl.googlesource.com/boringssl "${BORINGSSL_DIR}"
    fi
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
    echo "BoringSSL installed to ${BORINGSSL_INSTALL}"
fi

# --- AWS-LC ---
AWSLC_DIR="${THIRD_PARTY}/aws-lc"
AWSLC_INSTALL="${INSTALL_DIR}/aws-lc"

if [ -f "${AWSLC_INSTALL}/lib/libcrypto.so" ] || [ -f "${AWSLC_INSTALL}/lib64/libcrypto.so" ]; then
    echo "AWS-LC already built, skipping."
else
    echo "=== Building AWS-LC ${AWSLC_VERSION} ==="
    if [ ! -d "${AWSLC_DIR}" ]; then
        git clone --branch "${AWSLC_VERSION}" --depth 1 \
            https://github.com/aws/aws-lc.git "${AWSLC_DIR}"
    fi
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
    echo "AWS-LC installed to ${AWSLC_INSTALL}"
fi

# --- Intel IPP Crypto (Cryptography Primitives Library) ---
IPPCRYPTO_DIR="${THIRD_PARTY}/ipp-crypto"
IPPCRYPTO_INSTALL="${INSTALL_DIR}/ipp-crypto"

if [ -f "${IPPCRYPTO_INSTALL}/lib/intel64/libippcp.so" ] || [ -f "${IPPCRYPTO_INSTALL}/lib/libippcp.so" ]; then
    echo "Intel IPP Crypto already built, skipping."
else
    echo "=== Building Intel IPP Crypto (${IPPCRYPTO_VERSION}) ==="
    if ! command -v nasm &>/dev/null; then
        echo "ERROR: nasm is required to build Intel IPP Crypto."
        echo "Install with: sudo dnf install nasm"
        exit 1
    fi
    if [ ! -d "${IPPCRYPTO_DIR}" ]; then
        git clone --recursive --branch "${IPPCRYPTO_VERSION}" --depth 1 \
            https://github.com/intel/cryptography-primitives.git "${IPPCRYPTO_DIR}"
    fi
    cmake -S "${IPPCRYPTO_DIR}" -B "${IPPCRYPTO_DIR}/_build" \
        -DARCH=intel64 \
        -DCMAKE_INSTALL_PREFIX="${IPPCRYPTO_INSTALL}" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -fno-omit-frame-pointer" \
        -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -fno-omit-frame-pointer" \
        -DOPENSSL_ROOT_DIR="${OPENSSL4_INSTALL}"
    cmake --build "${IPPCRYPTO_DIR}/_build" --target ippcp -j "${NPROC}"
    mkdir -p "${IPPCRYPTO_INSTALL}/lib/intel64" "${IPPCRYPTO_INSTALL}/include"
    cp "${IPPCRYPTO_DIR}/_build/sources/ippcp"/libippcp.so* "${IPPCRYPTO_INSTALL}/lib/intel64/"
    cp "${IPPCRYPTO_DIR}/include"/*.h "${IPPCRYPTO_INSTALL}/include/"
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
