#!/bin/bash
# build-winetest.sh <dll> <outdir>
#
# Builds dlls/<dll>/tests as one standalone 64-bit test executable against the build tree, for a
# tree configured with --disable-tests: <outdir>/<dll>_test.exe runs under the tree's wine as
#
#   build-wow64/wine <outdir>/<dll>_test.exe <test file>
#
# and on Windows through scripts/winrun.sh.  It needs dlls/<dll>/tests/testlist.c in the build
# tree, which makedep writes for every tests directory configure.ac lists; a new one needs its
# WINE_CONFIG_MAKEFILE line and autoconf, then any make.
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="${WINE_BUILD:-$ROOT/wine-src-up/build-wow64}"
[ -d "$B" ] || B="$ROOT/build/wine-src/build-wow64"     # the tree scripts/build-from-series.sh makes
dll=$1; O=$(realpath -m "$2"); mkdir -p "$O"
cd $B
# upstream moved winecrt0 from dlls/ to libs/, and links PE modules with the bundled compiler-rt;
# take whichever this tree has
crt0=libs/winecrt0/x86_64-windows/libwinecrt0.a; [ -f $crt0 ] || crt0=dlls/winecrt0/x86_64-windows/libwinecrt0.a
rtlib=libs/compiler-rt/x86_64-windows/libcompiler-rt.a; [ -f $rtlib ] || rtlib=
# a variable of the tests' Makefile.in
makevar() { sed -n "s/^$1 *= *//p" ../dlls/$dll/tests/Makefile.in; }
imports=$(grep '^IMPORTS' ../dlls/$dll/tests/Makefile.in | cut -d= -f2)
# the C runtime the tests import, msvcrt when they name none, as makedep has it (msvcrt's tests look into its
# FILE, ucrtbase's want the conforming wcstok); it is linked after winecrt0, which needs it too
crtimp=$(for i in $imports; do case $i in ucrtbase|msvcr*) echo $i;; esac; done | head -1)
crtimp=${crtimp:-msvcrt}
crtlib=dlls/$crtimp/x86_64-windows/lib$crtimp.a
case $crtimp in
ucrt*) crtdef=-D_UCRT;;
msvcrt) crtdef=-D_MSVCR_VER=0;;
*) crtdef=-D_MSVCR_VER=${crtimp#msvcr};;
esac
# a C runtime's own tests are built without the compiler's builtins, which would answer for strcmp or log10
nobuiltin=; case $dll in msvcr*|ucrt*) nobuiltin=-fno-builtin;; esac
FLAGS="-Idlls/$dll/tests -I../dlls/$dll/tests -Iinclude -I../include -I../include/msvcrt $crtdef -D__WINESRC__ -D__WINE_PE_BUILD -Wall -fno-strict-aliasing -Wno-packed-not-aligned -mlong-double-64 -mcx16 -mcmodel=small -g -O2 $nobuiltin $(makevar EXTRADEFS)"
list=$(sed -n '/^SOURCES/,/^$/p' ../dlls/$dll/tests/Makefile.in)
srcs=$(echo "$list" | grep -oE '[A-Za-z0-9_.]+\.c\b')
# A source marked "#pragma makedep testdll" is a helper module the tests load from a TESTDLL resource (ole32's
# testlib.dll, ntoskrnl's drivers, user32's hook dll): it is linked on its own and embedded, as makedep does.
testdlls=
for f in $srcs; do
    grep -q '^#pragma makedep .*testdll' ../dlls/$dll/tests/$f && testdlls="$testdlls ${f%.c}"
done
for t in $testdlls; do srcs=$(echo "$srcs" | grep -vxF "$t.c"); done
# one with a .spec of the same name but no such mark is not built, and what needs it fails or skips
for spec in $(echo "$list" | grep -oE '[A-Za-z0-9_.]+\.spec\b'); do
    srcs=$(echo "$srcs" | grep -vxF "${spec%.spec}.c")
