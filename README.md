# wine-altars

**Microsoft 365 on Linux, signed in for real.** A patch series on top of upstream Wine, plus the
scripts and measurements around it, that runs Word, Excel and PowerPoint (Click-to-Run, 64-bit) and
lets you sign in with your own Microsoft account and your own subscription — the same sign-in,
the same licensing, as on Windows.

[![CI](https://github.com/Altars3668/wine-altars/actions/workflows/ci.yml/badge.svg)](https://github.com/Altars3668/wine-altars/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Altars3668/wine-altars?include_prereleases)](https://github.com/Altars3668/wine-altars/releases)
[![License: LGPL-2.1+](https://img.shields.io/badge/license-LGPL--2.1%2B-blue)](LICENSE)

中文说明：[README.zh-CN.md](README.zh-CN.md)

> **What "activation" means here.** You sign in with your own Microsoft account, and Office
> licenses itself against your subscription, exactly as it does on Windows. There is no crack,
> no key generator, no KMS emulation and no licence copying in this repository. Without a
> subscription Office does not license itself, here as on Windows. Microsoft does not support
> Office on Wine; this is an independent project.

## Where it stands

Measured on Wine 11.19 + this series, against Windows 11 (build 29671) as the reference:

| | |
|---|---|
| **Works** | Word, Excel and PowerPoint start, edit, save and print; Microsoft sign-in in Office's own window (WebView2) and licensing against a personal Microsoft 365 Family/Personal subscription; Word's Start screen, ribbon, dialogs and pop-up panels; an Excel sheet embedded and edited in place inside Word; printing through CUPS (manual duplex for printers without a duplexer is implemented and verified on captured output, not yet on paper); Chinese input through XIM; saving to OneDrive |
| **Checked** | Scripted feature sweeps: 34 Word, 72 Excel and 45 PowerPoint operations (save in many formats, export to PDF and XPS, charts, password-protected save, compare documents, …) pass, plus a save-and-reopen regression for each app. [`docs/`](docs) says how each was measured |
| **Not covered** | Outlook, OneNote, Access, Publisher and Teams are not part of the tested set. Work or school (Entra ID) accounts were not tested — only a personal Microsoft account. Updating Office in place was not tested; the safe route is a fresh prefix |
| **Known rough edges** | After an unclean exit Office offers Safe Mode on the next start (answer *No*). Under Xvfb (no GPU) WebView2 content renders black; that is the test display, not Office |

This is research-grade software. It gets a lot right and will still surprise you. The notes under
[`docs/`](docs) record what was measured, what turned out to be wrong, and where it currently stops.

## Quick start

You need a Microsoft 365 subscription (the scripts install the *Family/Personal* product by
default), an x86-64 Linux with X11 or Xwayland and a working GL driver, and about 10 GB of disk.
The release build is made on Ubuntu 24.04, so it needs glibc 2.39 or newer.

```sh
# 1. Wine, from the latest release (or build it yourself: docs/building.md)
curl -LO https://github.com/Altars3668/wine-altars/releases/latest/download/wine-altars-linux-x86_64.tar.xz
sudo tar -C /opt -xf wine-altars-linux-x86_64.tar.xz
export PATH=/opt/wine-altars/bin:$PATH

# 2. These scripts
git clone https://github.com/Altars3668/wine-altars.git && cd wine-altars

# 3. Prefix, WebView2 runtime, Office, sign-in switches.  Mostly waiting for Microsoft's CDN.
export WINEPREFIX=$HOME/.wine-office
scripts/office-setup.sh all

# 4. Start Word, and sign in with your own Microsoft account when it asks
scripts/office-setup.sh launch word
```

Step 3 is four commands you can also run one at a time (`prefix`, `webview2`, `install`,
`signin`); `scripts/office-setup.sh status` tells you which are done. By running `install` you accept
Microsoft's licence terms — it passes `AcceptEULA` to Microsoft's own installer.

**Signing in.** On the first start Word shows *Sign in to set up Office*; the same door is
*File → Account → Sign in*. Click it with the mouse — Office's buttons ignore the accessibility
"press" request, a real click works. A window titled *Sign in* opens (a WebView2 browser), you
enter your account, password and second factor as usual, and Word comes up licensed. *File →
Account* then shows the subscription as activated, as on Windows. The result is stored in the
prefix and survives restarts.

The step-by-step guide, with what each step does and what to do when it does not work, is
[`docs/getting-started.md`](docs/getting-started.md).

### Why the sign-in needs help

Three things stand between a stock Wine and Office's own sign-in window. All three are handled by
the steps above and none of them touches licensing:

1. Office decides *which* of its two sign-in implementations to use with feature gates, and both
   gates default the wrong way here. With the default, Office falls back to the built-in browser
   engine, the Microsoft sign-in page (a script-built single-page app) renders as an empty
   `<div>`, and the window is blank or closes at once. `signin` flips the two gates in the prefix
   (`scripts/enable-native-signin.sh`): the WebView2 gate on, and the "go through the account
   broker" gate off.
2. The WebView2 runtime has to be in the prefix (`webview2`). Microsoft's installer is a 32-bit
   program, which is why this Wine is built with its 32-bit half.
3. Wine's account broker (`windows.security.authentication.onlineid`) used to *claim* it could
   serve a Microsoft account and then fail to produce a ticket, so Office reported every later
   failure as a broker error instead of opening its own window. It now declines accounts it cannot
   serve ([`0024-onlineid-stand-aside-…`](patches/altars-up/0024-onlineid-stand-aside-when-this-broker-holds-no-token.patch)), and Office falls back
   to its own sign-in.

Identity caches copied from another machine make things worse, not better: they are sealed with
that machine's DPAPI keys, nothing here can open them, and Office answers "your account can't be
accessed" without offering a sign-in form. Start from an empty profile.

## Repository layout

| path | what is in it |
|---|---|
| [`patches/altars-up/`](patches/altars-up) | **The code**: 578 patches on top of upstream Wine `wine-11.19`, in order, `git am`-able; `BASE` names the upstream commit |
| [`scripts/`](scripts) | `office-setup.sh` (the quick start), `build-from-series.sh` (fetch, patch, build), prefix and measurement helpers, PE/PDB/WinRT inspection scripts |
| [`tools/`](tools) | About 190 small probes. Each one asks Windows and Wine the same question and keeps both answers side by side (`*.win.txt`, `*.wine.txt`); the Wine patches cite them |
| [`docs/`](docs) | The lab notebook and the notes behind individual fixes — [index](docs/README.md) |
| [`patches/office`](patches/office), [`patches/mstsc`](patches/mstsc), [`patches/mesa`](patches/mesa), [`patches/ported`](patches/ported) | Discovery-order patches with their reasoning, the inherited RDP-client series, a Mesa fix, work ported from other trees — see [`patches/README.md`](patches/README.md) |
| [`.github/workflows/`](.github/workflows) | CI, and the release build |

The method is the point of the project: every claim in the notes is a register read, a memory read
or bytes on the wire, measured on Windows and on Wine, and the probe that measured it is in the
repository. [`docs/method.md`](docs/method.md) lists the instruments and the traps already hit.

## Releases and CI

* **Tag `v*`** (for example `v11.19-altars.1`): GitHub Actions builds Wine from upstream and the
  series with [`scripts/build-from-series.sh`](scripts/build-from-series.sh), smoke-tests it
  (`wineboot`, a 64-bit and a 32-bit `cmd`), and publishes a release with the build, the patch
  series and checksums.
* **Push to `main`** that changes the series or the build recipe: the rolling **`nightly`**
  pre-release is rebuilt.
* **Every push and pull request**: shell and Python scripts parse, `shellcheck` finds no errors, the
  unit tests pass, and the series still applies to the pinned upstream commit.

A local build is the same recipe: [`docs/building.md`](docs/building.md).

## Reporting a problem

Wine logs and Office's own diagnostic files (`%LOCALAPPDATA%\Temp\Diagnostics`) contain account
identifiers, tenant ids, device ids and, in some modes, request URLs. **Read and redact before you
attach anything to an issue**, and never paste a token, a cookie or a licence file. The most useful
report is the output of `scripts/office-setup.sh status`, the Wine version, what you did, and the
first error line.

## Licence and trademarks

Everything here is under the GNU LGPL 2.1 or later, the licence of Wine ([`LICENSE`](LICENSE)).
This repository contains no Microsoft software: Office and the WebView2 runtime are downloaded from
Microsoft by Microsoft's own installers, onto your machine, for use under your own licence.

Microsoft, Windows, Office, Word, Excel, PowerPoint, Microsoft 365, OneDrive and Edge are
trademarks of the Microsoft group of companies. Wine is a trademark of its respective owners. This
project is not affiliated with, endorsed by, or supported by Microsoft or the WineHQ project.
