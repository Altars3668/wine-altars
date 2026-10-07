#!/usr/bin/env bash
# Build wine-altars from upstream Wine and the patch series in patches/altars-up/.
#
# This is the whole recipe -- fetch, apply, configure, build, install -- and the GitHub
# release workflow runs exactly this script, so a local build and a released build are
# the same build.
#
#   scripts/build-from-series.sh                    # all stages, installs into build/install
#   PREFIX=/opt/wine-altars scripts/build-from-series.sh
#   STAGES="fetch apply" scripts/build-from-series.sh   # only get a patched source tree
#
# Stages, in order:  fetch  apply  configure  build  install
#   fetch      shallow-clone upstream at the tag named in patches/altars-up/BASE
#   apply      git am the series on top of it (one commit per patch)
#   configure  out-of-tree configure, 64-bit host with a 32-bit PE half (new WoW64 mode)
#   build      make
#   install    make install into $PREFIX (or $DESTDIR$PREFIX)
#
# Environment (all optional):
#   WORK      scratch directory                      default: <repo>/build  (git-ignored)
#   SRC       where the Wine tree lives              default: $WORK/wine-src
#   BUILD     out-of-tree build directory            default: $SRC/build-wow64  (where scripts/build-probe.sh looks)
#   PREFIX    --prefix handed to configure           default: $WORK/install
#   DESTDIR   staging root for "make install"        default: empty
#   JOBS      parallel make jobs                     default: number of CPUs
#   ARCHS     --enable-archs                         default: i386,x86_64
#   UPSTREAM  git URL to fetch from                  default: UPSTREAM_MIRROR from BASE, then UPSTREAM_URL
#   OPT_CFLAGS compiler flags for both halves        default: -O2 -pipe   (no -g: smaller, faster build)
#   STRICT_DEPS=1  pass --with-<lib> for every library a desktop Wine needs, so configure
#                  stops with an error instead of quietly building a Wine without X11 or
#                  schannel.  default: 1
#   CONFIGURE_FLAGS  extra arguments for configure, appended last (e.g. "--without-wayland")
#   USE_CCACHE=1   put ccache's compiler wrappers first in PATH.   default: 0
#
# Needs: git, bison, flex, gettext tools, pkg-config, x86_64-w64-mingw32-gcc and
# i686-w64-mingw32-gcc (the "win32" thread-model variant) with their g++ siblings, and the
# -dev packages of the libraries in STRICT_DEPS.  See docs/building.md for the Debian/Ubuntu
# package list.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERIES="$ROOT/patches/altars-up"
WORK="${WORK:-$ROOT/build}"
SRC="${SRC:-$WORK/wine-src}"
BUILD="${BUILD:-$SRC/build-wow64}"
PREFIX="${PREFIX:-$WORK/install}"
DESTDIR="${DESTDIR:-}"
JOBS="${JOBS:-$(nproc)}"
ARCHS="${ARCHS:-i386,x86_64}"
OPT_CFLAGS="${OPT_CFLAGS:--O2 -pipe}"
STRICT_DEPS="${STRICT_DEPS:-1}"
USE_CCACHE="${USE_CCACHE:-0}"
STAGES="${STAGES:-fetch apply configure build install}"

