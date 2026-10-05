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
GCC_VER=11.2.0

# The GH-hosted runner's config.guess reports CPU-specific triplets (e.g.
# "nehalem-pc-linux-gnu") that the config.sub bundled with some of these
# (older) tarballs doesn't recognize. Pin an explicit, generic build triplet
# everywhere to sidestep that rather than relying on autodetection.
BUILD_TRIPLET=x86_64-pc-linux-gnu

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
./configure --build="$BUILD_TRIPLET" --enable-libssp --enable-ld --target="$TARGET" --prefix="$PREFIX" --with-sysroot="$SYSROOT"
make -j"$JOBS"
make install
popd

# gmp/mpfr/mpc are host-side dependencies: gcc links against them to run ON the
# build machine (it generates FreeBSD code, but the compiler binary itself is a
# Linux executable), so these are native builds, not cross-compiled to $TARGET.
echo "== gmp ${GMP_VER} (native, used by the gcc build) =="
fetch "https://ftp.gnu.org/gnu/gmp/gmp-${GMP_VER}.tar.xz"
tar xf "gmp-${GMP_VER}.tar.xz"
pushd "gmp-${GMP_VER}"
./configure --build="$BUILD_TRIPLET" --prefix="$PREFIX" --enable-shared --enable-static --enable-fft --enable-cxx
make -j"$JOBS"
make install
popd

echo "== mpfr ${MPFR_VER} (native) =="
fetch "https://ftp.gnu.org/gnu/mpfr/mpfr-${MPFR_VER}.tar.xz"
tar xf "mpfr-${MPFR_VER}.tar.xz"
pushd "mpfr-${MPFR_VER}"
./configure --build="$BUILD_TRIPLET" --prefix="$PREFIX" --with-gnu-ld --enable-static --enable-shared --with-gmp="$PREFIX"
make -j"$JOBS"
make install
popd

echo "== mpc ${MPC_VER} (native) =="
fetch "https://ftp.gnu.org/gnu/mpc/mpc-${MPC_VER}.tar.gz"
tar xf "mpc-${MPC_VER}.tar.gz"
pushd "mpc-${MPC_VER}"
./configure --build="$BUILD_TRIPLET" --prefix="$PREFIX" --with-gnu-ld --enable-static --enable-shared --with-gmp="$PREFIX" --with-mpfr="$PREFIX"
make -j"$JOBS"
make install
popd

echo "== gcc ${GCC_VER} =="
fetch "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz"
tar xf "gcc-${GCC_VER}.tar.xz"
pushd "gcc-${GCC_VER}"
mkdir -p build && cd build
../configure --build="$BUILD_TRIPLET" --without-headers --with-gnu-as --with-gnu-ld --disable-nls \
  --enable-languages=c,c++ --enable-libssp --enable-ld --disable-libitm \
  --disable-libquadmath --disable-multilib --target="$TARGET" --prefix="$PREFIX" \
  --with-gmp="$PREFIX" --with-mpc="$PREFIX" --with-mpfr="$PREFIX" \
  --disable-libgomp --with-sysroot="$SYSROOT" --with-build-sysroot="$SYSROOT"
make -j"$JOBS"
make install
popd

rm -rf "$WORK"
echo "== Toolchain installed at $PREFIX =="
"${PREFIX}/bin/${TARGET}-gcc" --version
