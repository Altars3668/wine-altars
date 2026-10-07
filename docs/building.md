# Building wine-altars

Wine is not vendored here. The code is [`patches/altars-up/`](../patches/altars-up): a series of
patches on top of the upstream tag named in `patches/altars-up/BASE`. Building means: fetch that
tag, apply the series, build.

## One command

```sh
scripts/build-from-series.sh                      # fetch, apply, configure, build, install
```

By default everything happens under `build/` (git-ignored): the upstream clone in
`build/wine-src`, the out-of-tree build in `build/wine-src/build-wow64`, the install in `build/install`.
(That is where `scripts/build-probe.sh` looks for headers and import libraries, so the probes in
`tools/` can be built against it.)
The same script, with `PREFIX=/opt/wine-altars DESTDIR=…`, is what the release workflow runs, so a
local build and a released build are the same build.

| variable | default | meaning |
|---|---|---|
| `WORK` | `<repo>/build` | scratch directory |
| `SRC`, `BUILD`, `PREFIX`, `DESTDIR` | under `WORK` | where the tree, the build (inside `SRC`) and the install go |
| `JOBS` | all CPUs | `make -j`; the link steps are memory-hungry, halve it on small machines |
| `ARCHS` | `i386,x86_64` | `--enable-archs`. Both halves are needed: Microsoft's WebView2 installer is a 32-bit program |
| `STAGES` | all five | any of `fetch apply configure build install`, e.g. `STAGES="fetch apply"` for just a patched tree |
| `STRICT_DEPS` | `1` | pass `--with-<library>` for everything a desktop Wine needs, so a missing `-dev` package stops configure instead of quietly giving you a Wine without X11 or schannel. `0` to give that up |
| `CONFIGURE_FLAGS` | empty | extra arguments for `configure`, appended last (`--without-wayland`) |
| `OPT_CFLAGS` | `-O2 -pipe` | compiler flags for both halves; no `-g`, which halves the build and the install |
| `USE_CCACHE` | `0` | wrap the compilers in ccache |

The fetch stage takes the tag from GitHub's mirror of Wine (`UPSTREAM_MIRROR` in `BASE`) and falls
back to WineHQ's GitLab; `UPSTREAM=<url>` overrides both. The apply stage is `git am --keep-cr`; it
leaves one commit per patch, so `git log` in `build/wine-src` is the series.

## What it needs

A 64-bit Linux, about 6 GB of disk, and:

* a C compiler for the host, `bison`, `flex`, `gettext`, `pkg-config`;
* the MinGW-w64 cross compilers for both halves, **the win32 thread-model variant**, with their
  C++ drivers (Office's runtime loads `icu.dll`, which is C++):
  `x86_64-w64-mingw32-gcc`/`g++` and `i686-w64-mingw32-gcc`/`g++`;
* development packages for X11 and its extensions, Wayland, OpenGL/EGL, Vulkan, FreeType, fontconfig,
  GnuTLS, Kerberos, CUPS, PulseAudio, ALSA, udev, D-Bus, GStreamer and FFmpeg.

On Debian or Ubuntu 24.04 that is:

```sh
sudo apt-get install --no-install-recommends \
  build-essential git bison flex gettext pkg-config ccache xz-utils \
  gcc-mingw-w64 g++-mingw-w64 libwayland-bin wayland-protocols \
  libasound2-dev libavcodec-dev libavformat-dev libavutil-dev libcups2-dev libdbus-1-dev \
  libegl-dev libfontconfig-dev libfreetype-dev libgl-dev libgnutls28-dev \
  libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev libkrb5-dev libpulse-dev \
  libudev-dev libunwind-dev libusb-1.0-0-dev libv4l-dev libvulkan-dev libwayland-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxfixes-dev libxi-dev \
  libxinerama-dev libxkbcommon-dev libxkbregistry-dev libxrandr-dev libxrender-dev libxxf86vm-dev

# Debian and Ubuntu install MinGW-w64 with both thread models and pick "posix" by default;
# Wine wants "win32":
for t in x86_64 i686; do for l in gcc g++; do
  sudo update-alternatives --set $t-w64-mingw32-$l /usr/bin/$t-w64-mingw32-$l-win32
done; done
```

Other distributions: the same libraries under their own names. If `configure` stops with
`... development files not found ... This is an error since --with-xxx was requested`, that is
`STRICT_DEPS` doing its job: install the package, or pass `CONFIGURE_FLAGS=--without-xxx` if you
really do not want the feature. `scripts/build-from-series.sh` also warns when a PE compiler uses
the posix thread model or has no C++ driver.

## Installing

```sh
sudo env PREFIX=/opt/wine-altars scripts/build-from-series.sh      # or: PREFIX=$HOME/wine-altars, no sudo
export PATH=/opt/wine-altars/bin:$PATH
```

Wine finds its libraries relative to the loader, so the installed tree can be moved or tarred up.
Several Wine versions can live side by side; what must not happen is two different builds opening
the same prefix at the same time (they need the same `wineserver`).

`scripts/install-system-wine.sh` is the author's way of making this Wine the system's (a copy in
`/opt/wine-altars` linked from `/usr/local/bin`, with `--rollback` and `--uninstall`); read it
before using it, it uses `sudo`.

## A prefix keeps its own copies of Wine's DLLs

Since Wine 9 a prefix holds copies of the builtin DLLs in `C:\windows\system32` and loads those.
`wineboot` refreshes them when the Wine *version string* changes, which it does between releases
but not between two local rebuilds of the same checkout. After a rebuild, before measuring
anything, run `scripts/sync-prefix-dlls.sh` (it replaces only files that are still Wine's own;
Office puts deliberate native copies of some DLLs there). A stale copy and a wrong conclusion look
identical in the logs.

## The release workflow

[`.github/workflows/release.yml`](../.github/workflows/release.yml) installs the dependencies
above on an Ubuntu 24.04 runner, runs the script with ccache, smoke-tests the result (a fresh
prefix under Xvfb, `cmd` as a 64-bit and as a 32-bit program, a registry query), and uploads:

| asset | content |
|---|---|
| `wine-altars-linux-x86_64.tar.xz` | the install tree, top directory `wine-altars/`, with `RELEASE.txt` giving version and commit |
| `wine-altars-patches.tar.xz` | `patches/altars-up/` on its own |
| `SHA256SUMS` | checksums of the two |

It runs for a pushed tag `v*` (release named after the tag), for pushes to `main` that touch the
series or the recipe (the rolling `nightly` pre-release), and by hand from the *Actions* tab
(optionally with a tag name). A tag that already has a release gets its assets replaced.

To cut a release: `git tag v11.19-altars.2 && git push origin v11.19-altars.2`. Tags are
`v<upstream version>-altars.<n>`.

## Changing the series

The commits live in a Wine tree; `scripts/export-series.sh` turns the branch into the files in
`patches/altars-up/`, applies them to the base in a scratch index and refuses to write anything
unless the result is byte-for-byte the branch's tree. To move to a newer Wine: rebase the branch,
change `BASE`, run the script. To contribute a patch without a Wine tree: add `0579-….patch`
(output of `git format-patch -1`) at the end; CI checks that the whole series still applies.
