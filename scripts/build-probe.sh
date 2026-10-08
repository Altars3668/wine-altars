#!/bin/bash
# build-probe.sh <src.c> <out.exe> <import dll | resource.res>...
#
# Builds a single-file probe against the tree's own headers and import libraries, as a
# Wine test is built, giving a PE that runs both on Windows and under Wine: run it on
# each (scripts/winrun.sh for Windows) and diff the two outputs.
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="${WINE_BUILD:-$ROOT/wine-src-up/build-wow64}"
[ -d "$B" ] || B="$ROOT/build/wine-src/build-wow64"     # the tree scripts/build-from-series.sh makes
src=$(realpath "$1"); out=$(realpath -m "$2"); shift 2
cd "$B"
# upstream moved winecrt0 from dlls/ to libs/, and links PE modules with the bundled compiler-rt;
# take whichever this tree has
crt0=libs/winecrt0/x86_64-windows/libwinecrt0.a; [ -f $crt0 ] || crt0=dlls/winecrt0/x86_64-windows/libwinecrt0.a
rtlib=libs/compiler-rt/x86_64-windows/libcompiler-rt.a; [ -f $rtlib ] || rtlib=
FLAGS="-Iinclude -I../include -I../include/msvcrt -I$(dirname "$src") -D_UCRT -D__WINESRC__ -D__WINE_PE_BUILD -Wall -fno-strict-aliasing -mcx16 -g -O1"
x86_64-w64-mingw32-gcc -c -o "$out.o" "$src" $FLAGS
libs=
for d in "$@"; do
    case "$d" in *.res) libs="$libs $(realpath "$OLDPWD/$d" 2>/dev/null || realpath "$d")"; continue;; esac
    f=dlls/$d/x86_64-windows/lib$d.a
    [ -f "$f" ] || f=libs/$d/x86_64-windows/lib$d.a
    [ -f "$f" ] || f=$(ls dlls/$d/x86_64-windows/*.a 2>/dev/null | head -1)
    [ -n "$f" ] && libs="$libs $f"
done
tools/winegcc/winegcc -o "$out" --wine-objdir . -b x86_64-w64-mingw32 -mconsole "$out.o" $libs \
    $crt0 $rtlib dlls/ucrtbase/x86_64-windows/libucrtbase.a \
    dlls/kernel32/x86_64-windows/libkernel32.a dlls/ntdll/x86_64-windows/libntdll.a
rm -f "$out.o"
echo "built $out"