say() { printf '\n==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
want() { case " $STAGES " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

[ -r "$SERIES/BASE" ] || die "no $SERIES/BASE"
patches=("$SERIES"/0*.patch)
[ -e "${patches[0]}" ] || die "no patches in $SERIES"
# shellcheck source=/dev/null
. "$SERIES/BASE"
UPSTREAM="${UPSTREAM:-}"

if [ "$USE_CCACHE" = 1 ]; then
    command -v ccache >/dev/null || die "USE_CCACHE=1 but ccache is not installed"
    # one wrapper directory first in PATH: every compiler named here goes through ccache, and
    # ccache finds the real one further down PATH
    ccbin="$WORK/ccache-bin"
    mkdir -p "$ccbin"
    for c in gcc g++ cc c++ x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ \
             i686-w64-mingw32-gcc i686-w64-mingw32-g++; do
        ln -sf "$(command -v ccache)" "$ccbin/$c"
    done
    PATH="$ccbin:$PATH"; export PATH
    echo "ccache: compilers wrapped from $ccbin"
fi

# ---------------------------------------------------------------------------------------
if want fetch; then
    say "fetch: upstream Wine $BASE_TAG"
    if [ -d "$SRC/.git" ]; then
        echo "    $SRC already holds a clone; leaving it alone"
    else
        mkdir -p "$(dirname "$SRC")"
        urls=()
        [ -n "$UPSTREAM" ] && urls+=("$UPSTREAM")
        urls+=("$UPSTREAM_MIRROR" "$UPSTREAM_URL")
        ok=0
        for u in "${urls[@]}"; do
            echo "    trying $u"
            if git clone --quiet --depth 1 --branch "$BASE_TAG" --no-tags "$u" "$SRC" 2>/dev/null; then
                ok=1; break
            fi
            rm -rf "$SRC"
        done
        [ $ok = 1 ] || die "could not clone $BASE_TAG from any of: ${urls[*]}"
    fi
    git -C "$SRC" cat-file -e "$BASE_COMMIT^{commit}" 2>/dev/null ||
        die "$SRC does not contain $BASE_COMMIT ($BASE_TAG), the commit the series applies to"
fi

# ---------------------------------------------------------------------------------------
if want apply; then
    say "apply: ${#patches[@]} patches"
    [ -d "$SRC/.git" ] || die "no Wine tree at $SRC (run the fetch stage)"
    n_done=$(git -C "$SRC" rev-list --count "$BASE_COMMIT..HEAD" 2>/dev/null || echo 0)
    n_all=${#patches[@]}
    if [ "$n_done" = "$n_all" ]; then
        echo "    already applied ($n_done commits above $BASE_TAG)"
    elif [ "$n_done" != 0 ]; then
        die "$SRC has $n_done of $n_all patches applied; remove it, or 'git am --abort' / 'git reset --hard $BASE_COMMIT'"
    else
        git -C "$SRC" -c user.name="wine-altars build" -c user.email="build@invalid" \
            -c core.autocrlf=false am --quiet --keep-cr --no-gpg-sign "${patches[@]}" ||
            die "git am stopped; see 'git -C $SRC am --show-current-patch'"
        echo "    $(git -C "$SRC" rev-list --count "$BASE_COMMIT..HEAD") commits on top of $BASE_TAG"
    fi
fi

# ---------------------------------------------------------------------------------------
if want configure; then
    say "configure (archs: $ARCHS)"
    [ -f "$SRC/configure" ] || die "no configure in $SRC"
    for t in bison flex pkg-config; do command -v "$t" >/dev/null || die "missing build tool: $t"; done
    IFS=, read -r -a archs <<<"$ARCHS"
    for a in "${archs[@]}"; do
        case $a in
            i386)   cc=i686-w64-mingw32-gcc ;;
            x86_64) cc=x86_64-w64-mingw32-gcc ;;
            *)      continue ;;
        esac
        command -v "$cc" >/dev/null || die "missing PE cross compiler: $cc (package gcc-mingw-w64)"
        model=$("$cc" -v 2>&1 | sed -n 's/^Thread model: //p')
        [ "$model" = win32 ] ||
            echo "    warning: $cc uses thread model '$model'; Wine wants the win32 variant (update-alternatives --set $cc /usr/bin/$cc-win32)"
        "${cc%gcc}g++" --version >/dev/null 2>&1 ||
            echo "    warning: no ${cc%gcc}g++: icu.dll and the other C++ modules will not be built"
    done

    flags=(--enable-archs="$ARCHS" --prefix="$PREFIX" --disable-tests)
    if [ "$STRICT_DEPS" = 1 ]; then
        # each of these makes configure fail, instead of warn, when the -dev package is missing
        flags+=(--with-x --with-xcomposite --with-xcursor --with-xfixes --with-xinerama --with-xinput
                --with-xinput2 --with-xrandr --with-xrender --with-xshape --with-xshm --with-xxf86vm
                --with-freetype --with-fontconfig --with-gnutls --with-vulkan --with-opengl
                --with-cups --with-pulse --with-alsa --with-udev --with-dbus
                --with-gstreamer --with-ffmpeg --with-krb5 --with-gssapi --with-wayland)
    fi
    # shellcheck disable=SC2206  # CONFIGURE_FLAGS is meant to be split on whitespace
    flags+=(${CONFIGURE_FLAGS:-})
    mkdir -p "$BUILD"
    ( cd "$BUILD" && CFLAGS="$OPT_CFLAGS" CROSSCFLAGS="$OPT_CFLAGS" "$SRC/configure" "${flags[@]}" ) ||
        die "configure failed; the last lines of $BUILD/config.log say which library is missing"
    if grep -q '^DISABLED_SUBDIRS.* dlls/icu ' "$BUILD/Makefile" 2>/dev/null; then
        echo "    note: icu.dll was not configured (no C++17 PE compiler); Office's runtime wants it" >&2
    fi
fi

# ---------------------------------------------------------------------------------------
if want build; then
    say "build: make -j$JOBS"
    [ -f "$BUILD/Makefile" ] || die "not configured: $BUILD/Makefile is missing"
    make -C "$BUILD" -j"$JOBS"
fi

# ---------------------------------------------------------------------------------------
if want install; then
    say "install: $DESTDIR$PREFIX"
    [ -f "$BUILD/Makefile" ] || die "not configured: $BUILD/Makefile is missing"
    make -C "$BUILD" install DESTDIR="$DESTDIR" >/dev/null
    bin="$DESTDIR$PREFIX/bin/wine"
    [ -x "$bin" ] || die "make install produced no $bin"
    "$bin" --version
    # the series has to be in the binaries, not only in the tree
    for d in srpapi secur32 kernelbase icu sppc; do
        for arch in x86_64-windows i386-windows; do
            f="$DESTDIR$PREFIX/lib/wine/$arch/$d.dll"
            case $arch,$ARCHS in i386-windows,*i386*|x86_64-windows,*x86_64*) ;; *) continue ;; esac
            printf '    %-24s %s\n' "$arch/$d.dll" "$([ -e "$f" ] && echo present || echo MISSING)"
        done
    done
fi
