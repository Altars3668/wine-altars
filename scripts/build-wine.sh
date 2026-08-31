#!/usr/bin/env bash
# Configure, build and install wine-altars into dist/.
#
# 64-bit only on purpose: every client this fork targets is x64, and a full
# multiarch build costs several times as much for nothing.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${SRC:-$ROOT/wine-src}"
BUILD="${BUILD:-$SRC/build64}"
PREFIX="${PREFIX:-$ROOT/dist}"
JOBS="${JOBS:-$(( $(nproc) / 2 ))}"      # wine's link steps are memory-hungry

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
    ../configure --enable-archs=x86_64 --prefix="$PREFIX" --disable-tests
fi

echo "==> make -j$JOBS"
make -j"$JOBS"
echo "==> make install"
make install

"$PREFIX/bin/wine" --version
# Prove the patch series actually made it into the build, not just the tree.
for d in srpapi secur32 kernelbase; do
    f="$PREFIX/lib/wine/x86_64-windows/$d.dll"
    printf '  %-14s %s\n' "$d.dll" "$([ -e "$f" ] && echo present || echo MISSING)"
done
