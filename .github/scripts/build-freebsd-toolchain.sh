#!/bin/bash
# Reproducible x86_64-freebsd12.3 cross-compiler, built the same way documented by
# the upstream maintainer at https://github.com/philippe44/cross-compiling/blob/master/freebsd/build.sh
#
# Unlike that script (which assumes a private, pre-made sysroot tarball), this one
# builds the sysroot itself from FreeBSD's official release archive, so the whole
# toolchain is reproducible from public sources only.
#
# Usage: build-freebsd-toolchain.sh <freebsd-release> <prefix>
#   e.g. build-freebsd-toolchain.sh 12.3 /opt/x86_64-freebsd12.3
set -euo pipefail

FBSD_RELEASE="${1:?freebsd release required, e.g. 12.3}"
PREFIX="${2:?install prefix required}"
TARGET="x86_64-cross-freebsd${FBSD_RELEASE}"
SYSROOT="${PREFIX}/${TARGET}"

BINUTILS_VER=2.40
GMP_VER=6.2.1
MPFR_VER=4.1.0
MPC_VER=1.2.1
LIBTOOL_VER=2.4.6
GCC_VER=11.2.0

JOBS="$(nproc)"
WORK="$(mktemp -d)"
cd "$WORK"

echo "== Fetching official FreeBSD ${FBSD_RELEASE}-RELEASE base.txz for sysroot =="
BASE_URL="https://archive.freebsd.org/old-releases/amd64/${FBSD_RELEASE}-RELEASE/base.txz"
curl -fL -o base.txz "$BASE_URL"

mkdir -p "$SYSROOT"
# Only the pieces a GCC/binutils cross-compiler and this project's build actually need.
tar xf base.txz -C "$SYSROOT" ./usr/lib ./usr/lib32 ./usr/include ./lib

echo "== Rewriting absolute symlinks to stay inside the sysroot =="
# base.txz symlinks point at absolute paths like /lib/libc.so.7; left as-is they'd
# escape the sysroot and resolve against the build host instead of FreeBSD 12.3.
find "$SYSROOT" -type l | while read -r link; do
  target="$(readlink "$link")"
  case "$target" in
    /*) ln -sf "${SYSROOT}${target}" "$link" ;;
  esac
done

mkdir -p "$PREFIX/bin"
export PATH="$PREFIX/bin:$PATH"

fetch() {
  local url="$1"
  curl -fL -O "$url"
}

echo "== binutils ${BINUTILS_VER} =="
fetch "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.xz"
tar xf "binutils-${BINUTILS_VER}.tar.xz"
pushd "binutils-${BINUTILS_VER}"
./configure --enable-libssp --enable-ld --target="$TARGET" --prefix="$PREFIX" --with-sysroot="$SYSROOT"
make -j"$JOBS"
make install
popd

echo "== gmp ${GMP_VER} =="
fetch "https://ftp.gnu.org/gnu/gmp/gmp-${GMP_VER}.tar.xz"
tar xf "gmp-${GMP_VER}.tar.xz"
pushd "gmp-${GMP_VER}"
./configure --prefix="$PREFIX" --enable-shared --enable-static --enable-fft --enable-cxx --host="$TARGET" --build="$(./config.guess)"
make -j"$JOBS"
make install
popd

echo "== mpfr ${MPFR_VER} =="
fetch "https://ftp.gnu.org/gnu/mpfr/mpfr-${MPFR_VER}.tar.xz"
tar xf "mpfr-${MPFR_VER}.tar.xz"
pushd "mpfr-${MPFR_VER}"
./configure --prefix="$PREFIX" --with-gnu-ld --enable-static --enable-shared --with-gmp="$PREFIX" --host="$TARGET" --build="$(../gmp-${GMP_VER}/config.guess 2>/dev/null || gcc -dumpmachine)"
make -j"$JOBS"
make install
popd

echo "== mpc ${MPC_VER} =="
fetch "https://ftp.gnu.org/gnu/mpc/mpc-${MPC_VER}.tar.gz"
tar xf "mpc-${MPC_VER}.tar.gz"
pushd "mpc-${MPC_VER}"
./configure --prefix="$PREFIX" --with-gnu-ld --enable-static --enable-shared --with-gmp="$PREFIX" --with-mpfr="$PREFIX" --host="$TARGET" --build="$(gcc -dumpmachine)"
make -j"$JOBS"
make install
popd

echo "== libtool ${LIBTOOL_VER} =="
fetch "https://ftpmirror.gnu.org/libtool/libtool-${LIBTOOL_VER}.tar.gz"
tar xf "libtool-${LIBTOOL_VER}.tar.gz"
pushd "libtool-${LIBTOOL_VER}"
./configure --prefix="$PREFIX" --enable-static --enable-shared --host="$TARGET" --with-sysroot="$SYSROOT" --program-prefix="${TARGET}-" --build="$(gcc -dumpmachine)"
make -j"$JOBS"
make install
popd

echo "== gcc ${GCC_VER} =="
fetch "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz"
tar xf "gcc-${GCC_VER}.tar.xz"
pushd "gcc-${GCC_VER}"
mkdir -p build && cd build
../configure --without-headers --with-gnu-as --with-gnu-ld --disable-nls \
  --enable-languages=c,c++ --enable-libssp --enable-ld --disable-libitm \
  --disable-libquadmath --target="$TARGET" --prefix="$PREFIX" \
  --with-gmp="$PREFIX" --with-mpc="$PREFIX" --with-mpfr="$PREFIX" \
  --disable-libgomp --with-sysroot="$SYSROOT" --with-build-sysroot="$SYSROOT"
make -j"$JOBS"
make install
popd

rm -rf "$WORK"
echo "== Toolchain installed at $PREFIX =="
"${PREFIX}/bin/${TARGET}-gcc" --version
