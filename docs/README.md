# Documentation

Start here if you want to **use** it:

| | |
|---|---|
| [`getting-started.md`](getting-started.md) · [中文](getting-started.zh-CN.md) | From nothing to Word, Excel and PowerPoint signed in with your own Microsoft account: prefix, WebView2, install, sign-in, troubleshooting |
| [`building.md`](building.md) | Building Wine from the patch series; the release workflow; changing the series |

Read these if you want to know **why it works**, or to work on it. They were written as the work
went, in the order things were found, and keep the wrong turns in: a measurement that was later
overturned stays, with the note that overturned it.

| | |
|---|---|
| [`method.md`](method.md) | The rule (every claim is a register read, a memory read or bytes on the wire), the instruments, and the traps already hit |
| [`office365-under-wine.md`](office365-under-wine.md) | **The lab notebook.** About 12,000 lines, English and Chinese: how Click-to-Run, App-V, licensing, sign-in, rendering and the long tail of Win32 behaviour were measured and fixed. Search it by symptom. The scripts it names that are not in this repository belong to earlier, retired routes |
| [`upstream-evaluation.md`](upstream-evaluation.md) | wine-staging, Valve's tree and wine-tkg surveyed against this project's blocker: 131 + 400 commits read, one taken |
| [`office-prefixupdate-20261003.md`](office-prefixupdate-20261003.md) | Prefix-update diagnostics against native Windows (system queries, RAS, profiles, MSXML validation) |
| [`office-rest-fixes-20261004.md`](office-rest-fixes-20261004.md) | The first batch of remaining Office features fixed |
| [`office-popup-focus-latency-20261006.md`](office-popup-focus-latency-20261006.md) | Pop-up panels that closed at once, and slow drop-downs |
| [`office-upgrade-references-corners-20261006.md`](office-upgrade-references-corners-20261006.md) | The upgrade-plan dialog (WebView2 hosted through DirectComposition), the References tab crash, rounded panels |
| [`office-window-shape-owner-json-20261006.md`](office-window-shape-owner-json-20261006.md) | A maximised window cut to a small area, cross-process owners, JSON iteration order |

The probes behind the numbers are in [`../tools/`](../tools): one directory per question, each with
a README, the source, and the output from Windows and from Wine side by side. The patches that
cite them are in [`../patches/altars-up/`](../patches/altars-up).

**On the commit ids in these notes.** `altars-up <sha>` and `wine-src <sha>` are commits of the
author's Wine tree, which is not published. [`../patches/altars-up/SERIES.tsv`](../patches/altars-up/SERIES.tsv)
maps them to patch files (`grep <sha> patches/altars-up/SERIES.tsv`). Commits older than the move
to upstream Wine master belong to a retired CrossOver-based tree and are not in the series.

**On the reference machine.** Several notes compare Wine's behaviour with `winref`: a Windows 11
PC with a real Microsoft 365 subscription (build 29671), on which the probes were run. Account
names, tenant and device identifiers were removed from the published copies of these files.
