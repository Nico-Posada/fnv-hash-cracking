#!/usr/bin/env bash
set -euxo pipefail

PREFIX="${FNVCRACK_DEPS:-/opt/fnvcrack-deps}"
BUILD_DIR="${FNVCRACK_BUILD_DIR:-/tmp/fnvcrack-deps-build}"
JOBS="${FNVCRACK_BUILD_JOBS:-$(nproc)}"

GMP_VERSION="${GMP_VERSION:-6.3.0}"
MPFR_VERSION="${MPFR_VERSION:-4.1.0}"
FLINT_VERSION="${FLINT_VERSION:-3.5.0}"

mkdir -p "$PREFIX" "$BUILD_DIR"
cd "$BUILD_DIR"

fetch() {
    local url="$1"
    local file="${url##*/}"

    if [ ! -f "$file" ]; then
        curl -fsSLO "$url"
    fi
}

build_gmp() {
    fetch "https://gmplib.org/download/gmp/gmp-$GMP_VERSION.tar.xz"
    rm -rf "gmp-$GMP_VERSION"
    tar -xf "gmp-$GMP_VERSION.tar.xz"

    cd "gmp-$GMP_VERSION"
    ./configure \
        --prefix="$PREFIX" \
        --enable-fat \
        --enable-shared=yes \
        --enable-static=no
    make -j"$JOBS"
    make install
    cd "$BUILD_DIR"
}

build_mpfr() {
    fetch "https://ftp.gnu.org/gnu/mpfr/mpfr-$MPFR_VERSION.tar.gz"
    rm -rf "mpfr-$MPFR_VERSION"
    tar -xzf "mpfr-$MPFR_VERSION.tar.gz"

    cd "mpfr-$MPFR_VERSION"
    ./configure \
        --prefix="$PREFIX" \
        --with-gmp="$PREFIX" \
        --enable-shared=yes \
        --enable-static=no
    make -j"$JOBS"
    make install
    cd "$BUILD_DIR"
}

build_flint() {
    fetch "https://github.com/flintlib/flint/releases/download/v$FLINT_VERSION/flint-$FLINT_VERSION.tar.gz"
    rm -rf "flint-$FLINT_VERSION"
    tar -xzf "flint-$FLINT_VERSION.tar.gz"

    cd "flint-$FLINT_VERSION"
    ./configure \
        --prefix="$PREFIX" \
        --enable-arch=x86_64 \
        --disable-assembly \
        --disable-avx2 \
        --disable-avx512 \
        --with-gmp="$PREFIX" \
        --with-mpfr="$PREFIX" \
        --disable-static \
        --disable-debug
    make -j"$JOBS"
    make install
    cd "$BUILD_DIR"
}

build_gmp
build_mpfr
build_flint

find "$PREFIX/lib" -maxdepth 1 -name "lib*.so*" -print
