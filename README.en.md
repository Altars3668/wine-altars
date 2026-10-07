# wine-altars

[简体中文](README.md) | **English**

A Wine patch collection for **Microsoft 365 desktop applications and Windows-client interoperability on Linux**. The current public build line targets upstream **Wine 11.19**, with a focus on 64-bit Click-to-Run Word, Excel and PowerPoint: installation, rendering, editing, printing, and normal sign-in to a personal Microsoft 365 subscription through Office's own WebView2 window.

> **Not a crack or a licensing bypass.** You need your own Microsoft account and valid subscription. This repository provides no key generation, KMS emulation or license copying, contains no Microsoft software, and is not officially supported by Microsoft or WineHQ.

[![CI](https://github.com/Altars3668/wine-altars/actions/workflows/ci.yml/badge.svg)](https://github.com/Altars3668/wine-altars/actions/workflows/ci.yml)

## What I changed

| Area | Changes and evidence |
| --- | --- |
| **Click-to-Run / App-V runtime path** | Addresses installation, services, COM / WinRT and application-start compatibility gaps. Opening a window is not treated as complete functionality. See the [lab notebook](docs/office365-under-wine.md). |
| **Normal Office sign-in** | Installs WebView2 and selects a working authentication path. A Wine broker without an account ticket stands aside instead of causing a blank or immediately closing window. See the [sign-in helper](scripts/enable-native-signin.sh). |
| **UI, focus and window behaviour** | Fixes immediately closing popups, slow menus, stale window shapes, cross-process owners, corners and the References crash. See [popup notes](docs/office-popup-focus-latency-20261006.md) and [window behaviour](docs/office-window-shape-owner-json-20261006.md). |
| **Documents, OLE and component interoperability** | Compares embedded Excel and MSXML / scripting / JSON behaviour with Windows, keeping source changes alongside probes. |
| **Linux desktop integration** | Covers CUPS printing, input methods and application integration. Manual duplex was verified using captured output, not yet physical paper. |
| **Paired Windows / Wine probes** | [tools/](tools/) keeps sources and outputs per question, explaining a specific return value, structure or visible behaviour instead of guessing at stubs. |
| **Rebuildable upstream patch series** | [patches/altars-up/](patches/altars-up/) pins the baseline and order, retains provenance, and includes export, build and CI scripts. |
| **Stepwise setup and maintenance** | `office-setup.sh` separates prefix, WebView2, Office installation and sign-in preparation, with status inspection instead of distributing host environments or account caches. |

The series also contains upstream ports and inherited work. **Not every patch is claimed as my original contribution.** The applicable series, discovery-order patches and historical CrossOver notes have separate purposes; see [patches/README.md](patches/README.md).

## Existing evidence and limits

Project notes record Word, Excel and PowerPoint startup, editing, saving, export, printing, save/reopen, in-place OLE editing and personal Microsoft 365 Family / Personal sign-in. These are measurements of particular builds and environments, **not a fresh application qualification performed by this README update or a guarantee for every Office release and machine**.

- Outlook, OneNote, Access, Publisher and Teams are outside the tested set; work / school Entra ID accounts were not validated.
- In-place Office updates are not a validated reliable route. Prefer a fresh prefix and retain data backups.
- WebView2 needs a working graphics driver; GPU-less Xvfb can display black content.
- An unclean exit may trigger a Safe Mode prompt on next startup; this does not by itself imply damaged sign-in or subscription state.
- Historical notes retain conclusions later overturned by measurements. Follow dates, subsequent corrections and probes rather than treating the entire notebook as current setup instructions.

## Getting started

**Rather not compile?** The [Releases](https://github.com/Altars3668/wine-altars/releases) page has a prebuilt Ubuntu 24.04 build, `wine-altars-linux-x86_64.tar.xz` (needs glibc 2.39 or newer), and `SHA256SUMS`. Check the checksum, unpack it under `/opt` (or anywhere), put its `bin/` on `PATH`, and continue from the `WINEPREFIX` line below. Tags starting with `v` are stable releases; `nightly` is the pre-release that tracks the patch series.

You need x86-64 Linux, X11 / Xwayland, a working GL driver, Wine build dependencies and your own valid Microsoft 365 subscription. Follow [building.md](docs/building.md) for dependencies and build into a user directory rather than replacing system Wine by default:

```sh
git clone https://github.com/Altars3668/wine-altars.git
cd wine-altars
# Install dependencies from docs/building.md first; limit parallelism to control linker memory use.
JOBS=2 PREFIX="$HOME/.local/opt/wine-altars" scripts/build-from-series.sh
export PATH="$HOME/.local/opt/wine-altars/bin:$PATH"

# Use a separate prefix; do not open it concurrently with different Wine builds.
export WINEPREFIX="$HOME/.wine-office-altars"
scripts/office-setup.sh all
scripts/office-setup.sh status
scripts/office-setup.sh launch word
```

`all` can be split into `prefix`, `webview2`, `install` and `signin`. `install` passes `AcceptEULA` to Microsoft's official installer and accepts the applicable terms; the scripts do not sign in on your behalf. In Word, use **File → Account → Sign in** and complete authentication and second-factor checks yourself.

Do not copy identity or license caches from another machine: they are protected by its DPAPI keys, not an alternative sign-in method. Prefixes, diagnostics and identity caches contain sensitive data and must not be uploaded publicly.

Full instructions: [English](docs/getting-started.md) · [简体中文](docs/getting-started.zh-CN.md).

## Source and file guide

| Path | Purpose |
| --- | --- |
| [patches/altars-up/BASE](patches/altars-up/BASE) | Upstream version, commit and source mirror. |
| [patches/altars-up/SERIES.tsv](patches/altars-up/SERIES.tsv) | Maps development-tree commit IDs to public patch files. |
| [scripts/build-from-series.sh](scripts/build-from-series.sh) | Fetch, apply, configure, build and install. |
| [scripts/office-setup.sh](scripts/office-setup.sh) | Prefix, runtime, Office and launcher setup. |
| [docs/building.md](docs/building.md) | Dependencies, variables, 32 / 64-bit components and prefix DLL synchronisation. |
| [docs/README.md](docs/README.md) / [docs/method.md](docs/method.md) | Notebook index and Windows comparison method. |
| [tools/](tools/) | Small behaviour probes and redacted reference outputs. |
| [.github/workflows/](.github/workflows/) | Validation and release recipes. |

Wine source is not vendored here. The builder fetches the baseline from `BASE` and applies the series. Historical `wine-src` / `altars-up` SHAs are not commits in this documentation repository; locate applicable public patches through `SERIES.tsv`.

## Releases and verification

- `ci.yml` defines syntax, static, unit and series-application checks. **Defining checks does not mean every run passes.**
- `release.yml` defines `v*` tag releases and rolling `nightly` prereleases for series / build-recipe changes.
- Check [Releases](https://github.com/Altars3668/wine-altars/releases) for actual artifacts before downloading. Do not assume `latest/download` exists; prereleases may not appear under `latest`.
- Release artifacts include the build, patch series and `SHA256SUMS`. Select an exact tag and verify checksums. Ubuntu 24.04 builds require compatible glibc.
- `office-smoke.yml` (started by hand) runs `office-setup.sh all` on a clean GitHub runner with the Wine from a release, starts Word, and keeps a screenshot and logs as artifacts. It stops at Office's own sign-in prompt ([screenshot](docs/img/word-first-start.png)) and does not sign in for anyone; whether a given run passed is what that workflow's run history says.
- Commands here build software or modify a prefix. Reading the instructions does not constitute installation, subscription sign-in or application qualification.

## Public and private histories

GitHub carries the existing anonymised public history; Gitea retains complete development history. README content is synchronised, but commit SHAs and some notebook / patch metadata can differ. **Do not force-push private history publicly or undo redaction just to make SHAs match.**

## License and reporting

This repository uses [GNU LGPL 2.1 or later](LICENSE) and retains Wine, ported-patch and component attribution / license notices. Office, WebView2 and subscriptions are governed separately by Microsoft; this repository provides helpers for installation from official sources.

Report the Wine / series version, reproduction steps and a redacted first error. Logs can contain account, tenant and device identifiers and request URLs. Read before uploading; never attach tokens, cookies, identity caches or license files.