done
stubs=
idlres=
proxies=
# each IDL file gives what its "#pragma makedep" lines ask for, widl choosing the output by its name as it does
# for makedep, and with the Makefile.in's EXTRAIDLFLAGS and <name>_EXTRAIDLFLAGS (rpcrt4's server tests
# prefix their stubs' names); the header always
for idl in $(sed -n '/^SOURCES/,/^$/p' ../dlls/$dll/tests/Makefile.in | grep -o '[a-z0-9_.]*\.idl'); do
    base=${idl%.idl}
    widl_idl() {
        tools/widl/widl -m64 --nostdinc -Ldlls/\* -Iinclude -I../include -I../dlls/$dll/tests -D__WINESRC__ \
            $(makevar EXTRAIDLFLAGS) $(makevar ${base}_EXTRAIDLFLAGS) "$@" ../dlls/$dll/tests/$idl
    }
    widl_idl -o $O/$base.h
    pragmas=$(sed -n 's/^#pragma makedep //p' ../dlls/$dll/tests/$idl)
    for output in client:_c.c server:_s.c proxy:_p.c ident:_i.c; do
        echo " $pragmas " | tr '\n' ' ' | grep -qw "${output%%:*}" || continue
        widl_idl -o $O/$base${output#*:}
        stubs="$stubs $base${output#*:}"
    done
    echo " $pragmas " | tr '\n' ' ' | grep -qw proxy && proxies="$proxies $idl"
    # the type library it describes, as a resource (makedep's typelib)
    if echo " $pragmas " | tr '\n' ' ' | grep -qw typelib; then
        widl_idl -o $O/${base}_l.res
        idlres="$idlres $O/${base}_l.res"
    fi
done
# the proxies' list for the module, as makedep writes it
if [ -n "$proxies" ]; then
    tools/widl/widl --dlldata-only -o $O/dlldata.c $proxies
    stubs="$stubs dlldata.c"
fi
FLAGS="$FLAGS -I$O"
# the tests' resources (user32's menus and dialogs, for one), compiled as makedep has wrc compile them
res=$idlres
for rc in $(echo "$list" | grep -oE '[A-Za-z0-9_.]+\.rc\b'); do
    tools/wrc/wrc -u -o $O/${rc%.rc}.res --nostdinc -I$O -Idlls/$dll/tests -I../dlls/$dll/tests -Iinclude -I../include \
        -I../include/msvcrt -D_MSVCR_VER=0 -D__WINESRC__ ../dlls/$dll/tests/$rc
    res="$res $O/${rc%.rc}.res"
done
# never a delay-import library (libX.delay.a): a module the tests import that way is not loaded until its
# first call, and GetModuleHandle() on it at the start of a test answers NULL
import_libs() {
    local d f
    for d in "$@"; do f=dlls/$d/x86_64-windows/lib$d.a; [ -f $f ] || f=libs/$d/x86_64-windows/lib$d.a; [ -f $f ] || f=$(ls dlls/$d/x86_64-windows/*.a 2>/dev/null | /usr/bin/grep -v '\.delay\.a$' | head -1); [ -n "$f" ] || f=$(ls dlls/*/x86_64-windows/lib$d.a 2>/dev/null | head -1); [ -n "$f" ] && echo $f; done
}
defaults="$crt0 $rtlib $crtlib dlls/kernel32/x86_64-windows/libkernel32.a dlls/ntdll/x86_64-windows/libntdll.a"
for t in $testdlls; do
    timports=$(sed -n "s/^${t}_IMPORTS *= *//p" ../dlls/$dll/tests/Makefile.in); [ -n "$timports" ] || timports=$imports
    tflags=$(sed -n "s/^${t}_EXTRADLLFLAGS *= *//p" ../dlls/$dll/tests/Makefile.in)
    ext=.dll; link=-shared; case " $tflags " in *" -mconsole "*) ext=.exe; link=;; esac
    # a module without the default libraries has no C runtime of its own (a driver takes ntoskrnl's)
    tdefaults=$defaults; tcflags=$FLAGS
    case " $tflags " in *" -nodefaultlibs "*) tdefaults=$rtlib; tcflags=${FLAGS/$crtdef/-D_MSVCR_VER=0};; esac
    x86_64-w64-mingw32-gcc -c -o $O/$t.o ../dlls/$dll/tests/$t.c $tcflags
    spec=; [ -f ../dlls/$dll/tests/$t.spec ] && spec=../dlls/$dll/tests/$t.spec
    fname=; case $t in *.*) fname=-Wb,-F,$t$ext;; esac
    tools/winegcc/winegcc -o $O/$t$ext --wine-objdir . -b x86_64-w64-mingw32 $tflags $link $spec $fname $O/$t.o \
        $(import_libs $timports) $tdefaults -s
    # wrc opens the file by a relative name only
    (cd $O && echo "$t$ext TESTDLL \"$t$ext\"" | "$B/tools/wrc/wrc" -u -o $t$ext.res)
    res="$res $O/$t$ext.res"
done
objs=
for f in $srcs; do
    x86_64-w64-mingw32-gcc -c -o $O/${f%.c}.o ../dlls/$dll/tests/$f $FLAGS
    objs="$objs $O/${f%.c}.o"
done
for f in $stubs; do
    x86_64-w64-mingw32-gcc -c -o $O/${f%.c}.o $O/$f $FLAGS
    objs="$objs $O/${f%.c}.o"
done
x86_64-w64-mingw32-gcc -c -o $O/testlist.o dlls/$dll/tests/testlist.c $FLAGS
tools/winegcc/winegcc -o $O/${dll}_test.exe --wine-objdir . -b x86_64-w64-mingw32 -mconsole \
    $objs $O/testlist.o $res $(import_libs $imports) $defaults
echo built $O/${dll}_test.exe
