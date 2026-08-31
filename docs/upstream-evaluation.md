# Borrowing from wine-staging, Valve and wine-tkg

Evaluated 2026-08-31 against this project's actual blocker, which is
Click-to-Run's bootstrap (see `office365-under-wine.md`). The conclusion is
that **almost none of it applies**, and the reason is worth writing down so
nobody re-runs the survey.

## wine-staging v11.0 — 131 patchsets, 1 candidate, 0 taken

Matches our base version exactly. The breakdown by subsystem:

* **Graphics, audio, input, games** — `wined3d-*` (7), `d3dx9-*` (5),
  `dinput-*`, `dsound-*`, `ddraw-*`, `nvcuda`/`nvapi`/`nvenc`, `winepulse-*`,
  `xactengine`, `dxva2`. This is the bulk of it and none of it is reachable
  from a Word that never loads its own code.
* **X11 / window management** — `winex11-*` (5), `winemac.drv`, `uxtheme-GTK`.
  Not reached.
* **Plausible but unsupported** — `ntdll-Junction_Points`,
  `ntdll-ext4-case-folder`, `ntdll-RtlQueryPackageIdentity`,
  `ntdll-Hide_Wine_Exports`.

That last group deserved a measurement rather than a guess, and got one:

> **Office does not look for Wine.** `strings` over `c2r64.dll`,
> `AppVIsvSubsystems64.dll`, `WINWORD.EXE` and `OfficeClickToRun.exe` finds
> **zero** occurrences of `wine_get_*`, `wine_unix_*`, `winehq`, `__wine` or
> any other Wine marker. `ntdll-Hide_Wine_Exports` cannot help something that
> never asks.

Case folding and junction points stay on the list only because C2R's `vfs`
layout and mixed-case paths (`microsoft shared` on disk, `Microsoft Shared` in
the registry) make them *conceivable* — but nothing observed so far fails on
either, and Wine's own path lookup is already case-insensitive. Not taken
without evidence.

## Valve — `ValveSoftware/wine`, `experimental_11.0` — 1 taken

Valve maintains a branch on our exact base. Subsystem distribution over the
last 400 commits: `cfgmgr32` (37), `setupapi` (22), `ntdll` (17),
`xinput1_3` (14), `wbemdisp` (13), `server` (11), `windows.storage` (10),
`win32u` (10), `winex11.drv` (9), `winevulkan` (9), `kernelbase` (9),
`wintypes` (8), `gameinput` (6).

Overwhelmingly game-facing — device enumeration for controllers, Vulkan,
per-title HACKs. Two areas looked relevant and one was:

* **`wintypes` / `windows.storage` (18 commits)** — real WinRT work, but it is
  `DataWriter` and `InMemoryRandomAccessStream`: streams and buffers. Our WinRT
  gap is the *contract facade DLLs* (`Windows.System.dll`, `Windows.dll`) that
  a caller loads by name. Different problem. Not taken.
* **`kernelbase: Add a stub for FindNextFileNameW()`** (9f7ab6c, Paul Gofman,
  CW-Bug-Id #27301) — **taken**, `patches/ported/0001-*`.

  Wine had `FindNextFileNameW` as `# @ stub` in both spec files, i.e. not
  exported at all. That is worse than a failing stub: an import that cannot
  bind is pointed at `stub_entry_point`, which raises `EXCEPTION_WINE_STUB`
  instead of returning an error, so a caller merely asking a question dies.
  `AppVIsvSubsystems64.dll` imports it.

  Measured honestly: Word does not call it, so this fixes nothing observable
  for Office **today**. Taken anyway — three lines, a real gap, and Valve
  arrived at it from an unrelated direction, which is the usual sign that a
  gap is load-bearing for somebody.

## CrossOver 26.3.0 — taken as the new base, but it does not fix this

CodeWeavers ships Microsoft 365 support commercially and publishes their Wine
under the LGPL. Their 26.3.0 sources are on **the same wine-11.0 base as ours**,
which makes the comparison exact: **221 files, +15717/-662** against pristine.

All 13 mstsc patches apply to it cleanly. (The Valve `FindNextFileNameW` patch
conflicts because CrossOver already has it — same author, CodeWeavers maintains
Valve's Wine too.) `altars-cx` is that tree plus our series.

They do have Click-to-Run specific work, and one comment names our exact
problem — `CXHACK 14391` in `rpcrt4/rpc_transport.c`:

> ClickToRun hooks NtReadFile and many other APIs. Some of hooked functions do
> RPC calls. When we call NtReadFile in such RPC call, it locks a critical
> section inside its NtReadFile hook, which causes dead locks. It most likely
> works on Windows because **RPC calls use LPC ports, so they don't call
> NtReadFile**.

Also `CX HACK 10834` (msoffice sets FILE_EXECUTE on saved files) and
`CX HACK 18329` (Office 365: always install osppc.dll).

**It changes nothing here.** Measured on the CrossOver base with the same
prefix: Word still never loads `wwlib.dll`, still ends on the same
`RPC_S_SERVER_UNAVAILABLE`, `ClickToRunSvc` still fails to start with 1077, and
`OfficeClickToRun.exe /service` reports the *same* missing pieces
(`Windows.System.dll`, `twinapi.appcore`, `PrivIsDllSynchronizationHeld`).
A module count that rose 186 -> 357 is an artifact: the extra entries are
`iexplore.exe`, `msi.dll`, `mshtml.dll` — other processes in the same log, not
Word getting further.

The likely reason is visible in the log: every CrossOver process tries to load
**`cxcompatdb.so` and fails**, because it is proprietary and not in the LGPL
source drop. CodeWeavers' Office support lives partly in that compatibility
database and in their bottle configuration, neither of which is published.

Keeping the base anyway: it is the same wine-11.0, it carries CXHACK 14391 for
when the deadlock does become reachable, and it costs nothing.

## wine-tkg

An integration project — it composes staging, Proton and kernel patches rather
than originating fixes. Nothing to take that is not already covered above.

## How to port something

    scripts/port-upstream.sh <repo-url> <commit-ish>

Applies to `wine-src`'s `altars` branch, commits with the provenance in the
message, and drops the patch in `patches/ported/`. Anything ported must say in
its commit message whether it changes anything **measurable** here, and say so
plainly when it does not.
