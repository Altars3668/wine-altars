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

# Wine builds its C++ modules -- icu.dll, which Office's runtime and its React Native engine load for time
# zones and Intl, the bundled libc++, dmsynth -- only with a C++17 PE compiler, and configure skips them
# without a word when there is none.  The mingw gcc driver finds cc1plus itself once g++-mingw-w64 is
# installed; a copy unpacked elsewhere (dpkg -x of g++-mingw-w64-{x86-64,i686}-win32, the same version as
# gcc-mingw-w64) is found through -B.
GXX="${GXX:-$HOME/.local/opt/mingw-w64-gxx-13-win32}"
cxx=()
for triple in x86_64-w64-mingw32 i686-w64-mingw32; do
    [ -d "$GXX/usr/lib/gcc/$triple" ] || continue
    ver=$(ls "$GXX/usr/lib/gcc/$triple" | head -1)
    case $triple in x86_64*) var=x86_64_CXX;; *) var=i386_CXX;; esac
    cxx+=("$var=$triple-gcc -B$GXX/usr/lib/gcc/$triple/$ver/")
done

mkdir -p "$BUILD"; cd "$BUILD"
if [ ! -f Makefile ]; then
    echo "==> configure --prefix=$PREFIX ${cxx[*]}"
    ../configure --enable-archs="$ARCHS" --prefix="$PREFIX" --disable-tests "${cxx[@]}"
fi
if grep -q '^DISABLED_SUBDIRS.* dlls/icu ' Makefile; then
    echo "==> note: no C++17 PE compiler, icu.dll and the other C++ modules are not built" >&2
fi

echo "==> make -j$JOBS"
make -j"$JOBS"
echo "==> make install"
make install

"$PREFIX/bin/wine" --version
# Prove the patch series actually made it into the build, not just the tree.
for arch in ${ARCHS//,/ }; do
    case $arch in i386) win=i386-windows;; *) win=x86_64-windows;; esac
    for d in srpapi secur32 kernelbase icu; do
        f="$PREFIX/lib/wine/$win/$d.dll"
        printf '  %-14s %-16s %s\n' "$d.dll" "$win" "$([ -e "$f" ] && echo present || echo MISSING)"
    done
done
