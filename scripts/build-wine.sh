#!/usr/bin/env bash
# Configure, build and install wine-altars into dist/.
#
# Defaults to 64-bit only: every client this fork targets is x64, and a full
# multiarch build costs several times as much.  Set ARCHS=i386,x86_64 when a
# 32-bit program has to run in the prefix -- MathType and AxMath ship 32-bit
# binaries, and without WoW64 the loader cannot start them at all.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${SRC:-$ROOT/wine-src}"
BUILD="${BUILD:-$SRC/build64}"
PREFIX="${PREFIX:-$ROOT/dist}"
JOBS="${JOBS:-$(( $(nproc) / 2 ))}"      # wine's link steps are memory-hungry
ARCHS="${ARCHS:-x86_64}"

[ -d "$SRC" ] || { echo "no Wine tree at $SRC" >&2; exit 1; }

cd "$SRC"
# srpapi adds a Makefile.in, so configure has to be taught about it.
if ! grep -q enable_srpapi configure 2>/dev/null; then
    echo "==> regenerating configure (srpapi adds a makefile)"
    autoconf -o configure configure.ac
fi

mkdir -p "$BUILD"; cd "$BUILD"
if [ ! -f Makefile ]; then
    echo "==> configure --prefix=$PREFIX"
    ../configure --enable-archs="$ARCHS" --prefix="$PREFIX" --disable-tests
fi

echo "==> make -j$JOBS"
make -j"$JOBS"
echo "==> make install"
make install

"$PREFIX/bin/wine" --version
# Prove the patch series actually made it into the build, not just the tree.
for arch in ${ARCHS//,/ }; do
    case $arch in i386) win=i386-windows;; *) win=x86_64-windows;; esac
    for d in srpapi secur32 kernelbase; do
        f="$PREFIX/lib/wine/$win/$d.dll"
        printf '  %-14s %-16s %s\n' "$d.dll" "$win" "$([ -e "$f" ] && echo present || echo MISSING)"
    done
done
