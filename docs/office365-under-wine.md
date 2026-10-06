# Microsoft 365 under Wine — what was measured

Everything below is either something a tool printed or a file that exists.
Where something is inferred it says so. The document is chronological and later
sections correct earlier ones where they were wrong; the corrections are marked.

## Where it stands (2026-09-02)

Word starts, builds its whole UI, reaches its Start screen, and renders Office's
own Microsoft sign-in page inside itself. **Its own window paints nothing** —
see the last section: DirectWrite could not see a single Windows font, which is
fixed, and the remaining gap is that Office's D2D render targets never reach the
screen. The Gecko-rendered sign-in page does paint, which is why signing in is
possible while the rest of Office is blank. The licence is unsolved separately.

    Office.Licensing.FullValidation   fails  0xc004f015  SL_E_LICENSE_NOT_INSTALLED
    Office.Licensing.OnlineRepair     S_OK in 2 ticks, without going online
    no licensing host contacted at start-up, on any of winsock/wininet/winhttp

The licence in the prefix was issued to another device's `HardwareId`, so this
device needs one of its own, which needs an activation this install never
attempts. `OnlineRepair` is the activity that would do it, and it returns success having
done nothing. A candidate gate was read statically in `Mso98win32client.dll` —
a virtual call whose result is stored at `+0x593750`, immediately before the
code that logs `SPPLicensedProducts` and `BestSPPCategory` — but **that
attribution is unconfirmed**: a one-shot breakpoint on that function's entry
never fired in 520 seconds. Either Word does not reach it in that window under a
debugger, or the function found by the `OnlineRepair` string reference is the
telemetry name registration rather than the activity (the string appears twice,
in a 2017-byte function and a 256-byte one). Resolving that is where to start.

**Ruled out as the gate**, each with a measurement: the WAM broker (it now
answers everything, with no FIXME reached); valid freshly-minted tokens for
every scope; the token's content at all; consent and privacy state; network
connectivity; the ECS audience configuration; the Click-to-Run service running;
the App-V layer; the service catalog; and the identity caches.

**Fixed along the way**, and worth having regardless of the licence: Office no
longer reports itself as `Not_C2R` (`scripts/install-c2r-marker.sh`), the WAM
broker answers its whole surface (`patches/office/0006-*`), and the sign-in page
works (`scripts/office-sign-in.sh`). The next thing to try is finishing that
sign-in by hand and seeing whether a device licence follows.

> **Update (2026-09-03):** "by hand" turned out to be doing too much work in
> that sentence. See "Sign-in's third blocker, found" near the end: the form
> can now be filled without a person's hands on a keyboard (`xdotool`, not
> `SendInput` -- a real X11 input path Gecko answers to, where three Win32 ones
> did not). What still needs a person is the credential itself and the
> decision to spend it against a live Microsoft endpoint. Also since this was
> written: the blank Office window was traced to the display-compositor layer
> (out of this repo's scope to fix), and a safe-mode dialog turned out to
> explain a large fraction of historical "Word shows no window" reports.

---

Session of 2026-08-31 begins here. **Its opening claim is superseded**: at the
time, `WINWORD.EXE` loaded 186 modules and exited with status 0 without loading
a line of Word, stopping inside Click-to-Run's bootstrap.

## The installation

Taken from a real Windows on the same machine (`/mnt/win-c`), not from an
installer:

| | |
|---|---|
| product | `O365HomePremRetail` — Microsoft 365 consumer subscription |
| version | `16.0.20208.20000`, channel `Insiders::DevMain` |
| platform | x64, `zh-cn` |
| install path | `C:\Program Files\Microsoft Office` |
| size | 5.2 GB (2.8 GB `root/Office16`, 2.1 GB `root/vfs`) |
| licence | one vNext token under `%LOCALAPPDATA%\Microsoft\Office\Licenses\5\`, `OSPPReady=1` |

### Click-to-Run is not a normal installation

C2R does not put files where programs expect them. It keeps one self-contained
package and has **App-V** project parts of it onto the filesystem when an Office
process starts. Wine has no App-V, so `root/vfs` has to be unfolded by hand —
`scripts/import-office.sh` does it:

    root/vfs/ProgramFilesCommonX64  ->  C:\Program Files\Common Files
    root/vfs/System                 ->  C:\windows\system32
    root/vfs/SystemX86              ->  C:\windows\syswow64
    ... nine branches in total

The same is true of the registry. `root/vreg/*.vreg.dat` are **standard `regf`
hives** (verified: they start with the bytes `regf`) that App-V overlays onto
HKLM at launch. Every COM class registration and file association Office has
lives there — 306 subkeys under `Classes` in `office.x-none` alone. Under Wine
they have to be written for real.

`scripts/export-office-registry.py` exports all four sources:

| source | keys | values |
|---|---|---|
| `SOFTWARE\Microsoft\Office` | 44862 | 72760 |
| `SYSTEM\CurrentControlSet\Services` (Office/App-V only) | 13 | 53 |
| `NTUSER.DAT\Software\Microsoft\Office` | 2874 | 9102 |
| `root/vreg/*.dat` (23 hives) | 35193 | 48845 |
| **total** | **82942** | **130760** |

A note on that exporter, because the bug is easy to repeat: hivex's
`value_type()` returns `(type, LENGTH)`, not `(type, data)`. Taking the second
element as the data silently drops every REG_BINARY / EXPAND_SZ / MULTI_SZ /
QWORD value — 11836 of them here, which is exactly where Office keeps its
licence and identity blobs. Use `value_value()`.

## Where Word stops

`WINEDEBUG=+loaddll`, and read the tail. In order:

    WINWORD.EXE
    c2r64.dll                      <- the Click-to-Run engine
    VCRUNTIME140.dll, VCRUNTIME140_1.dll, MSVCP140.dll   (native, ours)
    AppVIsvSubsystems64.dll        <- the App-V ISV subsystem
    RPCRT4, secur32, Kerberos, netapi32, MSV1_0, VERSION, dnsapi, ws2_32
    (stop — 186 modules, exit 0)

**No `wwlib.dll`, no `mso*.dll`.** Word never reaches its own code. The last
thing it does is stand up an RPC and security stack, and:

    dispatch_exception code=6ba (RPC_S_SERVER_UNAVAILABLE)   x1
    handle_syscall_fault code=c0000005                       x17

Inferred, not yet proven: `c2r64.dll` calls the `ClickToRunSvc` service over
RPC, does not get an answer, and gives up. What is measured is the exception
code and that nothing of Word loads after it.

## Ruled out

Keeping this list is half the value.

**The four missing imports in `AppVIsvSubsystems64.dll` are not the problem.**
Wine's loader reports them at load time:

    No implementation for KERNEL32.dll.FindNextFileNameW
    No implementation for ntdll.dll.RtlIsGenericTableEmptyAvl
    No implementation for ntdll.dll.RtlIsNameInExpression
    No implementation for ntdll.dll.RtlEnumerateGenericTableAvl

All four are real gaps — in Wine 11.0 they are commented-out stubs in the `.spec`
files, `RtlIsNameInExpression` has no entry at all, and the whole AVL generic
table family in `rtl.c` is FIXME stubs that do nothing. Implementing them was
the obvious first move.

They are never called. A missing import is bound to `stub_entry_point`, which
raises `EXCEPTION_WINE_STUB` (0x80000100) — and `WINEDEBUG=+seh` over a full
Word run shows exactly one exception, `6ba`, and no `80000100`. Implementing
those four functions would change nothing observable.

*The instrument matters here.* `stub_entry_point` prints **nothing** — it raises
and then spins in `for(;;) RtlRaiseException()`. Grepping the log for "stub
function" therefore finds nothing whether or not a stub was hit, which is not a
finding. A self-test — calling a known stub from a 10-line `.exe` — hung the
process until SIGTERM, which is what proves the exception path is live and that
the silence in Word's log means what it appears to mean.

**Declaring `ClickToRunSvc` is not enough.** The service is defined in the
SYSTEM hive, which the first version of the exporter did not read, so the
service genuinely did not exist in the prefix. After exporting and importing it
(`ImagePath` = `OfficeClickToRun.exe /service`, `Start` = 2, `LocalSystem`),
Word loads **186 modules and stops in the same place** — byte for byte the same
module list. Whatever `c2r64.dll` wants, a registry definition alone does not
give it.

**Wine does have the WinRT DLLs the C2R service asks for.** `twinapi.appcore`,
`windows.networking.connectivity` and `windows.security.authentication.onlineid`
all exist in Wine 11.0 and are in our `dist`. The `find_builtin_dll cannot find
builtin library` warnings next to them are about the unix `.so` half and are
normal for a PE-only DLL.

## The next thread

`sc start ClickToRunSvc` fails with **1077** (`ERROR_SERVICE_NEVER_STARTED`).
Run directly, `OfficeClickToRun.exe /service` names what it cannot find:

| missing | kind |
|---|---|
| `Windows.System.dll`, `Windows.System.Profile.dll`, `Windows.dll` | WinRT API-contract facade DLLs — Wine has none of these names |
| `kernelbase.PrivIsDllSynchronizationHeld` | not in `kernelbase.spec` |
| `wevtsvc.SvchostPushServiceGlobals` | not exported |

The WinRT facades are the interesting ones and the likely next patch: Wine
implements the *classes* (`windows.networking.connectivity` and friends) but not
the contract DLL names a caller loads by name. Note that
`GetNetworkConnectivityHint` — which `windows.networking.connectivity` is the
WinRT wrapper around — is already implemented by
`patches/mstsc/iphlpapi-network-connectivity-hint.patch`, so that half is done.

Order of work, cheapest first:

1. Find out what `c2r64.dll` actually asks the service for — a wire relay on the
   RPC endpoint, or a breakpoint on the `RPC_S_SERVER_UNAVAILABLE` decision.
   Do this **before** building anything: the service may not need to work, only
   to answer.
2. Get `OfficeClickToRun.exe /service` far enough to register with the SCM.
3. Only then decide whether the WinRT facades are needed or were incidental.

## The Windows-side comparison (2026-08-31)

A real Windows with **the same product** (`O365HomePremRetail` x64,
16.0.20425.20000 vs our 16.0.20208.20000) was used as a reference. There Word
starts normally and loads **244 modules**. PowerShell's `Process.Modules` is in
load order, so the two sequences can be lined up directly.

Windows, in order: `WINWORD.EXE`, `ntdll`, `KERNEL32`, `KERNELBASE`, `apphelp`,
`ucrtbase`, `VCRUNTIME140_1`, `VCRUNTIME140`, **`AppVIsvSubsystems64`** (9th),
`MSVCP140`, **`c2r64`** (11th), `ADVAPI32`, `msvcrt`, `sechost`, **`RPCRT4`**
(15th), `Comctl32`, `GDI32`, `win32u`, `gdi32full`, `msvcp_win`, `etw`,
`USER32`, `ole32`, `combase`, `oleaut32`, **`wwlib.dll`**, `oart`,
`mso20win32client` ...

We reach the same first eleven. Then the paths diverge, and the divergence is
sharper than "we stop":

| module | Windows | Wine |
|---|---|---|
| `Kerberos.dll` | **not loaded** | loaded |
| `MSV1_0.dll` | **not loaded** | loaded |
| `netapi32.dll` | **not loaded** | loaded |

`Kerberos` and `MSV1_0` are SSPI authentication packages; they are loaded only
when something authenticates. **On Windows, Click-to-Run's `ncalrpc` never
authenticates** — it is an ALPC port, local and unauthenticated. Wine implements
`ncalrpc` over **named pipes**, and the named-pipe connection drags in the full
NTLM/Kerberos negotiation, a path that does not exist on Windows at all.

That is the same difference CodeWeavers describes in CXHACK 14391 from the other
side ("RPC calls use LPC ports, so they don't call NtReadFile"). It is now a
measurement rather than a quotation.

The RPC target itself is fully identified without a debugger:

* interface UUID **`469d3a0e-e164-422e-a662-9cbe0621407e` v1.0**, extracted
  from `C2R64.dll`, `ApiClient.dll` and `OfficeClickToRun.exe` by anchoring on
  the NDR transfer-syntax GUID and reading the 20 bytes before it
* protocol **`ncalrpc`**, endpoint **`ClickToRun_Pipeline16`** — which is also
  `PipelineServerName` in the registry we already import

## Correction: Word never talks to Click-to-Run's RPC interface

`WINEDEBUG=+rpc` over a full Word start, 22525 lines. Two counts settle it:

* occurrences of `ClickToRun_Pipeline16`: **0**
* occurrences of the interface UUID `469d3a0e`: **0**

Word does **not** contact the Click-to-Run pipeline at all. What it actually
does, repeatedly, from several threads:

    RpcStringBindingComposeW (ncacn_np, "\pipe\svcctl")
    rpcrt4_conn_open_pipe    connecting to \\.\pipe\svcctl

`svcctl` is the **Service Control Manager**. Word is asking the SCM about a
service — and `ClickToRunSvc` is stopped, because it fails to start with 1077.

This supersedes the earlier reading on this page, which had `c2r64.dll` calling
the service and getting no answer. It never places the call. It asks the SCM
whether the service is there, gets an answer it does not like, and stops. The
interface UUID and endpoint extracted earlier are correct but not yet reached.

**So the whole problem is now one goal: make `OfficeClickToRun.exe /service`
start and register with the SCM.** What it says it is missing, unchanged across
both the pristine and the CrossOver base:

| missing | kind |
|---|---|
| `Windows.System.dll`, `Windows.System.Profile.dll`, `Windows.dll` | WinRT API-contract facade DLLs |
| `kernelbase.PrivIsDllSynchronizationHeld` | not in the spec |
| `wevtsvc.SvchostPushServiceGlobals` | not exported |

The Kerberos/MSV1_0 divergence from Windows is real but is a property of Wine's
named-pipe `ncalrpc`, and on this path it is `\pipe\svcctl` that drags them in —
so it is a second finding, not the blocker.

## Word starts (2026-08-31, later the same day)

`WINWORD.EXE` now reaches its own splash screen — the Microsoft 365 branding,
the logo, "正在启动 Microsoft Word…" — with Chinese text rendering correctly,
and reports an error of its own rather than dying in a loader. That is the whole
of Word's UI framework, resource loading (the MUI patch) and font stack working.

The chain that got there, each link found by running it and reading what it said:

1. **Two directories differing only in case.** `import-office.sh` unfolded
   `root/vfs/ProgramFilesCommonX64` into `Common Files/Microsoft Shared` while
   also copying the drive's own `Common Files/microsoft shared`. NTFS is
   case-insensitive and ext4 is not, so both existed, each holding half the
   content, and Wine's case-insensitive lookup picked whichever it found first.
   `OfficeClickToRun.exe` was in the half nobody looked in, which is why the
   service failed with `ERROR_FILE_NOT_FOUND` while sitting on disk. Merging
   them is what first got `ClickToRunSvc` to **RUNNING**.
2. **`IAnalyticsInfoStatics2` missing from twinapi.appcore** — the service
   activated `AnalyticsInfo`, QueryInterfaced for it, got `E_NOINTERFACE`, and
   C++/WinRT turned that into a thrown exception that killed the service.
3. **The AppV runtime virtualisation layer.** `AppvIsvSubsystems64.dll`'s
   `DllMain` fails here, and a failed `DLL_PROCESS_ATTACH` aborts the whole
   process. It is Click-to-Run's API-hooking layer, whose entire job is to
   project `root\vfs` and `root\vreg` over the real system — **which this prefix
   has already done for real**. `tools/appv-stub/` is a stand-in that reports
   "not virtualised"; `APIExportForDetours` returns 1 because that is literally
   what the shipped one does (`mov eax,1; ret`, read from the binary).
4. **Three absent kernel32/kernelbase exports**, then
   **`ole32.CoRegisterActivationFilter`**, then **`sppc.SLLoadApplicationPolicies`**
   — each surfaced only after the previous was supplied.

The delay-load diagnostic added to `ntdll` is what made step 4 tractable: the
MSVC helper raises `VcppException(ERROR_PROC_NOT_FOUND)` with a `DelayLoadInfo`
and Wine used to say nothing, so `0xC06D007F` in a log named no import at all.

**Where it stops now:** Word's own dialog, in Chinese —
*"很抱歉，出现错误，Word 不能启动。(6)"*. That is Word's error code, not Wine's.

### Error (6)

With everything above supplied, Word gets through its loader, paints the
Microsoft 365 splash, and stops on a dialog of its own:
*"很抱歉，出现错误，Word 不能启动。(6)"*. Reproducible, and unchanged by
`/a` (no add-ins) or `/safe`.

At that point Wine reports **zero** unimplemented functions, **zero** failed
delay imports and **zero** missing imports over a whole run, so this is Word's
own refusal rather than another gap in Wine.

Licensing state, for the next session to start from:

* the vNext token is in place under `%LOCALAPPDATA%\Microsoft\Office\Licenses\5\`
* `HKCU\...\Common\Licensing\LicensingNext` has `O365HomePremRetail = 2`
* `HKCU\...\Common\Identity\Identities` is **empty** — but it is *also* empty in
  the source `NTUSER.DAT`, so nothing was lost in the import; that machine keeps
  its sign-in elsewhere (OneAuth) or was not signed in.

A subscription SKU wants an identity to validate against, and `sppc` here
answers with an empty policy set by design. Whether (6) is the licensing check
or something else is not yet measured — the next step is to find what Word reads
immediately before it raises it, not to guess.

### What error (6) is not

Each of these was tried and changed nothing — the dialog is identical:

* **add-ins** — `/a` and `/safe` both still fail
* **`Normal.dotm` and the Roaming user data.** They were genuinely missing:
  `import-office.sh` never copied `AppData\Roaming\Microsoft\{Templates,Word,Office}`,
  so Word had no default template at all. Copying them (40 + 13 + 37 files) is
  right and is now in the script — but (6) is unchanged.
* **`HKCU\...\Word\Data`**, the binary blob of machine-specific state
* **the whole `HKCU\...\Office\16.0\Word` tree**, deleted so Word rebuilds from
  nothing
* **`osppc.dll`.** Word walks the loaded-module list looking for it — and
  CodeWeavers carry `CX HACK 18329`, "Office 365: Always install osppc.dll", so
  it looked promising. It is **absent from the source Windows too**, and their
  hack is in the MSI install path, not the C2R one.
* **`isolatedwindowsenvironmentutils.dll`**, the one module that still fails to
  load, is **not present on the real Windows either**.

Across a whole run Wine reports zero unimplemented functions, zero failed delay
imports and zero missing imports, for both Word and Excel.

### Excel gets a different error, and it is a number

Excel reaches a small dialog reading **"IOPL not enabled."** — the message text
for Win32 error **197, `ERROR_IOPL_NOT_ENABLED`**. Nothing in Wine's tree ever
returns 197, so it is a code Office obtained somewhere and rendered through
`FormatMessage`. Word's (6) and Excel's 197 are different symptoms, which is
itself informative: whatever fails is upstream of both, and each app reports it
in its own way.

That number is the most concrete thing left to pull on: find which call leaves
197 in the thread's last-error, rather than guessing at licensing.

### How far Word actually gets, and four more things ruled out

With `WINEDEBUG=+loaddll`, Word now loads **657 modules**, and the list is close
to complete: `wwlib`, `oart`, all five `mso*win32client`, `mso.dll`, and the
typesetting stack — `msls70` (Microsoft Line Services), `mspts70`,
`msptls70shared`, `pagelayout`, `MsoAria`, `riched20`. The last thing in the log
is Direct2D/DXGI work:

    dxgi:DXGID3D10CreateDevice Ignoring flags 0x20
    d2d:d2d_d3d_create_render_target Ignoring render target usage 0x2
    dxgi:dxgi_surface_GetDC ... semi-stub!

So Word initialises nearly everything it has and then refuses.

Ruled out this round, each measured:

* **The 197 in Excel's dialog is not a Win32 error at all.** Tracing
  `RtlNtStatusToDosError` and `SetLastError` over a whole Excel run finds no 197
  anywhere. It is an Office-internal code that Office rendered through
  `FormatMessage`, which picked an unrelated system string. Chasing it is a dead
  end — a good reminder that a plausible-looking number can be a coincidence.
* **The licensing registry is complete.** All eight subkeys under
  `HKCU\...\Common\Licensing` (`CachedLicenseData`, `LicensingExperience`,
  `ServicePlanFeatures`, the SKU GUID, …) match the source hive exactly.
* **Hardware graphics acceleration.** `DisableHardwareAcceleration`,
  `DisableAnimations` and `Avalon.Graphics\DisableHWAcceleration` all set — the
  dialog is unchanged, despite `dxgi_surface_GetDC` being a semi-stub.
* **`gfx.dll` and `aitrx.dll`**, the only two Office modules Windows loads that
  we do not, are both present in the prefix. Word simply never gets to them.
* **Word never touches the network.** No winhttp/wininet/DNS activity in a whole
  run, so this is not an online activation check timing out.

### Word's startup chain, by name

The published PDBs are **public-only**: `wwlib.pdb`'s TPI stream is 56 bytes
with zero type records, so there are function names but no enum members, no
locals, no line numbers. `scripts/pdb-enums.py` exists and works, but it has
nothing to read here — worth knowing before planning around type information.

The function names alone are enough to see the shape of the problem. `wwlib`
has a **`Boot` class with 188 methods returning `InitFailureReason`**, one per
startup step:

    Boot::IfrFirstBoot          Boot::IfrOleRegister      Boot::IfrInitAppDocs
    Boot::IfrFirstBoot2         Boot::IfrInitScreenRT     Boot::IfrInitDigSig
    Boot::IfrEnsureTHRCLS       Boot::IfrInitTaskManager  Boot::IfrInitLD
    Boot::IfrProcStartupPostIntl                          ... 188 in total

The dialog's format string lives in Word's own resource dll — `WWINTL.DLL`
carries `出现错误，<a> 不能启动。(<d>)`, where `<d>` is the number. So **error
(6) is one of these `Ifr*` methods returning `InitFailureReason` 6**, and the
question is now which one rather than what area.

Each has an RVA from the PDB, so the next step is mechanical: break on their
entry points, record the order they are called in, and read `rax` at the return
of the last one. No guessing about licensing required.

Tried and not useful here: the inherited `ntdll-tracelogging-decoder` patch does
enable Word's ETW providers (`EtwEventRegister enabling provider ...` for a
dozen of them), but Word writes essentially nothing before it fails — one event,
no payload. Its `EvtWordCoreBootStart`/`Stop` never fire.

### The startup structure, and why the PDB's addresses cannot be used

`wwlib` turns out to have a `Boot` class with a readable shape:

    Boot::FRun               Boot::ShowBootErrorEid    Boot::FAnyTaskFailed
    Boot::PrepareRun         Boot::LogBootTaskTelemetry
    Boot::FShowSplashScreenBasicChecks
    Boot::Ifr* x180          (each returning InitFailureReason)

`ShowBootErrorEid` is the dialog, and `FAnyTaskFailed` says startup is a task
list rather than a straight call chain. That is the map of the problem.

`tools/bootrace/` was written to walk it: a debugger (not an injected dll) that
arms `int3` at a list of RVAs, prints each as it is reached, restores the byte
and steps `rip` back. **It works** — armed on the export table's `FMain` and
`DllMain` it prints both, in order.

**But the published PDB's symbol addresses do not match this binary.** All 180
`Boot::Ifr*` breakpoints, and all 15 non-`Ifr` `Boot` methods, were armed
successfully and *never hit* — while `bootrace` proved itself on the same run
using export-table addresses. Cross-checking the three symbols that appear in
both places:

| symbol | export table | PDB (seg 1 + 0x1000) | difference |
|---|---|---|---|
| `DllMain` | `00176460` | `003ae2d0` | — |
| `DllGetClassObject` | `00f32e60` | `00e99a10` | `0x99450` |
| `DllCanUnloadNow` | `017b4bf0` | `0178ded0` | `0x26D20` |
| `FMain` | `01362cd0` | `01322510` | `0x407C0` |

Not a constant offset. Reading the bytes settles which is right: every export
address holds a real prologue (`mov [rsp+8],rbx; push rdi`, `sub rsp,28h`,
`push rbx`), every PDB address holds mid-function bytes.

This is not a parsing bug, and that was checked properly rather than assumed:

* The S_PUB32 record was dumped and read field by field
  (`reclen=30 kind=0x110e flags=2 off=0178ced0 seg=1`).
* Walking the symbol-record stream lands exactly on its end — 264,870 records,
  29,908,348 of 29,908,348 bytes, every one S_PUB32, none misaligned.
* **`pdb-symbols.py` agrees with `llvm-pdbutil` symbol for symbol.** On
  `c2r64.pdb`, which llvm *can* read, llvm reports `_GUID_0000000a_…` at
  `0002:407592` (decimal) and this reads `0002:00063828` — the same address.
* The PDB's own section table matches the PE's exactly, all eight sections.
* The PE has exactly one CODEVIEW debug entry, GUID/age `E529CCD6-…-2`, which
  is what was fetched.
* There are two `WWLIB.DLL` on disk with *different* symbol GUIDs — the one
  under `Updates\Apply\FilesInUse` is a different build — but `+loaddll`
  confirms Word loads `root\Office16\wwlib.dll`, the one this PDB belongs to.
* No constant offset explains it: a histogram of (nearest `.pdata` start −
  symbol offset) over 20,000 symbols is flat noise, top bucket 2.5%, and every
  one of the eight section VAs as a base gives under 2%.

One earlier check here was itself faulty and is worth flagging: comparing
symbol addresses against `.pdata` entries. x64 `.pdata` holds only non-leaf
functions and S_PUB32 holds plenty of data symbols, so a low hit rate proves
nothing — the control (`c2r64`, whose PDB llvm can read) scored just as low.
What actually decides it is the runtime: `bootrace` hits export-table addresses
and never hits PDB addresses, in the same run.

**So: names yes, addresses no.** Anything that needs an address has to recover
it from the binary — export table, or by finding the function some other way —
rather than trusting the PDB.

### The dialog comes from mso.dll, not wwlib

The `Boot::Ifr*` breakpoints never fired because they were in the wrong module.
Wine's relay log carries a `ret=` for every call, which is the caller's address,
so the question needed no symbols at all:

    user32.MessageBoxW(0, L"很抱歉，出现错误，Word 不能启动。(6)",
                       L"Microsoft Word 16.0", 0x1040)   ret=6fffec7960f7

Resolving that against the module table **from the same run** (ASLR makes a
table from another run useless, and Wine prints the bases in upper-case hex,
which cost two wrong answers before it cost a right one):

    0x6fffeb750000  mso.dll        <- ret lands here
    RVA 0x10460f7, inside the function starting at RVA 0x10460b0 (+0x47)

So the dialog is raised by **`mso.dll`, Office's shared layer**, not by Word's
own `wwlib`. The format string living in `WWINTL.DLL` is only a resource; the
code that formats and shows it is in mso.

Worth noting from the same log: the line immediately before creates Word's real
main window — `CreateWindowExW(..., L"OpusApp", L"Microsoft Word", ...)`. Word
gets all the way to having a main window before this fires.

`MSO.pdb` has the same address problem as `wwlib.pdb` (`DllGetLCID` differs from
the export table by 0x9BE0), so the function at `0x10460b0` has no name yet. It
does have an address, which is what a breakpoint needs.

## Error (6), named: the App-V registry tokens were never expanded (2026-09-01)

The winedbg backtrace of the thread sitting on the dialog is short enough to
read end to end:

    user32.MessageBoxW
    mso            +0x10460f7
    mso30win32client +0x683833
    wwlib          +0x1421b3d
    wwlib          +0x616eb9
    wwlib          +0x1004abc     <- the caller that decides the number
    wwlib          +0xbf1079
    winword        +0x1368

Disassembling the frame that decides the number, `wwlib+0x1004a00`, gives the
whole answer in twenty instructions:

    call  [mso!#36903]        ; a process-wide singleton
    xor   ebx, ebx
    test  rax, rax
    jne   done                ; non-NULL -> return 0
    ...
    lea   edx, [rbx+0x42]
    lea   ebx, [rdx-0x3c]     ; ebx = 0 + 0x42 - 0x3c = 6
    call  wwlib+0x616e10      ; "Word cannot start (6)"

So "(6)" is not a stage index. It is a constant added at one call site, and
that call site is reached by exactly one condition: `MSO.dll` ordinal 36903
returned NULL. That ordinal is a magic-static that calls
`Mso30Win32Client!#24676(0x4d, ...)` once and caches the result, which
dispatches to `Mso98win32client+0xae788` — a function that names itself in its
own trace string:

    L"[T%d]%s: called on token %#x (%s)"   with   L"LoadLocalizedLibraryCore"

Breaking there with `bootrace -wstr rbp+0x3c0` (the buffer the function fills
with the library's name) says which library token `0x4d` is, and what path was
composed for it:

      3 after_path
          rbp+960  = "MSPTLS.DLL"
          rbp-96   = ""

An empty path. `MSPTLS.DLL` is present -- twice -- in the copied installation,
so nothing was missing; the path composer produced nothing. It composes from
`HKLM\Software\Microsoft\Office\16.0\Common\InstallRoot\Path`, and that value
in the prefix read:

    "Path" = "[{AppVPackageRoot}]\Office16\"

That is an App-V token. The virtual registry stores every path that way, and on
Windows the tokens never reach the application: `AppvIsvSubsystems64.dll` hooks
the registry APIs and substitutes them on the way out. This fork replaces that
dll with a stub that returns 1, so nothing substituted them.

2623 `[{AppVPackageRoot}]` plus ten other tokens, all in the vreg dumps and
none in the real hives -- which also explains why applying the real HKLM tree
after the vreg did not fix it: the real hive has no `InstallRoot\Path` at all,
only a `Virtual` subkey. The token *is* the value.

`export-office-registry.py` now substitutes them at export time, in value data,
value names and key paths -- Office keys its ChangeNotify and compatibility
tables by path, so data alone would not have been enough. Two things to know if
this is ever revisited: the test for a token has to be done on UTF-16 bytes
(`b"[\x00{\x00"`), because as ASCII it never matches; and `user.reg` has its own
`[{` strings that are JSON, not tokens.

## Then it crashed, and Windows said why (2026-09-01)

With the paths expanded, Word got past (6) and died instead:

    Unhandled page fault on read access at vcruntime140+0xf0e0 (wcsstr)
    mso +0x2d75f7 ... mso +0x9d5ba ... wwlib +0x1b3096d ... winword+0x1368

`mso+0x2d75e3` is `mov r15,[rbx+0x30]` followed by
`wcsstr(r15, L"\\?\Volume{")`, and `[rbx+0x30]` held garbage. Reading back up
the function gives what fills it:

    GetVolumePathNameW(path, buf, 0x104)
    GetVolumeNameForVolumeMountPointW(buf, guid, 0x104)
    CreateFileW(guid, 0, 0, NULL, OPEN_EXISTING, 0, NULL)
    if (handle != INVALID_HANDLE_VALUE) {
        info->Version = 2;
        r = VirtDisk.GetStorageDependencyInformation(handle, 1, 0x400, info, &size);
        if (r == ERROR_INSUFFICIENT_BUFFER) retry;
        if (r == 0) use info->Version2Entries[0].HostVolumeName;   <- here
    }

Office reads entry 0 whenever the call succeeds and never looks at
`NumberEntries`. Wine's stub returned `ERROR_SUCCESS` with `NumberEntries = 0`
and left the rest of the buffer untouched, so Office read whatever was there.

Rather than guess what Windows does, the same sequence was compiled with mingw
and run on both sides. On Windows 11:

    volume guid: '\\?\Volume{69a2ea4e-...}\'
    CreateFileW(guid with trailing backslash) -> INVALID_HANDLE_VALUE (err 3)
    CreateFileW(guid without it)              -> ok
    GetStorageDependencyInformation(flags=1)  -> 0xc03a0015, used=0
    small buffer (8)                          -> 87 (ERROR_INVALID_PARAMETER)

and under this fork's Wine, before the fix:

    CreateFileW(guid with trailing backslash) -> handle 0x34 (err 0)
    GetStorageDependencyInformation(flags=1)  -> 0 (success), entry = poison
    small buffer (8)                          -> 0

Two divergences, either of which is enough on its own. The one fixed here is
virtdisk: `ERROR_VIRTDISK_NOT_VIRTUAL_DISK` is both what Windows answers and
the honest answer for a Wine that has no virtual disk stack at all. The other
-- Wine opening `\\?\Volume{...}\` where Windows returns ERROR_PATH_NOT_FOUND
-- is left alone for now; it is in path handling, it is riskier to touch, and
with virtdisk correct Office reaches the same branch it reaches on Windows.

## Where it stops now: the licence (2026-09-01)

Word now builds its entire UI. `winedbg`'s `info window` shows the real thing,
not a splash screen:

    OpusApp                     "Word"        <- Word's main frame
      FullpageUIHost / NetUIHWND
      _WwF                                    <- the document frame
      MsoCommandBarDock             x4        <- the docks
      MsoWorkPane / NUIPane / NetUIHWND
    NUIDialog                   "Microsoft Word"

and the main thread sits in `DispatchMessageW` inside `mso40uiwin32client` --
a modal dialog's message pump, not a hang.

The dialog paints as an empty rectangle, which made it unreadable by
screenshot. Office draws its own dialogs: a `NUIDialog` has one `NetUIHWND`
child and no controls, so there is nothing for a screenshot to fall back on
when the drawing is the broken part. MSAA asks the application for the text
instead, which works whether or not a pixel reached the screen --
`tools/uidump` does that:

    NUIDialog "Microsoft Word" 375x177
      NetUIHWND
        [graphic]      name="错误图标"
        [static text]  name="Microsoft Office 无法验证此产品的许可证。
                             应使用控制面板修复 Office 程序。"
        [push button]  name="确定"
        [push button]  name="帮助"

So the remaining gate is Office licensing, not Wine. Word is otherwise up.
Immediately before the dialog, Office activates
`Windows.Security.Authentication.OnlineId.OnlineIdSystemAuthenticator` and then
launches `winebrowser.exe` -- it is trying to sign in. The supported way past
this is to sign in to the Microsoft 365 account inside the prefix, which needs
the account holder's credentials.

Certificate work is not the problem: with `+crypt` on, every
`CertGetCertificateChain` returns 1 and `CertVerifyCertificateChainPolicy`
returns no error.

Still open, all measured, none of them the licence gate:

  - `Windows.Security.EnterpriseData.ProtectionPolicyManager` (6 activations)
  - `Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager` (2)
  - `Windows.System.Profile.SharedModeSettings` (1)
  - msxml3 rejects Office's Ribbon extensibility schema: libxml2 calls an
    attribute that appears both directly and through an attributeGroup a
    "Duplicate attribute use", MSXML accepts it
  - `{99d651d7-5f7c-470e-8a3b-774d5d9536ac}` (VSTOAddinLoader) is not
    registered, because the VSTO runtime lives outside the Office package and
    was never copied

## The per-user half was in a profile Office never opens (2026-09-02)

The licence dialog had been read as a licensing problem. It was, but not the
one it looked like: **none of the per-user import was where Office looks for
it.** `WINEDEBUG=+file`, one run, says it in a line:

    GetFileAttributesExW L"C:\users\crossover\AppData\Local\Microsoft\Office\Licenses\5" 0 ...
    NtQueryFullAttributesFile L"\??\C:\users\crossover\...\Licenses\5" not found (c000003a)

`import-office.sh` copied the licence, the identity caches and the Roaming
templates into `C:\users\$USER` — `user`. The CrossOver base this fork
builds on names its Windows user **`crossover`**, always, whatever `$USER` is:

    dist/bin/wine     -> USERNAME=user    USERPROFILE=C:\users\user
    dist-cx/bin/wine  -> USERNAME=crossover   USERPROFILE=C:\users\crossover

and `run-word.sh` defaults to `dist-cx`. Counted over the same run: 744 + 332
path references under `crossover`, and the only `user` ones are `%TEMP%`.
So Office was reading an empty profile one directory away from the whole
subscription, and reporting itself unlicensed — which is the honest answer to
the question it was actually asking.

`import-office.sh` now asks the runtime instead of assuming:

    WIN_PROFILE="$("$WINE" cmd /c 'echo %USERNAME%')"

The failure mode is worth remembering because nothing about it looks like a
bug: every copy succeeds, every file is present, and the application is right.

### What that did not fix, measured

Putting the licence in the right profile changed nothing observable — the same
`NUIDialog`, the same window set. Two further things were ruled out and one
was named:

  - **Expiry is not the gate.** The imported token had `NotAfter
    2026-08-02`, already past. A valid one taken from another machine of the
    same subscription (`NotAfter 2026-11-14`, same `UserId`) produces an
    **identical** dialog. `HardwareId` is per-machine and differs between the
    two, so a token issued elsewhere is not expected to satisfy this device
    either -- but expiry, at least, is not what is being complained about.
  - **The dialog cannot be read the usual two ways.** It paints nothing (a
    1920x1080 screenshot holds 12 colours), and MSAA returns empty names for
    every static in it, so `tools/uidump` gets the shape -- error icon, a link,
    a check button, six buttons, one of them `确定` -- and no text.
  - **Office's own identity cache does not decrypt.** 259
    `CryptUnprotectData` calls fail, every one of them with
    `unrecognized CryptProtectData block` / `info0 magic value not matched`.
    Attributing each failure to the file its thread opened last puts all 259
    in one place:

        C:\users\crossover\AppData\Local\Microsoft\IdentityCache\1\UD\
            u_*\e_C2GK9UTC67FSUCG3\{Accounts,AT,ID}\*.bin

    These are Windows-sealed DPAPI blobs. Wine's `CryptProtectData` derives its
    key from the user name, a fixed in-tree secret and a salt carried in the
    blob -- there is no machine master key -- so it can only open what Wine
    itself wrote, and it says so rather than guessing. Office reads the cache,
    gets nothing back, and concludes it is not signed in.

  - `webauth.FindAccountAsync` is a stub, and Office calls it with the exact
    account the licence names (`0003BFFD6D5A3345`, matching the token's
    `RenewalToken` identity). So a fresh in-prefix sign-in has a second gap
    waiting behind the first.

  - Office never calls `CryptProtectData` in these runs, so the cache cannot
    heal itself even once something else is fixed.

### Bridging DPAPI, and the precondition that makes it possible

The one measurement that decides whether the cache can be transplanted at all:
**Office reads these records with `pOptionalEntropy = NULL`.** Wine's `report()`
dumps the entropy blob whenever it is non-NULL, and across all 259 calls it
dumped none. Since Wine's key is (user name + fixed secret + stored salt +
entropy), a record re-sealed with NULL entropy **as the user Office runs as**
is a record Office can open.

That gives a bridge with no guessing in it: recover the plaintext on the
Windows machine that owns the master key, and re-seal it with this Wine's own
`CryptProtectData` from inside the prefix, as `crossover`. The plaintext never
needs to touch disk on this side -- it can stream from ssh straight into the
re-sealing tool.

Two traps found on the way, both cheap to repeat by accident:

  - **Opening a live Office telemetry `.db` read-write destroys its WAL.**
    `OTele\winword.exe.db` had a 1.2 MB `-wal`; a single `sqlite3 .tables`
    against the file checkpointed and deleted it, and the events it held were
    already drained. Copy `db` + `-wal` first, query the copy.
  - **The PDB address skew is not a constant**, so it cannot be corrected for.
    `MSO.pdb`'s GUID/age match the shipped `MSO.DLL` exactly, and yet across
    the 62 exported names that appear in both, the PDB-to-binary delta takes
    **36 distinct values** (`DllGetLCID` -0x9BE0, a block of `IMsoNotebook*`
    -0x25740, `FileIO` ones -0x1EEE0), all inside `.text` and all in segment 1.
    An earlier session recorded this for one symbol; it holds generally.
    Names yes, addresses no -- confirmed, not merely inherited.

## The broker had to answer, not fail — and then Office went further (2026-09-02)

`Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager` is the
sign-in broker, and Office asks it about the account its own cache names
(`0003BFFD6D5A3345`, the same id the licence's `RenewalToken` carries). In the
in-tree WAM shim, `FindAccountAsync` was one of the `STUB_ASYNC` macros:

    #define STUB_ASYNC(name, sig, args) \
    static HRESULT WINAPI name sig \
    { FIXME args; if (result) *result = NULL; return E_NOTIMPL; }

which is the sppc mistake again in a different dll. "There is no account with
that id" and "this API does not exist" are not the same answer: on `E_NOTIMPL`
Office concludes the broker is broken and stops asking, where a **completed
operation carrying no account** is something it acts on — and it is what
Windows answers for an id the broker does not hold. Its neighbour
`GetTokenSilentlyAsync` already had this right, returning
`create_token_result_operation(3, NULL, ...)` — status 3, *interaction
required* — instead of failing.

The operation object could not express "completed, carrying nothing" — three
places `AddRef`/`Release`d the payload unconditionally — so those were made
NULL-tolerant, `FindAccountAsync` now returns `create_object_operation(NULL,
result)`, and `FindAllAccountsAsync` was pointed at `create_findall_operation`
like the `WithClientId` sibling right below it that already answered.

**Measured effect, same prefix, one variable changed:**

| `WINEDEBUG=+webauth` | before | after |
|---|---|---|
| `FindAccountAsync` | 19, all `E_NOTIMPL` | answered |
| `GetTokenSilentlyAsync` | — | 20 |
| `FindAllAccountsWithClientIdAsync` | — | 40 |
| `request_get_Properties` / `string_map_Insert` | — | 203 |
| `response_get_Token` | — | 20 |

So Office stops treating the broker as broken and walks the whole token
acquisition path: it builds token requests, resolves the provider
(`https://login.microsoft.com`, authority `consumers` — the consumer MSA
provider), and reads tokens back out.

The dialog is unchanged, and that is the honest result: this removed a gap,
it did not remove the last one.

### Where it stops now, and it is one file

The shim already has the injection point: `GetTokenSilentlyAsync` reads
`Z:\tmp\office-wam-token.txt` and, if it can, hands the contents back as a
successful token response. That file exists, and the log proves it is being
used — across 20 calls there is **not one** `no authorized token available`
line, and `response_get_Token` fires 20 times. Office is being handed a token,
is reading it, and is still refusing.

The token was written at 18:44 the previous day and read back after 04:00, so
it is roughly nine hours old against an MSA access-token lifetime of about one
hour. **The next thing to try is simply a fresh token in that file** — no DPAPI
work, no identity-cache transplant, no credentials leaving any machine. If a
fresh token still leaves the dialog up, then the token's audience is wrong
rather than its age, and the request properties Office fills in (203
`string_map_Insert` calls on each request) name what it is actually asking for.

Worth keeping in view: the licence still carries another machine's
`HardwareId`, so even a successful sign-in has to result in Office obtaining a
licence for *this* device rather than validating the transplanted one.

## The gate is local, and it is the HardwareId (2026-09-02)

Three measurements, in this order, moved the answer a long way from where it
looked like it was.

**1. Office never asks a licensing service.** `WINEDEBUG=+winhttp,+wininet`
over a whole run reaches exactly four hosts:

    mobile.events.data.microsoft.com      telemetry, 8x /OneCollector/1.0/
    support.content.office.net            word_whatsnew.xml
    relcomms-prod-...b02.azurefd.net      content
    wus-000.odc.officeapps.live.com       /restore

No `ols.officeapps.live.com`, no `licensing.mp.microsoft.com`, nothing. So
whatever decides "unlicensed" decides it **offline**, and no amount of token
freshness reaches that decision -- a token Office never spends cannot be the
thing stopping it.

**2. Office does read the licence.** With the token finally in the profile the
runtime uses, `+file` shows the whole sequence on it: `FindNextFileW` enumerates
`Licenses\5\*`, then `CreateFileW`/`NtCreateFile` open the token itself. It also
looks for `Licenses\5\Grace\0` and `ProgramData\...\Licenses\5\Perpetual` and
finds neither. So the file is found, opened, parsed -- and rejected.

**3. A working machine has none of the identity state we were chasing.** The
same Office, licensed and working, on the machine the fresh token came from:

    HKCU\...\Common\Identity\Identities            0 subkeys
    HKCU\...\Common\Identity\ConnectedAccountCID   (empty)

This prefix has `ConnectedAccountCID` **set**, inherited from the imported hive,
and the working machine does not. So the empty `Identities` key here was never
the problem, and the identity registry is not what separates licensed from not.

Put together: what makes the reference machine licensed is a signed licence
whose `HardwareId` matches the device it is on, validated entirely offline. The
token here is signed, in date and for the right subscription, and the one field
left that can disqualify it is the `HardwareId`, which belongs to the other
machine.

**This is not something to work around.** Making this prefix compute the other
device's `HardwareId` would be defeating the device binding, and the allowance
the subscription grants is counted in exactly those ids. The legitimate route is
the one the subscription already provides: a one-time online activation issuing
a licence for *this* device, after which it validates offline the way the
reference machine's does. That is gated on sign-in working, which is the WAM
shim, which is where the remaining work is.

### Two things ruled out along the way, cheaply

  - **Office was chasing the wrong account, and that was self-inflicted.**
    Migrating the identity caches into the right profile also brought in
    OneAuth's five account records -- three for work/school tenants, two
    consumer. Office picked a work account with no relationship to the
    `O365HomePremRetail` subscription installed here, and put a `LoginHint` for
    it on every request. Narrowing `OneAuth/accounts/` to the one CID the
    licence itself names removed the hint and a stray `claims` property. It did
    not change the dialog, which is consistent with measurement 3.
  - **`get_UserName` is not read.** The shim returned an empty user name while
    `get_Id` answered properly from `ConnectedAccountCID`; that asymmetry looked
    like a candidate. Counted over a run, Office calls `account2_get_Id` 16
    times and `get_UserName` **zero**. Making the name resolve is still the more
    honest answer, and it is not the gap.

Account identifiers are deliberately not written down here; they are in the
prefix's own hive and OneAuth records for anyone who needs them.

## The WAM broker is finished, and it is not the gate (2026-09-02)

The previous section ended by saying the remaining work was the WAM shim. It
was; it is now done, in the sense that matters: **over a full Word start-up not
one call into the broker reaches a FIXME**, and nothing crashes. What that
bought is written below, and so is the measurement that says it is not enough,
because that is the more useful half.

### First, a trap that invalidated four hours of measurement

Since Wine 9 a prefix's `C:\windows\system32` holds real copies of the builtin
PE dlls, and the loader takes them from there. `make install` updates `dist-cx`
and changes nothing about what Word loads. No error, no missing file — the run
simply exercises whichever build the prefix was made from.

This was found the only way it can be found, by disbelieving a log: the source
had a `TRACE` where the log printed `fixme`. The dll in `system32` was four
hours and two edits behind its source. Every conclusion drawn about the broker
that afternoon had been drawn about a binary nobody had built that day.

`scripts/sync-prefix-dlls.sh` now pushes built builtins into the prefix. It
copies only files the prefix already has, and only where the prefix's copy is
itself marked `Wine builtin DLL`, because `import-office.sh` deliberately puts
Microsoft's own VC runtimes in `system32` under native overrides and those must
survive. On the first run it refreshed one dll and left fifteen alone.

> Run it after every build, before every measurement. A stale deployed binary
> and a wrong conclusion look identical from the log.

### What answering everything changed

Each of these was made only after a log said Office was asking. The rule is
sppc's, for the third time in this project: refusing a call and answering it are
not the same thing to the caller.

  - `FindAccountAsync` answered "no such account" for the very CID the broker
    hands out on every token response. Office asks 15 times per start-up. It now
    returns the account.
  - `FindAllAccounts*` reported an empty list 35 times in a run that took 16
    tokens for the account it said did not exist.
  - `FindSystemAccountProvider*`, `RequestToken*` and `InvalidateCacheAsync`
    were `E_NOTIMPL`; the first two have non-system siblings that already answer.
  - `WebTokenResponse.Properties` returned NULL, which is not "no properties"
    but a pointer the caller must guess is absent.

Two of these needed something that was not guessable:

  - **`IAsyncAction` is not `IAsyncOperation<T>`.** They share every vtable slot
    but the last, where `GetResults` takes no out parameter. Returning an action
    through the operation vtable makes `GetResults` write through whatever is in
    the second argument register.
  - **A WinRT map is iterable, and that is not optional.** Giving Office a real
    property bag instead of NULL made it immediately ask that bag for
    `IIterable<IKeyValuePair<String,String>>`; `E_NOINTERFACE` took Word down
    with an unhandled `0xe0000002`. Implementing `IIterable`/`IIterator`/
    `IKeyValuePair` fixed it. Note the shape of this: a change that was strictly
    more correct crashed the process, because it moved Office onto a path the
    shim had never been asked to walk.

Tokens are now looked up per scope. Office does not ask for "a token": one
start-up makes 16 requests, ten for `service::officeapps.live.com::MBI_SSL_SHORT`
and the rest for graph, the consent service, ads and `ssl.live.com`. One string
returned for all of them is a fixed answer shaped like a token.

### And then Office still did not go and get a licence

With the broker answering everything, three measurements say the licence is not
downstream of it.

**1. Word never contacts a sign-in or licensing host.** Traced across all three
user-mode stacks at once (`+winsock,+wininet,+winhttp`), a full start-up
resolves exactly five names:

    mobile.events.data.microsoft.com     telemetry
    support.content.office.net           what's-new
    relcomms-prod-...azurefd.net         release comms
    discovery.svc.cloud.microsoft        service discovery
    wus-000.odc.officeapps.live.com      /odc/servicemanager/catalog

Every request returned 200. There is no `ols.officeapps.live.com`, no
`login.live.com`. The token is fetched from the broker sixteen times and never
spent. Earlier this was measured with WinHTTP alone, which was not enough to
support the claim; all three stacks now say the same thing.

**2. Removing the licence entirely changes nothing.** With
`Licenses\5\<id>` moved aside so Office had no licence at all, the dialog was
byte-identical and Word still contacted no licensing host. A program that wanted
a licence and had none would ask for one.

**3. Office's own record says it rejected the licence and stopped.** Comparing
`HKCU\...\Common\Licensing` against the export from the machine where Office
works — the same values, imported into this prefix, after Office had run here:

    NextUserLicensingLicenseIds        winref: CWW_00000000-...   here: ""
    NextUserLicensingLicensedUserIds   winref: 0000000000000000   here: ""
    EligibleForExtendedGrace           winref: 0                  here: 1

Office read the licence, rejected it, **cleared** the licence/user id pair it
had been given, and moved the install into extended grace. That is the whole
state machine, in Office's own handwriting, and it has no "go and fetch one"
step in it.

  - `PendingAcids` holds two ACIDs here, which looked like pending activation
    until the same two turned up unchanged in the working machine's export. It
    is steady state, not a request.

**4. Office will not start an interactive sign-in through the broker.** Told
honestly that it has no token for a scope (`UserInteractionRequired`), Office
asks 4 times instead of 16, never calls `RequestTokenAsync`, and quietly does
less than before. There is no fallback to reach for here.

`OLicenseHeartbeat.exe` — the one binary whose entire job is licence renewal —
runs to completion under Wine, exercises the broker 94 times, fetches its ECS
config, asks for exactly one token (Graph), and contacts no licensing service
either.

### Where that leaves it

The broker is no longer a suspect, and that is worth having: it answers
everything Office asks, consistently, without crashing, and any later attempt
needs that underneath it. But sign-in was the last thing standing between this
prefix and an activation attempt, and with sign-in satisfied Office still never
makes one. The gate found in the previous section stands, and the route to it
is not through WAM.

The next thing to measure is what decides, inside Office, that an unlicensed
install should sit in extended grace rather than activate — that decision is
made locally, it is made with the network available and working, and it is made
in the same run whose registry writes are quoted above.

## Following the grace decision into Office's own telemetry (2026-09-02)

The previous section ended with a question: what decides, locally and with the
network working, that an unlicensed install should sit in extended grace rather
than activate? This is how far that got, and what it eliminated.

### The decision is made once, so you have to put the state back to watch it

A `+reg` trace of a normal start-up shows **no** write to any licensing value.
Office decided on some earlier run and now boots into the settled state. To
observe a decision you have to restore the pre-decision state first — here, the
two id values and `EligibleForExtendedGrace=0` copied back from the working
machine's export. Then it happens in one place:

    NtOpenKeyEx ... MachineGuid            (read, ~20 times a run)
    RegQueryValueExW  NextUserLicensingLicenseIds -> 206 bytes
    NtSetValueKey     NextUserLicensingLicenseIds <- 2 bytes   (empty)
    NtSetValueKey     NextUserLicensingLicensedUserIds <- 2 bytes
    NtSetValueKey     EligibleForExtendedGrace <- 1

`MachineGuid` being read immediately before is independent corroboration of the
`HardwareId` finding two sections up, arriving from a different direction. The
prefix has one (`HKLM\Software\Microsoft\Cryptography`), so Office can compute a
device identity for *this* machine; it simply is not the one the licence names.

### A genuinely fresh install does not activate either

Deleting the whole `HKCU\...\Common\Licensing` subtree and moving the licence
file aside puts the prefix where a never-licensed install is. With a signed-in
user and a complete broker, that run asks **nine times** for
`service::officeapps.live.com::MBI_SSL_SHORT` — the licensing audience — takes
the token, resolves two hosts (telemetry and service discovery), and writes:

    EligibleForExtendedGrace          1
    NextUserLicensingLicensedUserIds  ""
    CachedLicenseData\winword.exe     03 99 99 99 ... (a 0x99 placeholder GUID
                                      where the working machine has a real one)

So this is not the stale licence poisoning a retry. Office will not activate
from a clean slate either.

### Three more things ruled out

  - **The token's content is not what stops it.** Replacing the token file with
    937 bytes of ASCII filler produced an identical run: 14 requests, token
    handed over 14 times, same hosts, same dialog. Note what this does and does
    not show — both strings are invalid, so identical behaviour is equally
    consistent with "Office ignores the content" and "Office parses both and
    rejects both". What it does establish is that whatever rejects the token
    does so **before any network call**, so no amount of obtaining a *valid*
    token can be tested from here without first obtaining one.
  - **The service catalog is not the licensing directory.** Office's one
    `discovery.svc.cloud.microsoft` call 302-redirects to a regional ODC host
    and returns 200. Fetching the same document shows it is the *storage*
    services catalog — OneDrive, Dropbox, Box, SharePoint — with no licensing
    endpoint in it. It was worth reading rather than assuming.
  - **The App-V layer is never consulted.** Word loads
    `AppvIsvSubsystems64.dll` (Microsoft's real one, reached through a symlink;
    `tools/appv-stub` is not in use). A logging forwarder in front of it —
    `tools/appv-stub/forward.c`, all seven exports passed straight through —
    recorded the process attaching and **not one call to any export** in a full
    start-up. Whatever decides this install's Click-to-Run status, it does not
    ask the virtualisation layer. Word also never opens
    `HKLM\...\ClickToRun\Configuration`, which is fully populated here.

### The one lead worth taking next

Office's telemetry database names its own licensing activities. Copy
`OTele\winword.exe.db` **with its `-wal`** before touching it — sqlite
checkpoints and deletes the WAL on first open, which is where all the recent
events live — and read the WAL with `strings` first:

    Office.Licensing.LicExitOfficeProcess   101
    Office.Licensing.FullValidation          58
    Office.Licensing.OnlineRepair            38
    Office.Licensing.Nul.Storage.LoadModelFlow  14

Each activity carries `Activity.Result.Code` with `Activity.Result.Type` =
`HRESULT`. Decoding the field that follows:

That instrument now exists: `scripts/otele-events.py`. The payloads are Bond
CompactBinary, but the interesting part decodes without the schema because each
value is preceded by its own name as a string. The framing is the whole trick,
and getting it wrong is silent: a field header is `(id << 5) | type`, so the
byte after a name is **not** the value. Reading it as one turns `0xc004f015`
into a 38-bit number that fits no integer type — which is exactly the wrong
answer this section originally reported, before the header was accounted for.

    session        seq   activity                            dur    ok  result
    7bHRxxhSrk+2XU .23   Office.Licensing.FullValidation     984    no  0xc004f015
    7bHRxxhSrk+2XU .26   Office.Licensing.OnlineRepair         2   yes  S_OK
    7bHRxxhSrk+2XU .51   Office.Licensing.FullValidation     793    no  0xc004f015
    7bHRxxhSrk+2XU .54   Office.Licensing.OnlineRepair         2   yes  S_OK
    7bHRxxhSrk+2XU .59   Office.Licensing.LicExitOfficeProcess  2 yes  0xc004f015

**`0xC004F015` is `SL_E_LICENSE_NOT_INSTALLED`**, and that is Office's own
verdict — Wine returns that code from nowhere; `grep` across the whole tree
finds it in no dll of ours.

The ordering is the part that matters, and it is identical in every session in
the store: validation fails, **online repair runs because it failed**, online
repair returns `S_OK` in 2 units against validation's 800-5000, validation fails
again with the same code, and the process exits carrying it. So this is not an
activity that no-ops because it has nothing to do — it is invoked as the remedy
for the failure that just happened, and it reports that it applied one. Whatever
it is doing, it is not going online: the same run resolves no licensing host at
all.

That is the sharpest lead left. Four candidate reasons for a no-op that returns
success were tested and none of them survived:

  - **Consent.** `HKCU\...\Common\Privacy\SettingsStore` is byte-identical to
    the working machine's export, `DisconnectedState=1` included.
  - **Connectivity.** Office asks `INetworkListManager::IsConnectedToInternet`
    23 times a run; the CrossOver base already forces it to `VARIANT_TRUE`
    (its own hack for bug 14719), so Office is told it is online.
  - **The audience mismatch.** Word asks ECS under `Other/Unknown` where
    `OLicenseHeartbeat` asks under `Production/DCWin8`; fetching the config for
    `Other/Unknown`, `Production/DevMain` and `Production/DCWin8` returns the
    same 1263 bytes with the same licensing keys. The label is not load-bearing.
  - **The App-V layer**, ruled out above.

One measurement not yet made would settle a great deal: the same three lines
from the *working* machine's `OTele\winword.exe.db`. If `OnlineRepair` there
also returns `S_OK` in 2 ticks, this is normal and the defect is upstream of it;
if it takes hundreds of ticks and touches the network, then this activity is
being short-circuited here and finding what short-circuits it is the whole job.

Two notes for whoever picks this up. Word reports `Release.Audience = Not_C2R`
and `Release.Channel = Unknown` in every event, and asks ECS for
`/config/v2/Office/word/16.0.20208.20000/Other/Unknown`, while
`OLicenseHeartbeat.exe` on the same install asks for `.../Production/DCWin8`.
Whether that mismatch matters is unmeasured — it may be nothing more than a
telemetry label — but it is the one place where this install visibly does not
describe itself as Click-to-Run, and vNext licensing is a Click-to-Run feature.
The other: Office also reports `Unknown Manufacturer` / `Unknown Model`, which
is what Wine's SMBIOS looks like from inside.

## The working machine's telemetry, and what it does and does not settle (2026-09-02)

The previous section proposed one measurement that would settle a great deal:
the same activity lines from the machine where Office is licensed. Copying
`OTele\winword.exe.db` **and its `-wal`** off that machine (never open it with
sqlite first — it checkpoints and deletes the WAL) gives the answer, and it is
not the answer that was hoped for.

**There are no `Office.Licensing.*` events there at all.** A complete Word
session — file open, save, quit, 62 activity records — emits none. So the
comparison cannot be made: `FullValidation` and `OnlineRepair` only exist on an
install that is failing. Their absence on a licensed machine is consistent with
"nothing to validate, nothing to repair" and tells us nothing about how long
`OnlineRepair` would take if it had work to do. That line of enquiry is closed,
not answered.

### But the same file settles the Click-to-Run question, hard

The audience labels dismissed earlier as "possibly just a telemetry label" turn
out to be a real, measured divergence:

                            winref (works)         this prefix (fails)
    Release.Audience        Insiders_DevMain    Not_C2R
    Release.AudienceGroup   Insiders            Other
    Release.Channel         DevMain             Unknown
    App.Branch              Devmain             Devmain

`App.Branch` resolves in both, so Office is finding *some* of its configuration
here. And the cache it writes that from is per-application, which makes the
divergence sharper still. `HKCU\...\Common\ExperimentConfigs\Ecs\<app>`:

    contextualactionsserver, excel, officeclicktorun,
    opushutility, outlook, powerpoint, protocolhandler,
    publisher, sdxhelper                       Insiders_DevMain   (imported)
    olicenseheartbeat                          DCWin8_DevMain_Insiders
    word                                       Not_C2R

The bottom two rows were written **by runs in this prefix**. The imported value
for `word` was `Insiders_DevMain`; Word overwrote it. So on one machine, with
one registry and one Click-to-Run configuration, `OLicenseHeartbeat.exe`
resolves a proper Click-to-Run audience and `WINWORD.EXE` concludes it is not a
Click-to-Run install at all.

That matters because vNext licensing is a Click-to-Run feature, and it is the
best available explanation for an `OnlineRepair` that returns `S_OK` in two
ticks without going online: an install that does not believe it is C2R has
nothing to repair online.

It is a hypothesis, not a finding — what has been *measured* is only that the
two processes disagree. What Word consults to reach `Not_C2R` is still unknown,
and three obvious candidates are already eliminated:

  - it never opens **any** `ClickToRun` registry key (zero hits in a full `+reg`
    trace of a start-up, while `HKLM\...\ClickToRun\Configuration` is fully
    populated with `AudienceData="Insiders::DevMain"`);
  - it never opens any `ClickToRun`, `Integrator` or `AppVManifest` **file**
    (zero hits in a 433,000-line `+file` trace, in a run that opens 1,557 paths
    under the package root);
  - it never calls the **App-V layer** (all seven exports, logging forwarder,
    zero calls).

So the detection is none of the three. Finding it is the next job, and unlike
everything before it there is now a control to check any answer against: the
same binary on the same install reaching the opposite conclusion depending on
which executable is running.

### The token experiment, run: valid tokens change nothing

This was the last open question — whether Office declines to spend the token
because the token is bad. It is not. Answered with a real credential rather
than an inference:

`mint.ps1` asks the working machine's own WAM broker for all five scopes Office
requests here and writes one file per scope, named by the same sanitisation the
shim uses, so the output drops straight into `Z:\tmp\office-wam-tokens`. It
cannot run over SSH — Windows OpenSSH gives a service logon, and WAM answers
`0x800703F0 ERROR_NO_TOKEN` to `FindAccountProviderAsync` from one. Run it in
the interactive desktop session (a scheduled task registered with `/it` borrows
the console session's token).

All five minted. The `officeapps.live.com` token hashes differently from the
day-old one, so it is genuinely newly issued, not WAM handing back its cache.
The shim then served every request from the store — 15 lookups, **0 misses**,
nine of them the licensing audience — and the run is indistinguishable from
every run before it:

    Office.Licensing.FullValidation     512    no  0xc004f015
    Office.Licensing.OnlineRepair         2   yes  S_OK
    Office.Licensing.FullValidation     595    no  0xc004f015
    Office.Licensing.OnlineRepair         2   yes  S_OK

Same hosts (telemetry, discovery, ECS), no licensing endpoint, same dialog.

**So the token is not the gate, and never was.** The earlier garbage-token test
could not distinguish "ignores the content" from "parses both and rejects both";
this one can, and the answer is that a valid, unexpired, correctly-scoped token
for the licensing audience changes nothing. Nothing downstream of the broker is
waiting on a better credential — which also closes the last reason to do more
work on the WAM shim for licensing's sake.

Delete the minted files when done; they are live credentials with a short life.

## Chasing `Not_C2R` with the control group (2026-09-02)

The audience divergence had a control the rest of this investigation never
had: two processes on one install reaching opposite conclusions. This is how
far that got, what it eliminated, and one correction to the section above.

### Correction: the earlier "never reads it" evidence was weaker than stated

That section reported that Word never opens any `ClickToRun` registry key or
file. Both measurements were taken on runs where `FlightCacheAudience` was
**already cached** — so Word had no reason to look, and a cached decision was
being read as an absence of one. Same family of mistake as the stale deployed
binary: measuring something that had already been decided earlier.

Deleting `HKCU\...\Common\ExperimentConfigs\Ecs\word` forces the recompute. The
conclusions survive re-measurement — on a run that *does* recompute, Word still
opens `ClickToRun\Configuration` zero times and touches no `ClickToRun` file —
but the evidence only became worth its wording after the cache was cleared.
The write itself is easy to catch once you do:

    NtSetValueKey (0x8fc, L"FlightCacheAudience", 1, ..., 16)   16 bytes = "Not_C2R"

### What the succeeding process does differently

`OLicenseHeartbeat.exe`, recomputed the same way (delete its ECS key, run it),
writes 48 bytes — `"DCWin8_DevMain_Insiders"` — and its trace shows exactly what
Word's lacks:

    Software\Microsoft\Office\ClickToRun\Configuration     opened 62 times
      VersionToReport      68 reads
      InstallationPath     36
      ProductReleaseIds    12
      AudienceId            8

Word: zero, on a run that recomputes. So the divergence is not that the two
processes disagree about what the configuration says — one reads it and the
other never asks.

It also starts the Click-to-Run service, and `OfficeClickToRun.exe` **runs under
Wine**: `sc start ClickToRunSvc` reaches `STATE : 4 RUNNING`.

### Four more things eliminated

  - **The service is not the gate.** With `ClickToRunSvc` running before Word
    starts, Word still writes `Not_C2R`, still reports the same licensing
    telemetry, and still contacts no licensing host.
  - **No C2R client library is involved.** `ClickToRun\ApiClient.dll` and the
    dozen `AppV*.dll` next to it are never loaded by Word.
  - The registry config, the C2R files, and the App-V exports stay eliminated,
    now on properly-recomputing runs.

One trap worth writing down, because it briefly produced a wrong reading here:
with the service running, `OfficeClickToRun.exe` logs **into Word's own debug
output**, so a grep for `ClickToRun\Configuration` in "Word's log" returned 70
hits that were not Word's. The threads doing the reading (`00cc`, `00d0`) touch
no Word-specific key at all, which is what gives it away. Attribute by thread id
before believing a count from a shared log.

### Where this leaves the audience question

Word reaches `Not_C2R` without consulting the registry, the filesystem, the
App-V layer, the C2R service, or any C2R library — so it is decided in-process
from something already in its own address space. That is where the next
instrument has to point, and it is the kind of question the project's existing
"PDB names yes, addresses no" problem makes expensive.

Worth keeping in proportion: the ECS configuration returned for `Other/Unknown`
and for a real audience is byte-for-byte the same, so `Not_C2R` is only a
*candidate* explanation for the licensing no-op, resting on the untested premise
that Office's vNext licensing branches on Click-to-Run status. It is the best
remaining lead, not a diagnosis.

## `Not_C2R`, found and fixed: Office asks whether App-V is in its address space (2026-09-02)

The determination is in `Mso20win32client.dll`, which Word loads and which is
the only module carrying the `Not_C2R` literal that Word maps. Finding it did
not need a working PDB address, which is fortunate, because this module has the
same problem `MSO.pdb` does — five named exports, five *different* deltas
between the PDB's addresses and the binary's:

    MsoNotifyPerfMarker  pdb 0x002af6c0  export 0x0026a700  delta  +0x44fc0
    MsoFPuncWch          pdb 0x0037e3f0  export 0x003b7e20  delta  -0x39a30
    MsoFSpaceWch         pdb 0x001a0ee0  export 0x0007a5d0  delta +0x126910
    MsoWchToLowerLid     pdb 0x00106ad0  export 0x0025e440  delta -0x157970
    MsoWchToUpperLid     pdb 0x00105cb0  export 0x001e4520  delta  -0xde870

The symbols are still worth having — they name the machinery (`GetAudience` ->
`AudienceCache` -> `GetInstallMethod`, `GetC2rStreamPackageUrl`,
`GetAudienceMappingFromC2rStreamPackageUrl`) — but the address had to come from
the binary. The route that works, and is worth reusing:

1. Locate the string's RVA, then scan `.text` for a RIP-relative `lea` whose
   target is that RVA. Two hits here, 21 bytes apart — the two arms of one
   small-string assignment, not two decisions.
2. `.pdata` gives the containing function: `0x1bb120..0x1bb792`, 1650 bytes,
   with the assignment only `0x106` in — so the decision is at the top.
3. Disassemble the top:

        mov  $6,%ecx ; call 0x18005d8f0 ; test %eax,%eax ; jne <real audience>
        mov  <global>,%eax ; cmp $0x51 ; je <real> ; cmp $0x52 ; je <real>
        ...falls through to "Not_C2R"

4. That predicate caches its answer and computes it from two calls through one
   IAT slot. The slot resolves to `KERNEL32!GetModuleHandleW`, and the two
   strings it is given are **`AppVEntSubsystems32.dll`** and
   **`AppVEntSubsystems64.dll`**.

**Office decides whether it is a Click-to-Run install by asking whether the
Windows App-V client is loaded in its own process. Nothing more.** It reads a
module handle and never calls into the module. That is exactly consistent with
every earlier measurement: no registry read, no file open, no App-V export
called, and no change when the Click-to-Run service is running.

`AppVEntSubsystems*.dll` belongs to Windows, not to Office, so it exists nowhere
in the prefix. Note it is a *different* dll from the `AppvIsvSubsystems64.dll`
Office ships and Word does load — which is why `tools/appv-stub` never affected
this.

### The fix, and what it changes

`tools/appv-stub/entsubsystems.c` is a module that exists and does nothing else.
Because the check is `GetModuleHandleW`, the file merely being on disk is not
enough — it has to be mapped, so it is pulled in as a **static import** of the
`AppvIsvSubsystems64.dll` stand-in that Word already loads. A static import is
also the safe way: `LoadLibrary` from inside a `DllMain` is the deadlock-prone
one.

Measured, reproducibly:

  - `FlightCacheAudience` for `word` goes `Not_C2R` -> **`Insiders_DevMain`**,
    matching the working machine exactly.
  - `EligibleForExtendedGrace` goes 1 -> **0**.
  - Word loads **362** modules where it used to load 167 — more than twice as
    much of Office actually runs.
  - New hosts appear (`odc.officeapps.live.com`, `otelrules.svc.static.microsoft`).
  - **The dialog finally says what it is.** It had been an error icon with three
    text lines MSAA reported as empty; on the C2R path it reads
    「Microsoft Office 无法验证此产品的许可证。应使用控制面板修复 Office 程序。」
    with a Help button — the "cannot verify the license" text that patch 0005
    had removed from the pre-C2R path.
  - 197 `err:crypt:valid_protect_data unrecognized CryptProtectData block` —
    Office now walks the identity caches copied from the other machine, which
    are DPAPI blobs Wine cannot open.

Word takes noticeably longer to start, and a 70-second window is no longer
enough to reach the dialog; 200 seconds is.

### And it does not fix licensing — the hypothesis is disproven

The reason `Not_C2R` was the leading lead was the premise that vNext licensing
is Click-to-Run-only, so an install that does not think it is C2R would have
nothing to repair online. With the install now correctly reporting itself as
C2R, the licensing loop is unchanged:

    OnlineRepair records, before the fix:  dur=2 S_OK x28
    OnlineRepair records, after:           dur=2 S_OK x46   (18 new, all identical)

`FullValidation` still fails with `0xc004f015`, `OnlineRepair` still returns
success in two ticks without going online, and no licensing host is contacted.
**So the premise was wrong**, and the C2R status is not what gates the online
step. The fix is worth keeping on its own merits — it is a real Wine gap, and it
puts far more of Office on its intended path — but it is not the licence.

## Office's own sign-in page, working (2026-09-02)

The first session's conclusion was that the legitimate route is a one-time
online activation, and that it was "gated on sign-in working". Sign-in now
works: Office renders the real Microsoft sign-in page, with an email field and
a Next button, inside Word. `scripts/office-sign-in.sh` puts it on screen.

Four things had to be true at once, and none of them is the licence:

**1. The Click-to-Run marker.** Without it Office takes the reduced `Not_C2R`
path and the Account pane's sign-in control is not reachable at all.

**2. Every scope the broker is asked for.** Office discovers scopes
progressively — each one it gets a token for takes it further, and the next
request appears only then. Nine, in the order they surfaced:

    service::officeapps.live.com::MBI_SSL_SHORT openid profile
    service::ssl.live.com::MBI_SSL_SHORT openid profile
    openid service::ssl.live.com::MBI_SSL profile
    openid service::https://graph.microsoft.com/.default::DELEGATION profile
    https://consentservice.microsoft.com/checkin/UnifiedUserConsent.Read openid profile
    service::ads.arcct.msn.com::MBI_SSL openid profile
    service::messaging.engagement.office.com::MBI_SSL_SHORT openid profile
    service::outlook.office.com::MBI_SSL openid profile
    service::substrate.office.com::MBI_SSL openid profile

With all nine in the per-scope store a start-up serves 19 requests and misses
none. Note the two `ssl.live.com` entries differ only in `MBI_SSL_SHORT` versus
`MBI_SSL` and in word order — a store keyed on the exact scope string needs
both.

**3. Wine Gecko.** With the tokens in place Office got as far as wanting to
render HTML, and the next thing on screen was Wine's "could not find a Gecko
package" prompt — which is also what had been silently ending those runs early.
`wine-gecko-2.47.4-x86_64.msi` into `~/.cache/wine` and `msiexec /i` into the
prefix. Note `setup-prefix.sh` sets `mshtml=d` for its own run; that is about
not blocking a script on the prompt, and the package still has to be installed.

**4. No identity caches from the other machine.** This is the one that is
counter-intuitive. `IdentityCache` (47 files) and `OneAuth` (27) copied from the
working machine are DPAPI blobs sealed by Windows, and Wine rejects all 197
`CryptUnprotectData` calls against them. With them present Office shows
「很抱歉，当前无法访问您的帐户」 and a Sign in button that leads nowhere.
Moved aside, the DPAPI failures go to zero and Office shows the ordinary
signed-out state — 「登录到 Office」 — from which the button works.

An earlier session bridged those caches and a later one removed them as
unnecessary; both were right about their own moment. They are not needed, and
while they are unreadable they are worse than absent.

### Getting to the button at all

Two mechanics matter, both learned the hard way:

  - **Pressing OK on the licensing dialog quits Word.** Everything has to be
    driven with that dialog still up. MSAA reaches the Account pane behind it
    perfectly well.
  - `tools/uiclick` presses things by accessible name. It matches exact names
    before substrings, because "登录" as a substring first matches the sentence
    that offers it (「...请重新登录。」) and pressing a static text does nothing
    while looking like it worked. It also takes its argument from the wide
    command line — through argv the localised names arrive as `??`.

What comes up is a `MozillaWindowClass` window on
`https://odc.officeapps.live.com/odc/v2.1/hrd?app=145&...` with the Microsoft
logo, 「电子邮件、电话号码或 Skype」, 「下一步」, and the account-creation links.

**Completing it is a person's job, and not only because of the password.**
Driving the form headlessly was tried three ways and none works. MSAA's
`put_accValue` answers `E_NOTIMPL` on the field — Gecko implements the
IAccessible2 text interfaces instead, MSAA's setter being deprecated. Focusing
it with `accSelect` and sending `SendInput` unicode events leaves it empty, and
so does doing that after raising the window with `SetForegroundWindow` /
`SetActiveWindow` / `SetFocus` on the accessible object's own HWND. Pressing
「下一步」 then simply redisplays the same page, which is what an empty field
should do. So what is demonstrated here is that the page renders and is live,
not that it can be completed unattended.

`tools/uiclick` keeps both mechanisms (`-set`, `-type`) because knowing they do
not work is worth as much as the pressing that does.

### What this does not settle

The licensing dialog is still there, `FullValidation` still returns
`0xc004f015`, and no licensing host is contacted at start-up. Whether finishing
the sign-in produces a device licence is untested — that is the next thing to
find out, and it is now something a person can simply try.

## Word can be debugged, but not single-stepped (2026-09-02)

`tools/bootrace` against `WINWORD.EXE` died with `0xC0000005` and zero
breakpoints reached, which read like Office refusing to run under a debugger.
It is not that. Re-run without `-rearm` — one-shot breakpoints, no trap flag —
Word survives the full window, both with a breakpoint armed and with a symbol
list naming a module it never loads. So the debugger is usable; **`-rearm`'s
single-stepping is what kills it**, which matters because `-rearm` is the
natural flag to reach for on a function called more than once.

What that did not buy, yet: breakpoints on the licensing function found above
(`+0x123990` entry, `+0x593750` gate store, `+0x123d20` quick exit) fired
**none** in 520 seconds, against a start-up that normally takes 190 and is much
slower under a debugger. Two readings, and this measurement does not separate
them:

  - Word had not reached licensing in the window, or
  - that function is not the `OnlineRepair` activity. The `OnlineRepair` string
    is referenced twice in `Mso98win32client.dll`, from a 2017-byte function and
    a 256-byte one; the analysis above assumed the larger is the activity and
    the smaller is name registration, which is plausible and unverified.

The cheap way to separate them is a breakpoint on something known to run late in
start-up: if that fires and the licensing ones do not, the attribution is wrong
rather than the window too short.

## Why the window is blank: DirectWrite could not see a single Windows font (2026-09-03)

Office paints nothing under Wine — a white frame with a close button and no
content — and this is what is behind it. The painting is now materially better
and still not fixed; both halves are recorded here.

### Office does not draw through GDI

One measurement settles where to look. With the diagnostic in
`dlls/win32u/font.c` printing every `NtGdiExtTextOutW` draw target, a whole Word
start-up produces **one** call, into a 400x248 memory bitmap (`display=0`).
Office renders its entire UI through Direct2D and DirectWrite. GDI tracing will
therefore always look empty, which is why the blankness had no visible cause.

### The failure, and its cause

    d2d_device_context_DrawTextLayout  Failed to draw text layout, hr 0x80004005   x971
    layout_run_get_last_resort_font    Failed to create last resort font, hr 0x80004005

`layout_run_get_last_resort_font` tries the requested family, then falls back to
**Tahoma in the system collection**, and fails the run if that is missing. Asking
DirectWrite directly (`tools/dwprobe`) shows why:

    font families visible to DirectWrite: 434     (Go, GFS Artemisia, Cabin, Cantarell, ...)
    FindFamilyName(Tahoma)   -> exists 0
    FindFamilyName(Arial)    -> exists 0
    FindFamilyName(Segoe UI) -> exists 0

Every family it could see was a *host* font. The prefix's `C:\windows\Fonts`
holds 423 Windows fonts, Tahoma among them — and the registry key was the
problem:

    HKLM\Software\Microsoft\Windows NT\CurrentVersion\Fonts
      1095 entries pointing at Z:\usr\share\fonts   (the host's)
         4 bare .fon filenames                       (Wine's bitmap fonts)
         0 entries pointing at C:\windows\Fonts

Wine's DirectWrite builds its system collection **only from that key** —
`factory_create_system_fontset` -> `create_system_path_list` -> `open_fonts_key`
in `dlls/dwrite/main.c`. It never scans the font directory. GDI *does* scan it,
so fonts dropped there work for ordinary Win32 programs and the discrepancy
stays invisible until something asks DirectWrite. `import-office.sh` unfolds
`root/vfs/Fonts/private` into that directory and registers nothing.

`scripts/register-prefix-fonts.py` writes the entries. DirectWrite then sees
Tahoma, Arial, Segoe UI and 微软雅黑, and both failures go to zero:

    DrawTextLayout failures     971 -> 0
    d2d_factory_get_device      488 -> 0

### Register a curated set, not the whole directory

Registering all 422 readable files made Word **stop making progress at 56 loaded
modules** — Wine's dwrite spends over a million `localizedstrings` calls building
the collection, and start-up never completes. The script's default is therefore a
curated set (75 files: DirectWrite's own last resort, the UI and document
families, and the zh-CN ones), which restores Word to 163 modules — its normal
count. `--all` is there for when something turns out to be missing, and `--clean`
removes what it added.

### Where Office actually puts its pixels (2026-09-03)

The font fix removed every D2D and DirectWrite failure and the window still
looked empty, so the next question was where the drawing goes. Reading Wine's
source could not answer it — the app chooses the path — so the answer came from
four measurement-only TRACEs, kept in
`patches/office/0007-diagnostics-where-office-puts-its-pixels.patch`.

**Office presents through a flip-model swapchain on a child window.** With the
swapchain creation instrumented to print the HWND, its class and rect, and the
full `DXGI_SWAP_CHAIN_DESC1`:

    hwnd 0x100e6 class "NetUIHWND" rect (1,1)-(1439,809) visible 0
        1438x808 fmt 87 buffers 2 scaling 1 effect 3 alpha 0     <- Word's frame
    hwnd 0x100f6 class "NetUIHWND" rect (534,345)-(907,493) visible 1
        373x148  fmt 87 buffers 2 scaling 1 effect 3 alpha 0     <- licensing dialog

`effect 3` is `DXGI_SWAP_EFFECT_FLIP_DISCARD`, `fmt 87` is `B8G8R8A8_UNORM`.
`tools/winenum` places those HWNDs in the window tree:

    OpusApp                vis=1 (0,0)-(1440,810) "Word"
      FullpageUIHost       vis=1 (1,1)-(1439,809)
        NetUIHWND          vis=1 (1,1)-(1439,809)      <- the swapchain, visible

Neither `dcomp.dll` nor DirectComposition is involved at all — Word never loads
that module, which was worth one run to rule out.

**Wine carries it end to end.** A child window always takes the offscreen path
(`needs_offscreen_rendering` returns TRUE for anything parented below the
desktop), so its X client window is XComposite-redirected, reparented onto a 1x1
dummy, and blitted onto the top-level. Instrumented, per start-up:

    19 x X11DRV_client_surface_present  hwnd 0x100e6 -> toplevel 0x100ae
       dst (1,1)-(1439,809)  src (0,0)-(1438,808)  rgn SIMPLEREGION full  blt 1

and reading the redirected client X window's pixels directly with
`import -window` shows Office's start screen sitting in it (88% #F5F5F5, 10%
#F0F0F0). The pixels are real, the blit is asked for the right rectangle, and it
succeeds.

Two intermediate claims were checked and dropped:

  - `d3d10_texture2d_Map` hands Office real content — a mapped 16x16 came back
    with 52 non-zero premultiplied pixels — so "D2D renders nothing" was never
    true.
  - `dxgi_resource_GetSharedHandle` is a stub called 405 times per Word start-up
    and 40 per SETLANG. Changing it from `E_NOTIMPL` to the answer Windows gives
    for a non-shared resource, `DXGI_ERROR_INVALID_CALL`, changed the rendering
    by **zero pixels**. Reverted; recorded so it is not tried again.

**The window is no longer white.** Word's frame now shows the start screen:
title bar, left nav rail with the selected item, two document cards, separators
— 8 colours instead of 3, #F5F5F5 over 81% of the client area. This reproduces
from a cold start with the Wine tree at HEAD, so it is not any of the diagnostic
patches.

**And this is the part to be honest about: what changed is not known.** The
all-white captures earlier the same day are not reproducible, and none of the
candidates survives:

  - the four instrumentation patches are TRACE-only and reverting them does not
    bring the white back;
  - the prefix's deployed builtins were already in sync before any rebuild
    (`sync-prefix-dlls.sh` reported 0 refreshed on its first run of the day);
  - occlusion was tested by raising another window over Word and re-capturing:
    the obscured area comes back **black**, not white, so the early captures
    were not a screenshot artifact;
  - the only Office-visible registry change (`Common\Graphics`, set and then
    deleted) makes no difference either way.

So the improvement is real and verified, and its cause is unexplained.

### The remaining gap is text, and SETLANG is the reproducer

Everything paints except glyphs and icons. Word is a bad test case for this —
it stops on the licensing dialog — but **`SETLANG.EXE`** (Office Language
Preferences) is not licence-gated, starts in 25 seconds, and fails identically:
every panel, border, list box, scrollbar and theme colour renders, and there is
**no text in a single control**. Its own caption — 「Microsoft Office 语言首选项」
— *does* render, because that one goes through GDI.

Measured over one SETLANG start-up:

    IDWriteTextAnalyzer GetGlyphs                        260   layout works
    GetGdiCompatibleGlyphPlacements                      260
    IDWriteTextLayout::Draw                                0
    CreateGlyphRunAnalysis / CreateAlphaTexture            4   all at the very end,
                                                               all ~10x10: the caption's
                                                               ? and X
    D2D BeginDraw                                          0
    GDI+ draw calls                                        0
    NtGdiExtTextOutW                                      14   all caption

Office shapes 260 glyph runs and rasterises four of them. That is why the empty
boxes are the right size: the *layout* is correct and the rasterise-and-blit step
never runs. It is not Wine dropping glyphs — Wine is never asked for them.

Office also has no fallback: with `HKCU\Software\Wine\Direct3D` `renderer` set
to `no3d`, SETLANG dies with an unhandled exception before showing a window. Its
UI is unconditionally GPU-composited.

### The reason Office declines: it wants a shared surface it cannot have

Office's UI framework names its own failure, and it is worth knowing how to make
it say so. A whole-warning sweep of a SETLANG start-up
(`WINEDEBUG=-all,err+all,warn+all`) turns up

    warn:seh:dispatch_exception EXCEPTION_WINE_CXX_EXCEPTION exception raised   x43

and an MSVC C++ throw carries everything needed to name itself:
`ExceptionInformation[3]` is the throwing module's base and `[2]` is its
`ThrowInfo`, so the RVA is one subtraction away and every pointer inside is
another RVA, ending at a `TypeDescriptor` whose name is a plain string in the
PE. Forty of the forty-three come from `Mso40UIwin32client.dll` — NetUI — and
they are:

    .?AVDeviceError@AirSpace@@

AirSpace is Office's GPU compositor. The count matches, exactly, two other
things measured in the same run:

    fixme:dxgi:dxgi_resource_GetSharedHandle ... stub!                      x40
    warn:dxgi:dxgi_resource_inner_QueryInterface {9d8e1289-...} E_NOINTERFACE x40

`{9d8e1289-d7b3-465f-8126-250e349af85d}` is `IID_IDXGIKeyedMutex`. Office
creates its compositor surfaces with `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`
(the `misc 0x100` in the texture dump — note that `GDI_COMPATIBLE` is `0x200`,
and reading 0x100 as that is a mistake this write-up made once), asks DXGI for
the shared handle and for the keyed mutex, gets a stub and an `E_NOINTERFACE`,
and throws.

`IDXGIKeyedMutex` is now implemented
(`patches/office/0008-dxgi-implement-idxgikeyedmutex.patch`) — Wine declared it
in `dxgi.idl` and built nothing behind it. **It changes no pixels**, because
Office fails at `GetSharedHandle` first.

And that is where this stops, with the wall named precisely. Handing back a
plausible handle from `GetSharedHandle` was tried, and the result is worth
recording because it looks like progress and is not:

    AirSpace::DeviceError       40  ->     0
    d3d11 OpenSharedResource     0  ->  3794   (a stub)
    AirSpace::DeviceLostError    0  ->  4907
    D3D device creations         3  ->   139
    pixels                            unchanged

Office gets the handle, tries to open it on another device, cannot, concludes
the device was lost, tears it down and rebuilds it — a hundred and thirty-nine
times per dialog. The fake handle was reverted; failing at `GetSharedHandle` is
the cheaper failure.

The last measurement rules out the shortcut. Printing the device on both sides
shows the create and the open are on **different** `IDXGIDevice` objects, on
different threads:

    GetSharedHandle     handle 0x1001 on dxgi device 0x...65F8383E0   thread 030c
    OpenSharedResource  handle 0x1001 on dxgi device 0x...54B0718E0   thread 0384

So this cannot be closed by returning the same texture to the opener. Office
renders its text and icons on one D3D device and composites them on another,
through a shared keyed-mutex surface, and Wine has no cross-device resource
sharing: `IDXGIResource::GetSharedHandle`, `ID3D11Device::OpenSharedResource`
and `ID3D10Device::OpenSharedResource` are all stubs, and making them work means
sharing a texture between two `wined3d_device`s. That is the next piece of work,
and it is a wined3d feature, not a patch.

### Closing the shared-surface gap: Office now draws its text, and it is still not on screen

All three missing pieces are implemented in
`patches/office/0008-shared-surfaces-and-idxgikeyedmutex.patch`: a handle
registry in wined3d (the one place both dxgi and d3d11 can reach without new
plumbing), `GetSharedHandle` and `IDXGIKeyedMutex` in dxgi, and
`OpenSharedResource` in d3d11. What Office does with them:

    C++ exceptions from Office        43  ->    3
    IDWriteTextLayout::Draw            0  ->   32
    glyph runs rasterised              4  ->   78
    D3D11 DrawIndexed                  8  ->  188
    D3D11 calls in total            1448  -> 12207
    pixels on screen                        unchanged

**Office now asks for its text, rasterises it and draws it, where before it
never asked at all.** And Wine's rasteriser is not the problem: instrumenting
`glyphrunanalysis_CreateAlphaTexture` to count the alpha it hands back shows all
78 textures full of real glyph coverage —

    altars-alpha bounds (74,1)-(139,14)  size 845  nonzero 437  peak 255
    altars-alpha bounds (132,34)-(216,46) size 1008 nonzero 771 peak 255
    ...  78 of 78, none empty

The compromise in that patch has to be read carefully, because the alternative
was measured. Wine cannot share storage between two wined3d devices, so
`OpenSharedResource` hands back the *originating* texture. Returning a
correctly-shaped but private texture instead makes Office composite an empty
surface and the dialog renders visibly **worse** — 51 distinct colours instead
of 577, large black regions. Aliasing is not correct in general; it is what the
single-adapter single-process case wants, wined3d raises no error on it, and it
beats both the stub and the copy.

**Where it stops now: SETLANG creates one swapchain and presents it exactly
once.** Everything Office draws after that first frame — which is all of the
text — goes nowhere. Making the keyed mutex non-blocking was tried and changes
neither the present count nor a pixel, so the handoff is not what stalls it.

### A correction on reading offscreen surfaces

The trick in the previous section — find the redirected client X window by size
and `import`/`xwd` its pixels — **does not work for a window Wine left
attached**, and it produced a wrong reading here that was nearly reported. A
child X window with no backing store hands back whatever was last in that
region of the framebuffer, which in this case was Visual Studio Code, complete
with 3497 distinct colours of somebody else's antialiased text. Word's client
window is XComposite-redirected and reads back correctly; SETLANG's is
attached and does not. Check `xwininfo -id <toplevel> -tree`: zero children
means redirected and readable, a child at the client offset means the read is
meaningless.

### The shared storage, built — and Office's UI renders

`patches/office/0008-shared-surfaces-and-idxgikeyedmutex.patch` now carries the
whole thing, and it is what makes Office's interface appear.

The shortcut had to go first. Handing the opening device the *originating*
texture makes every call succeed, gives one keyed mutex, and looks right in
every trace — and the pixels do not cross, because a wined3d texture's GL object
belongs to the context that created it. `tools/d3dshare` is sixty lines that
settle this with no application involved: device B clears the shared surface
red, device A reads it back.

    A's texture 0x...ad70, B's texture 0x...ad70   (aliased -- same object)
    B: ClearRenderTargetView(red)                  issued
    A: Map(staging)                                ok
    pixel A reads back: 00000000                   VERDICT: DOES NOT CARRY PIXELS

So each participant gets a texture on its own device, the two share one
keyed-mutex state, and the content moves at the handoff the mutex already
defines — whoever acquires gets the pixels of whoever released last, out through
a staging texture on one device and in through a staging texture on the other.
With that, the same probe reads back `ffff0000`.

**Measured on real screen pixels** (see the correction below), before and after:

                                            before      after
    Word start screen, distinct colours          8       ~1300
    SETLANG dialog,    distinct colours        577        1263
    C++ exceptions from Office                  43           3
    IDWriteTextLayout::Draw                      0          32
    glyph runs rasterised                        4          78
    D3D11 DrawIndexed                            8         188

Word's start screen renders completely: the ribbon-less Start view with six
template cards and their names, the 最近/收藏夹/与我共享 tabs, the search box, the
sign-in link, the navigation rail. SETLANG renders every label, list entry and
button caption. This is the painting problem, closed.

### A correction that matters for anyone repeating these measurements

Nearly every screenshot earlier in this document was taken with `import -window`
or `xwd -id` on the **top-level window**, and that reads Wine's own window
surface — GDI content — not what is on the screen. The GL child window
composited over it is invisible to that read. It is why the top-level looked
like it had a start screen while a user would have seen an empty frame, and why
an offscreen client window once read back as Visual Studio Code.

Use `xwd -root` and crop to the window's geometry. Every number in the table
above is from a root capture.

### Two things this does not fix

Both checked by building without the patch, so both are pre-existing:

  - **Word's licensing dialog is a black rectangle.** It is black at baseline
    too. Word's own Start view renders around it.
  - **Word fails to open a window at all on roughly one start in three.** The
    baseline rate is the same (2 of 3 in both), and a run that hangs makes no
    keyed-mutex calls at all, so it stops before any of this code. Leftover
    `WINWORD.EXE` processes that `wineserver -k` does not reap make it worse —
    `pkill -x WINWORD.EXE` before each run, or the single-instance handoff eats
    the launch.

That is the state to hand over. The font fix is real and verified, the
presentation path is known, the shared-surface chain is implemented and Office's
UI renders through it, and what remains is the licence.

### Two traps found the hard way this session

  - **`+relay` no longer traces PE builtins.** `WINEDEBUG=+relay` with
    `RelayInclude` set to `gdi32.dll;user32.dll` produces *nothing* from those
    modules in Wine 11 — only internal `Call window proc` / `Call PE DLL` lines.
    The skill this work follows leans on relay as its fourth instrument; here it
    is dead, and the per-component channels (`+font`, `+dib`, `+d2d`, `+dwrite`)
    are the replacement.
  - **`RelayFromInclude` left set in the prefix silently filters everything.**
    An earlier session had left it at `wwlib;oart;mso40uiwin32client;...`, so a
    relay run against SETLANG returned zero lines and looked like a Wine
    limitation rather than a stale registry value. Check
    `HKCU\Software\Wine\Debug` before believing an empty relay.

### Two corrections from this session

  - **"The Office prefix is broken" was wrong.** D3D10 device creation fails in
    the Office prefix and succeeds in a fresh one — but only on display `:2`.
    On `:77` with a fresh wineserver the Office prefix creates D3D10 devices
    fine. `:2` is a long-running Xwayland that reports `current 0 x 0` and zero
    connected RandR outputs, so Wine falls back to Xinerama, finds no monitor,
    and DXGI cannot resolve an output for its device window
    (`dxgi_get_output_from_window Failed to get monitor from window`). The
    prefix was never the variable; the display was.
  - **Some of the "empty log" runs blamed on Word's single-instance handoff were
    a full disk.** `/tmp` here is an 8 GB tmpfs shared between sessions, and the
    `+d2d`/`+file` traces in this session are hundreds of megabytes each. When
    it filled, runs produced empty logs that look exactly like a failed start.
    Cap the channels, and check `df` before believing an empty log.

## The black licensing dialog, traced to the last X11 pixel -- and past this tree (2026-09-03)

The previous session closed with two things this repository's fix does not
reach: the licensing dialog's solid black rectangle, and the roughly one-in-
three start that shows no window at all. This is how far the first one goes,
measured the same way as everything above: real screen pixels, real register
and memory reads, nothing inferred.

### First, the correct control: the dialog does not use shared surfaces at all

`patches/office/0009-diagnostics-the-licensing-dialogs-pixels-are-correct.patch`
adds the TRACEs this section is built on. The first thing they show rules out
0008 as a suspect for this window specifically:

    altars-tex 373x148 fmt 87 misc 0 texture ...E750 device ...AE08
    altars-tex 373x148 fmt 87 misc 0 texture ...E7F0 device ...AE08

`misc 0` -- neither buffer carries `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`.
Both belong to one device. This window never calls `AcquireSync`,
`ReleaseSync` or `OpenSharedResource`; the shared-surface code added for the
main frame and SETLANG is simply never reached here. Whatever is wrong with
this dialog is a different problem from the one 0008 solved.

### The content is real, and it is drawn

With `+d3d11`, the dialog's one and only present shows real work, not an
empty frame:

    ClearRenderTargetView render_target_view ...FF40 color_rgba {1,1,1,1}   <- white, not black
    DrawIndexedInstanced instance_index_count 30 instance_count 1           <- one background quad
    DrawIndexedInstanced instance_index_count 30 instance_count 56          <- 56 more, almost
                                                                                certainly the glyph batch
    d3d11_swapchain_Present -> altars-present hwnd 0x100f8 count 1

Exactly one present, ever, for this window. The main frame's equivalent
window presents 37+ times in the same run.

### The pixels are correct at every layer this tree owns

Three independent reads, all added in 0009, all inside the present path:

    altars-redirect  hwnd 0x100f6 window c004ee map_state 2 w 373 h 148
    altars-srcpixel  hwnd 0x100f6 window c004ee pixel 00ffffff map_state 2 ...
    altars-dstpixel  hwnd 0x100f6 window e00019 pixel 00ffffff at (1,29)

In order: the window is redirected while already viewable, at the right size
(not a race against mapping); the GL-rendered source window holds white at the
exact instant `NtGdiStretchBlt` reads from it; and an `XGetImage` read of the
*destination* -- the dialog's own top-level X window -- immediately after the
blit and an `XFlush`, at the exact position the content was written, is also
white. This is not Wine's bookkeeping saying the write succeeded; it is the
X server being asked, separately, what is actually in that window.

`xwd -root` cropped to that exact screen rectangle, at that exact moment,
reads solid black -- one colour, all of it.

### That gap survives everything triable from outside the app

A live process was poked six different ways, each one ruling out one
explanation:

  - **`xwd -id` directly on the toplevel** (bypassing any question about which
    capture method to trust): 542 distinct colours -- white background, Word's
    two blues (`#185ABD`, `#1651AA`), anti-aliased greys. The window's own
    drawable holds the dialog, rendered, indefinitely -- this was re-checked
    after several of the pokes below and never changed on its own.
  - **`windowraise`**: no change.
  - **A resize round-trip** (grow one pixel, shrink back, forcing
    `ConfigureNotify`): no change. The window's own content survives this
    (confirmed by re-reading it), so the null result is not "the poke erased
    the content".
  - **Unmap, then remap**: this one *did* disturb the window's own stored
    content (542 colours dropped to 13 -- `Backing Store: NotUseful` means the
    X server owes it nothing across an unmap) -- but restoring the content with
    an external, Wine-independent `XCopyArea` + `XSync` still left the screen
    black.
  - **Forcing `override_redirect = True`** and remapping (a way to ask
    whether whatever manages "normal" top-levels is the blocker): no change,
    content confirmed present throughout.
  - **Reissuing the present itself, nine times total, 100ms apart, from
    inside Wine**, on a completely fresh run untouched by any of the pokes
    above (so as not to test a possibly-already-wedged window): no change.
    `altars-dstpixel` confirmed white after the ninth blit; the screen was
    still black.

### The control that says where this actually lives

A ten-line Xlib program with no Wine and no GL in it -- create a window, map
it, fill it white once, done -- shows up correctly and immediately in the same
`xwd -root` capture, at a different point on the same screen. So `:77`'s
display server is not broadly incapable of showing a window presented exactly
once; a plain software client works fine the first time, every time.

That leaves a narrower, still-open question, now aimed at the right place:
something about a window that has been associated with GL/DRI3 rendering
(this dialog's child renders through wined3d's GL backend) is not reaching the
compositor when its last mile home is a plain `XCopyArea`, on this Xwayland.
Whether that reproduces on a plain Xorg, on a different compositor, or a real
user's desktop is untested from here -- it would need access to the
compositor process itself, which is outside this repository and outside a
single session's reach.

### Where this leaves it

Ruled out, all measured: the shared-surface mechanism (not in use here), the
render target's content (white, drawn, 57 quads worth), the source drawable
(white at blit time), the destination drawable (white immediately after the
blit), window redirection timing, window stacking (nothing else occupies that
screen rectangle), override-redirect state, and "does it just need another
frame" (up to nine tried). Not ruled out, and outside what a Wine patch can
reach: whatever this specific Xwayland does differently for a GL-associated
window's single software present.

Every diagnostic added for this is TRACE-only and has no effect on behaviour,
confirmed by a full Word run afterward: 2275 distinct colours on the main
frame (matching every prior good run), no new errors in the log, patch 0008's
result fully intact. `0009` is meant to be re-applied whole the next time this
question comes back, not carried in a build being used for anything else.

## The other open item: a real Win32 dialog was blocking most of the "no window" runs (2026-09-03)

The previous session's "Word fails to open a window on about one start in
three" turned out, on this session's re-measurement, to very likely *be* this,
at least in large part: a plain `#32770` messagebox --
「Word 上次启动失败。安全模式可以帮助您解决问题...是否要在安全模式中启动？」,
Yes/No -- appearing before the main frame and sitting there indefinitely,
because nothing answers it. An automated run watching only for the main
window (as every measurement script in this project does) cannot tell that
apart from a genuine hang. One run that had timed out at 90 seconds earlier in
this project's history was, when checked directly, sitting on exactly this
dialog, unanswered.

This is not a Wine bug by itself -- it is what real Windows Word does after
an unclean exit, and Wine's implementation is a perfectly ordinary Win32
dialog: real child `Button` controls, a coordinate click works, no MSAA
needed (unlike every NetUI-drawn dialog elsewhere in Office). It showed up
in roughly 2 of 8 explicit checks this session, in the same range as the
one-in-three figure being re-examined.

### What was and was not found

`run-word.sh`'s own header already claimed the fix -- clear the per-session
`%TEMP%` directories and the `Resiliency` key -- and that claim did not
survive this session's re-testing: the dialog still appeared, more than once,
on runs immediately following that exact cleanup. Chasing the real trigger:

  - **Not the Word registry tree.** `wine reg export` on
    `HKCU\...\Office\16.0\Word` immediately after cleanup, and again after a
    forced kill and the same cleanup, diffs to nothing but `AddInLoadTimes`
    noise (a rolling latency sample every add-in load appends to). No
    `Resiliency` key exists in either snapshot -- it is not being recreated
    and cleared each time, it is just already gone.
  - **Not Wine's own crash-recovery bookkeeping.** `RegisterApplicationRestart`,
    `RegisterApplicationRecoveryCallback`, `ApplicationRecoveryInProgress` and
    `ApplicationRecoveryFinished` in `dlls/kernel32/process.c` are all stub
    `FIXME`s that fake success and keep no state at all, so there is nothing
    there to have gone stale.
  - **A real difference, not yet cleanly isolated: how the previous run ended.**
    `wine taskkill /im WINWORD.EXE` (a real `WM_CLOSE`) followed by a fresh
    launch showed no safe-mode dialog across several tries; `pkill -x
    WINWORD.EXE` (a host-level `SIGTERM` that Wine does not translate into a
    graceful in-guest shutdown) was followed by the dialog on some tries and
    not others. The pattern fits -- an app that vanishes without its own exit
    path running looks exactly like a crash to whatever mechanism *does*
    track this -- but a quick back-to-back trial loop raced its own
    `wineserver -k`/relaunch timing and produced one inconclusive batch. A
    slower, one-at-a-time series (kill one way, wait for the process to fully
    settle, launch, observe, repeat) is the next thing to run, and whatever
    file or registry value **does** track this is still unfound -- `OTele`'s
    own session bookkeeping is the next place to look, not yet checked this
    session.

### The immediate mitigation, independent of the root cause

`run-word.sh` now backgrounds a small watcher (`xdotool`, best-effort, silent
if unavailable) that answers the dialog with "No" if it appears, so a scripted
run does not sit blocked behind it looking hung regardless of what triggers
it. It clicks at a fixed offset from the window's own reported geometry
(`+203,+81` from what `xdotool getwindowgeometry` reports as the top-left --
that tool measures the client area, inside the window manager's 3px
border/29px titlebar, so it is not the same origin as Win32's own
`GetWindowRect`; the offset was derived from `tools/winenum`'s real button
rect minus that client-area origin, and cross-checked against the click that
worked manually earlier in this session). The offset math and the
background-survives-`exec` mechanism were each verified separately (a minimal
isolated reproduction, and a direct recomputation against `winenum`'s output);
catching the watcher fire against a live, naturally-occurring instance of the
dialog was not managed in this session -- the two follow-up launches after the
fix both happened not to trigger the dialog at all, which the roughly-one-in-
three rate makes unsurprising but does mean this still wants a live positive
confirmation next time it comes up.

## The licence: one concrete branch found, not yet resolved (2026-09-03)

`grep -rl OnlineRepair` across the installed tree lands in exactly one place:
`Mso98win32client.dll` (both bitnesses) -- Office's C++ licensing engine
(mangled names confirm a `Mso::Licensing` namespace). It also carries the
LVUX ("Licensing Validation UX") machinery, and three of its log strings are
the most direct lead found so far:

    LicActivator::Activate - No activation - Headless LVUX enabled
    LicActivator::WaitForResult - No activation - Headless LVUX enabled
    LIC::SubscriptionHeartbeat - No heartbeat - Headless LVUX enabled

"Headless LVUX" -- a mode with no activation and no heartbeat -- is exactly
consistent with `OnlineRepair` returning `S_OK` in two ticks while resolving
no licensing host at all.

Following the "Activate" string by RVA (scan `.text` for a RIP-relative `lea`
targeting it, `.pdata` for the containing function, per this project's own
method) lands two call sites inside one function pair, both right at the top.
Disassembling the first:

    call [rip+X]            ; -> Mso30win32client.dll ordinal 60682 (an object)
    mov  r9, rax             ; save it
    mov  rcx, [rax]          ; rcx = *object = vtable pointer
    mov  rax, [rcx+0x2b8]    ; rax = vtable slot 0x2b8/8 = #87
    mov  rcx, r9             ; restore 'this'
    call [rip+Y]             ; Control Flow Guard dispatch thunk -- jumps to
                             ; whatever is in rax, i.e. the virtual call above
    test al, al
    je   <"No activation - Headless LVUX enabled">   ; <- exactly the branch

So the gate is a virtual method -- slot #87 on whatever concrete class
`Mso30win32client.dll`'s ordinal 60682 returns -- and `false` from it is what
produces "Headless LVUX enabled" and skips activation.

**Not yet resolved:** naming that method statically needs a class name for
the vtable, and both PDBs involved (`Mso98Win32Client.pdb`,
`Mso30Win32Client.pdb`, both fetched fresh from `msdl.microsoft.com` and
matching this exact build's PDB GUID) show the same address-delta problem
this project has hit before with Office's own PDBs -- the nearest-preceding
symbol for the ordinal's RVA lands 0x648 bytes in, in unrelated code, the same
symptom that made `MSO.pdb`'s five exports each need their own delta. That
calibration (cross-validate against several already-known-good addresses)
was not done this session.

**The faster route from here, not yet tried:** a live breakpoint at
`Mso98win32client.dll`'s `test al,al` (the RVA found above) reads `al` and the
object's real vtable directly, which resolves the concrete class with no
symbol-matching needed at all -- turning "which virtual method is this" into
one measurement instead of a delta-calibration exercise. `winedbg` is not
currently built into `dist-cx` (`programs/winedbg` in the Wine tree); building
it, or writing a small vectored-exception one-shot breakpoint injector per
the skill's instrument 3, is the next concrete step.

## The live breakpoint, built and correct, and a real wall it ran into (2026-09-03)

`tools/bpread` is that breakpoint tool -- the standard Win32 debugging API
(`CreateProcess`/`DebugActiveProcess`, `WaitForDebugEvent`,
`Read`/`WriteProcessMemory`, `Get`/`SetThreadContext`), the same base
`winedbg` itself is built on, so it needs no injected code and no hardware
breakpoints (which are already known not to work under Wine). Self-tested
first, per the skill's own instrument 3c, against something fully understood:
a breakpoint on `kernel32.dll`'s `ExitProcess` while running `cmd.exe /c echo
hi` hit once, with `rcx` (the exit code parameter) correctly `0`.

### The address is confirmed right

Attached to Word once it was already stable (main window up, no crash --
below), a breakpoint at the RVA found earlier armed cleanly and the original
byte read back was `0xff` -- exactly the opcode `call [rip+X]` starts with,
independently confirming the address this session's static analysis landed
on is the real one, with no symbol involved at all.

### But the check itself was already over by then

Waited there for over half an hour, including one attempt to force a fresh
check by navigating to File > Info (Office's own "verify licence" surface):
the breakpoint never fired. This settles the "Headless LVUX" idle-task
question this document's previous section left open: **this check runs once,
at startup, and does not recur** while Word sits idle behind the licensing
dialog, and nothing tried here re-triggers it either. Whatever answered it
already happened by the time any debugger can safely be attached to Word --
which is the real obstacle, not this tool.

### Attaching this early is fatal, on its own, no breakpoint required

Launching `WINWORD.EXE` straight under `DEBUG_PROCESS` crashes it with
`STATUS_ACCESS_VIOLATION`, reproducibly, at the same address, before
`Mso98win32client.dll` even loads. Racing an attach against the ordinary,
working launch path (polling `tools/winenum`-style process enumeration and
attaching the instant `WINWORD.EXE` appears -- at that point only ~13 modules
had loaded) reproduces the identical crash. To isolate what specifically was
fatal, the same race was repeated arming **no breakpoint at all** (a
nonexistent target module, so nothing is ever patched): **it crashed exactly
the same way.** So this is not a bug in this tool's breakpoint logic -- the
mere act of a debugger attaching this early in `WINWORD.EXE`'s startup is
fatal, on its own. A 500ms delay between seeing the module load and touching
its memory made no difference either. Attaching once the main window is
already up and stable never crashes it, confirmed repeatedly -- so the
window where attaching is safe and the window where the check has not yet
run do not overlap, as far as this session found.

This is a real, reproducible Wine gap in its own right -- something about
early process/thread setup under an active debugger -- and is a different,
and likely deeper, piece of work than Office's licensing logic itself.
Fixing it is what would unblock reading this branch's true answer live rather
than by symbol archaeology.

### One more gap, worth knowing before it costs a session

`--attach` takes wineserver's own small-integer process id -- the one
`tools/winenum` prints -- not the Linux pid `pgrep`/`ps` report. Against the
Linux pid, `OpenProcess` fails with `ERROR_INVALID_PARAMETER` for *every*
access mask tried, including `PROCESS_QUERY_LIMITED_INFORMATION` alone,
which is what makes it easy to misread as a permissions problem when it is
actually the wrong id namespace entirely. Confirmed by testing every mask in
isolation, and confirmed to work immediately once the correct pid (via
`tools/winenum`) was used instead.

A second, still-unexplained one: a single long-running process that repeatedly
calls `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, ...)` in a polling loop
never saw a process (`WINWORD.EXE`) that started after the polling began, in
over four hundred tries a second apart -- while a *freshly started* process
making the identical call saw it immediately. Worked around by making each
poll a fresh short-lived process (`tools/bpread`'s `--waitfor` now does this
internally) rather than understood.

### The early-attach crash, followed to its exact faulting instruction

`bpread` was extended to log every exception the debuggee raises, not only
its own breakpoints, and to print every module's load address. Racing an
attach against three separate fresh launches, the crash is identical every
time -- same fault, same address, regardless of exactly how many modules had
loaded when it hit (13 in one run, over a hundred in another, so this is a
race that different runs happen to reach at different points in startup, not
a fixed step count):

    EXCEPTION code=0xc0000005 addr=00006ffff2b475f4 firstchance=1 tid=... read at 0000000000000000
    EXCEPTION code=0xc0000005 addr=00006ffff2b475f4 firstchance=0 tid=... read at 0000000000000000

First chance and second chance, same thread, same address -- Word's own
handlers get the standard opportunity and do not take it either.

The address falls inside `Mso30win32client.dll` (its load address is
apparently non-randomised in this environment: identical across all three
runs), at RVA `0x5775f4`. Read directly out of the file rather than trusted
through the PDB -- which shows the same address-delta problem as every other
Office PDB this project has hit, landing on an unrelated astronomical-date
conversion function that cannot be right:

    5775cc: mov rcx, [rcx]     ; rcx = *rcx (the incoming parameter) -- succeeds
    ...
    5775f4: mov rax, [rcx]     ; rax = *rcx again -- faults, rcx is NULL here

So the parameter this function receives (call it `P`) is itself a valid,
non-null pointer -- dereferencing it once at `5775cc` works -- but the value
stored *at* `P` is null. If `P` is an object and offset 0 is its vtable
pointer, as is typical, this is exactly the shape of a well-known race: a
thread reaches for an object through a pointer another thread has already
published, before that second thread's constructor has run far enough to
write the vtable. Nothing here proves that is *the* race, but the evidence is
consistent with one: a pointer that is valid but whose target is not yet
initialised, hit only under the timing a debugger's pauses introduce. Real
Windows may have the identical fragility and simply schedule around it more
consistently than Wine happens to here.

This is as far as static reading can safely take it -- confirming which race,
and fixing it, needs the early-attach crash itself resolved first (so a live
read of `P` and its would-be vtable becomes possible), which is where this
stopped.

### A second attempt: raw Linux ptrace instead of Wine's own debugging API

The early-attach crash is specifically wineserver's Windows-level debugging
support (`DebugActiveProcess` et al., built on ptrace) that sets
`PEB->BeingDebugged` and drives whatever timing triggers it. Wine's normal,
non-debugged exception handling is a plain in-process `SIGSEGV` handler and
needs no ptrace at all. So `tools/bpread/ptrace-bp.c` traces the real Linux
process directly with `ptrace(2)`, going around wineserver's debug_obj
machinery entirely -- nothing in Wine has a path to notice a bare ptrace
tracer that isn't also that machinery.

Two things had to be worked out first, both costly, both worth having
recorded:

  - **This must be the tracee's own parent.** Yama's default
    `ptrace_scope=1` refuses `PTRACE_ATTACH` between unrelated processes, and
    confirms it in a way that is easy to misread: `OpenProcess`-equivalent
    access fails for *every* mask against an unrelated pid, not just the ones
    that need real privilege. `ptrace-bp` forks and execs the target itself
    (`PTRACE_TRACEME` then `execve`) rather than attaching to something
    already running.
  - **WINWORD.EXE is heavily multi-threaded (60-70 threads).**
    `PTRACE_O_TRACECLONE` is required so every thread it ever creates arrives
    traced, not only the first one -- the target address could execute on
    any of them.

Self-tested first (a trivial C program's own function, hit three times,
correct registers each time) before trusting it on anything real, and it
found the exact same address's original byte as `0xff` a second, independent
way -- matching what the Win32-API-based tool already found, this time via a
completely different code path with zero Wine involvement at all.

**It does avoid the original crash.** A pass-through trace (a target address
that never maps, so nothing is ever patched) ran WINWORD.EXE for well past
the point the Win32-API crash always happened, with no `STATUS_ACCESS_VIOLATION`.

**It runs into a different wall instead.** Word/Wine's own threads send real
`SIGSTOP` among each other constantly -- tens of thousands of instances in a
single run, for a reason not established here. `SIGSTOP` (and the other
stop-class signals) cannot be re-delivered through `PTRACE_CONT`'s signal
argument the way an ordinary signal can; the kernel's group-stop semantics do
not survive that round-trip. Forwarding one that way reproducibly made every
thread of WINWORD.EXE exit with code 2 within seconds. Suppressing those
specifically (delivering 0 instead of passing them through) did not fix it
either: **even with that fix, and even with a target address that is never
mapped so the whole tool is pure pass-through, tracing every one of
WINWORD.EXE's threads this thoroughly still ends the same way** -- every
thread exiting with code 2, a few seconds into startup, well before any
window appears. This was confirmed deliberately, as its own isolated test,
specifically to rule out the breakpoint logic and the signal handling as the
cause: it is not either of those. What in Wine's own multi-threaded startup
reacts this way to being traced this comprehensively is unestablished.

So both routes into a live read of the licensing branch are now blocked, for
two different reasons: the standard Win32 debugging API crashes the process
outright during early startup; raw ptrace avoids that crash but the process
exits cleanly (code 2) shortly after regardless, once traced this
thoroughly. A narrower ptrace approach -- tracing only the specific thread
expected to reach the target, rather than every thread via
`PTRACE_O_TRACECLONE`, or using `PTRACE_SEIZE` (which has more correct
group-stop handling than the `PTRACE_TRACEME` this tool uses) -- is the next
thing to try, not attempted this session.

### "Headless" reconsidered: probably not about a debugger at all

Everything above assumed "Headless LVUX" names a debugger-detection branch,
because that was the obvious reading and the live-debugging tools existed to
test it. That assumption was never itself confirmed, and a cheap, completely
separate check argues against it: `tools/sessionprobe` asks the ordinary
Win32 questions an install would ask to tell an interactive desktop session
from a real headless one (`GetSystemMetrics(SM_REMOTESESSION)`, the active
console session id versus this process's own, the window station name, the
desktop handle and its visibility) -- no Office, no debugger, nothing this
session's own tools touch. Every answer says ordinary and interactive:
session id matches the active console session, `SM_REMOTESESSION` is 0, the
window station is `WinSta0`, the desktop is `Default` and visible. If Office's
check is asking any of these ordinary questions, Wine is answering them the
unremarkable way, and "Headless" must mean something else -- an internal
Office mode flag, a readiness gate on some *other* subsystem (the object
comes from an interface with at least 88 methods; slot #87 is one query
among many), or something this project has not found yet. Worth remembering
before spending more time on the live-debugging route above: confirm what
the branch is actually asking before assuming the answer is "a debugger is
attached."

### The real reason raw ptrace fails: wineserver needs that slot too

The "code 2 mass exit" tracing every thread caused was chased as a signal-
semantics problem for a full round -- `PTRACE_TRACEME`, then suppressing
stop-class signals, then a full rewrite onto `PTRACE_SEIZE` with textbook
`PTRACE_LISTEN` handling per `ptrace(2)` (fetched and read directly to get
this right rather than guess again). **All three still ended in the
identical code-2 exit.** That result across three progressively more
correct implementations of the same idea is itself the finding: it was never
this tool's own ptrace usage that was wrong.

`wine-src/server/process.h` selects `USE_PTRACE` on Linux (the `#elif`
chain's `#else` -- Mach on Darwin, procfs on Solaris, ptrace everywhere
else), and `wine-src/server/ptrace.c`'s `suspend_for_ptrace()` is exactly
what the name says: wineserver attaches to a thread **on demand**, whenever
it needs to (`GetThreadContext`/`SetThreadContext`, debug registers, and
several other internal operations -- eight call sites in that one file),
detaches when done, every time, for the life of the process. The function's
own comment states the failure mode outright: *"this may fail if the client
is already being debugged"* -- and on that failure it is `set_error(
STATUS_ACCESS_DENIED )` and the caller gets nothing.

Only one tracer can hold a given thread at a time. An external ptrace
tracer permanently occupies that slot for as long as it is attached, so
*every* one of those eight operations -- not something specific to
breakpoints, signals, or this tool -- starts failing with
`STATUS_ACCESS_DENIED` the moment an external tracer attaches, for as long
as it stays attached. Sixty to seventy threads, several of these operations
apiece during ordinary startup, failing outright instead of the unconditional
success they get in every untraced run: that is a full, sufficient
explanation for all of them exiting within seconds, with no need for any
theory about SIGSTOP or group-stops at all. Those still real (Word's own
threads do use real SIGSTOP among themselves constantly, and the classic
API's mishandling of it is exactly as documented), but they are not what
this specific failure comes from.

This reframes the earlier debugging-API-vs-ptrace comparison: they were
never two attempts at the same workaround. `DebugActiveProcess` **makes
wineserver itself the tracer**, so there is no second tracer competing for
the slot -- consistent with it running much further before failing
differently (the `STATUS_ACCESS_VIOLATION` documented above). A bare
external ptrace tracer is structurally incompatible with a running
wineserver for as long as it holds the process, independent of how
correctly its own signal and group-stop handling is implemented -- three
implementations, one always getting more correct than the last, is why that
now reads as settled rather than merely unlucky.

The one route that follows from this and was not tried: patch
`suspend_for_ptrace()` itself to tolerate `PTRACE_ATTACH` failing with
`EPERM` (another tracer already present) without treating it as fatal to
the caller -- narrower and more targeted than either debugging API this
session used, since it touches exactly the one function actually in
conflict rather than working around the conflict from outside Wine
entirely.

## Sign-in's third blocker, found: Gecko's own input path, not the OS's (2026-09-03)

The 09-02 session tried three ways to drive the rendered sign-in form and
closed with: "Completing it is a person's job, and not only because of the
password." All three went through Win32's own input surface -- MSAA's
`put_accValue` (`E_NOTIMPL`), `accSelect` + `SendInput(KEYEVENTF_UNICODE)`, and
the same after forcing focus with `SetForegroundWindow`/`SetActiveWindow`/
`SetFocus`. All three leave the field empty.

There is a fourth surface that was not tried, because it is not a Win32 one:
Gecko is a real X11 client under its `MozillaWindowClass` window, and input
reaching it through the X server rather than through Wine's own synthesis is a
different path end to end. `SendInput(..., KEYEVENTF_UNICODE)` is not "press
this key" -- Windows has no key for an arbitrary Unicode codepoint, so Wine has
to fake one, typically by remapping a spare keycode through the X keyboard map
and faking a press of that. `xdotool`'s `type`/`key`, for the plain ASCII an
email address is made of, needs none of that: the characters already sit on
the running layout, so it sends the same keycodes a real keyboard would,
through `XTestFakeKeyEvent`, with no remapping step for Wine to get wrong.

Measured, on the same rendered page `scripts/office-sign-in.sh` already
reaches:

    xdotool mousemove --sync <x> <y> click 1      # <x,y> from uidump's new accLocation
    xdotool type --delay 80 "test-probe-only@example.com"

and `tools/uidump` immediately after:

    name="电子邮件、电话号码或 Skype" value="test-probe-only@example.com" [text] ...

The field holds it. This is the first of the four methods that does. Cleared
immediately after (`ctrl+a`, `Delete`) and never submitted -- `下一步` was not
pressed, so nothing reached Microsoft with this text.

### What had to be added to get the coordinates at all

`accLocation` was not in `tools/uidump` before this -- it printed names,
values and roles, never where a control actually is on screen, because every
earlier use of it was about reading text, not aiming a click. One four-line
addition (`rect=(x,y,w,h)`, screen coordinates, exactly what
`IAccessible::accLocation` returns and exactly what `xdotool mousemove` needs)
turned it into the missing half of a click.

### One caution this run also re-confirmed, the expensive way

`office-sign-in.sh` had no watcher for the safe-mode dialog `run-word.sh`
already knows to dismiss. This run hit exactly that: both `uiclick` presses
("帐户", "登录") reported no match, because the safe-mode dialog -- not the
expected Word window -- was what actually had focus. `uiclick "No" Button`
found nothing either, on a plain Win32 dialog whose lightweight `oleacc` proxy
did not expose its buttons by name the way it does on a real Windows install;
a coordinate click from `accLocation` worked where the name search did not.
Dismissing it and re-running the two presses reached the sign-in page
normally. `office-sign-in.sh` now carries the same watcher `run-word.sh` does,
so this should not cost the next run the same way.

A second thing worth separating from the first: once genuinely at the
Backstage view, a raw `xdotool click` at the "帐户" nav item's own
`accLocation` rect did **not** switch panes, while `uiclick`'s
`accDoDefaultAction` on the same control did, immediately. So the fourth-way
finding is specific to Gecko's own input handling, not a general "xdotool
beats MSAA" result -- Office's own NUI controls still need addressing the way
`uiclick` already does it, and Gecko's embedded content needs the opposite.
Two different surfaces, two different correct tools, and neither is a good
guess at the other's answer.

### Where this leaves it

The technical half of "a person's job" is done: the form can be filled without
a person's hands on a keyboard. The other half -- whose credential, and
pressing 下一步 against a live Microsoft sign-in endpoint with it -- is a real,
external, hard-to-reverse action on the account's actual sign-in history, and
is where this stops without asking first.

## The black rectangle, seen a second place, and the display it was measured on (2026-09-03)

Looking at the Account pane directly (`xwd -root`, cropped to Word's window,
while the sign-in flow above was paused) shows the same symptom the licensing
dialog has, on ordinary content that has nothing to do with licensing: the
"帐户隐私" / "Office 主题" / "登录到 Office" SLABs and the "Microsoft 365
预览体验计划" detail billboard are both solid black rectangles, while
"产品信息", "获取加载项" and the rest of the same pane render completely
legibly, in the same screen, at the same moment. So the black-rectangle
symptom is not specific to the licensing dialog -- it is at least these two
more NUI panels within Word's own main window, and the mechanism is almost
certainly the same one already traced ("The black licensing dialog, traced to
the last X11 pixel"): whichever panels present through a single, unshared
GL-backed surface rather than through 0008's shared-surface path hit it, and
which panels that is looks like it varies panel to panel, not window to
window.

### What this display actually is

That earlier section left one thing explicitly open: "whether that reproduces
on a plain Xorg, on a different compositor, or a real user's desktop is
untested from here." It has an answer now, and it changes how the finding
should be read:

    DISPLAY=:77   Xvfb :77 -screen 0 1920x1080x24 -nolisten tcp
    DISPLAY=:0    Xwayland :0 -rootless ... (this account's own real desktop,
                  under Mutter)

`:77` is **Xvfb** -- a headless, software-only virtual framebuffer with no
compositing manager and no real GPU scanout -- not the Xwayland the previous
session's wording speculated about. Every measurement in this whole document,
including the ones that ruled out the shared-surface mechanism and confirmed
correct pixels at every layer this tree owns, was taken on that virtual
display. The account's actual desktop session (`:0`) is a different, real
compositor entirely, running throughout, untouched by any of this.

### Two native, Wine-free controls, neither of which reproduces it

To test the display rather than the app, two small GLX programs (no Wine, no
X11 calls Wine itself doesn't already make) were written and run against
`:77`:

  - **A plain top-level GLX window**, mapped, cleared to magenta once,
    swapped once: reads back correctly, all 30000 pixels, on `:77`.
  - **A closer replica of Wine's own mechanism** -- a GL-rendered child window
    reparented under a 1x1 dummy parent (matching this project's own
    "XComposite redirect -> reparent to 1x1 dummy" description of
    `X11DRV_client_surface_present`), `XCompositeRedirectWindow`'d, cleared
    and swapped once, its content read back as a pixmap and `XCopyArea`'d into
    a separate, ordinary, fully visible destination window -- "last mile home
    is a plain XCopyArea", the previous section's own phrase for it: **also**
    reads back correctly on `:77`, all 30000 pixels.

Neither control reproduces the black rectangle. That does not overturn the
earlier finding -- real Office windows on this same display still go black,
measured six separate ways -- it says the reproduction needs some more
specific ingredient than "GL content, redirected once, copied once" alone:
something in wined3d's particular context setup, its multi-device/DXGI present
path, or a timing detail neither control happened to share with it. Worth
having anyway: it rules out "Xvfb cannot composite a redirected GL window at
all" as the explanation, which was the simplest version of the theory.

### The test that would actually settle it, not yet run

Rerunning the exact same Office flow with `DISPLAY=:0` -- the account's own
real, currently-running desktop compositor -- rather than a hand-rolled
stand-in, is the direct answer to the question the previous session left open.
It was not attempted this session: the only running `WINWORD.EXE` was left
mid-flow at the user's own request (see the sign-in section above), and Word's
single-instance handoff is believed to be scoped to the wineserver instance
behind a prefix rather than to a display, so a second launch against the same
`WINEPREFIX` risks disturbing that session rather than opening a clean second
one on `:0`. The clean way to run it: a throwaway copy of the prefix (or at
least of `dist-cx` plus a fresh `WINEPREFIX`), `DISPLAY=:0 dist-cx/bin/wine
... WINWORD.EXE`, and the same `xwd -root` read this whole document already
uses -- on the account's real desktop this time, not the virtual one every
prior measurement here was taken on.

## The real-desktop test, run: the dialog is black there too; the Start screen is not (2026-09-03)

The previous section's open test was run, with the user's explicit permission
(popping a window on their own live desktop needs that, and the auto-mode
classifier enforced it directly when this was first attempted without asking).
A throwaway copy of the whole prefix, `DISPLAY=:0` against the account's own
real Mutter/Xwayland session rather than the `:77` Xvfb every prior
measurement here used.

**First, a capture-method correction that would have produced a wrong answer
on its own.** `xwd -root` / `import -window root` fail outright on `:0`
(`BadMatch` on `X_GetImage`) -- this Xwayland is rootless, so there is no
single unified root framebuffer to read the way Xvfb has one; each top-level
window is its own compositor-managed surface. That makes `xwd -id` / `import
-window` on the specific *top-level* window the right tool here, not the
fallback this document warned against elsewhere -- that warning was about
reading an offscreen *child* window's stale backing store (the SETLANG trap),
which is a different mistake from reading a real top-level's own drawable on a
display where the top-level's drawable **is** what the compositor presents.

**A histogram alone still lies.** The licensing dialog's capture came back
with 91 distinct colours and Word's own exact blue (`#185ABD`) in it, which
briefly read as "renders fine." Looking at the actual image says otherwise:
the title bar and the `确定` button are real, correctly-coloured content: the
91 colours are almost entirely *there*. The body -- the error icon and the
"Microsoft Office 无法验证此产品的许可证" text `uidump` confirms is present --
is the same solid black rectangle as every measurement on `:77`. Counting
colours is not looking at the picture.

**The main window is a different story.** The Start screen -- nav rail,
title bar, 新建/最近 tabs, both template cards' names, the search box -- reads
back essentially completely on `:0`, matching what 0008 already produced on
`:77`. One small anomaly, a hollow black-bordered white box in the empty
lower content area, is a different shape entirely from every fill-black
rectangle this document has been chasing (border only, not a solid fill) and
was not investigated further this session -- worth keeping separate rather
than folding into the same bug just because both are "something's wrong with
a rectangle."

### What this changes

Not "Xvfb-specific, so a real user would never see it" -- that hope is
answered, and the answer is no. What *does* survive: the earlier finding that
this dialog's swapchain carries `misc 0`, no
`D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`, so it never enters 0008's
shared-surface path at all. Put the two together and the shape of the problem
sharpens rather than closes: the shared-surface path renders correctly on
**both** displays; whatever plain, unshared single-present path the dialog
(and evidently at least some Account-pane SLABs, per the `:77` finding two
sections up) uses instead is broken on **both** displays too. That is a much
narrower and more promising target than "something this Xvfb's compositor
does" -- it says the fix that already worked once might simply need to reach
further, into windows that do not yet ask for a shared surface, rather than a
second investigation into an unrelated presentation mechanism.

Not attempted this session: identifying what actually decides whether a given
NUI window's swapchain requests the shared-surface flag or not, and whether
that decision is reachable from this tree the way `Not_C2R`'s was.

## The black rectangle's actual shape: a race, not a mystery (2026-09-03)

Re-running the same dialog on `:0` with 0009's diagnostics enabled (rather
than just screenshotting it) shows something the single `:77` measurement
never could: the dialog presents **repeatedly** here (four captured in one
short window, not the "exactly one, ever" `:77` recorded), and the pixel this
document has been calling black or white **alternates, present to present**:

    src/dst pixel 00000000   (before dxgi logs "count 1")
    dxgi:  altars-present ... count 1
    src/dst pixel 00ffffff   (immediately after)
    dxgi:  altars-present ... count 2
    src/dst pixel 00000000
    dxgi:  altars-present ... count 3
    src/dst pixel 00ffffff
    dxgi:  altars-present ... count 4
    src/dst pixel 00000000

Every one of the four counted presents is preceded by a genuine, fresh
`ClearRenderTargetView`/two `DrawIndexedInstanced` calls -- Office really is
redrawing every time, not skipping frames. The x11drv-side trace lines
(`X11DRV_client_surface_present`, thread `01a0`) and the dxgi-side present
count (`d3d11_swapchain_present`, thread `01a8`) are printed from **two
different threads**. That, plus content that is sometimes there and sometimes
not despite a fresh draw immediately before it, is the signature of a race,
not a broken pixel: the X11-side read (`XGetImage` on the offscreen source,
immediately followed by the `NtGdiStretchBlt`) can run before the GL driver
has actually finished the draw it was just told to do, and whichever wins is
what ends up on screen.

### What is actually supposed to prevent this, and why it doesn't fire here

`wined3d/swapchain.c`'s `swapchain_gl_present` has exactly the guard this
needs:

    if (swapchain->device->context_count > 1)
    {
        WARN_(d3d_perf)("Multiple contexts, calling glFinish() to enforce ordering.\n");
        gl_info->gl_ops.gl.p_glFinish();
    }

-- gated on `device->context_count > 1`. AirSpace's cross-device compositing
(what 0008 fixed) inherently drives a device through more than one context, so
this fires for the main window's path essentially as a side effect, and that
side effect is very likely *why* 0008's path renders reliably on both
displays: the `glFinish()` it incidentally triggers is exactly the barrier the
offscreen-redirect StretchBlt needs before it reads the source drawable. This
dialog's swapchain carries `misc 0` -- no cross-device sharing at all -- which
is consistent with a single-context device, where this guard's condition is
false and no `glFinish()` ever runs before winex11.drv reads the same drawable
from a different thread.

**This part is inferred from reading the code alongside the trace, not
measured directly** -- `device->context_count`'s actual value at the moment of
this dialog's present was not itself printed this session, so "it is exactly
1 here" is the one link in this chain still resting on architecture rather
than a register read. Printing it is a one-line addition and the obvious next
measurement, not yet made.

### Why this reads as a real, in-scope bug rather than a compositor mystery

Put together with the previous section: the shared-surface path (0008) is
reliable on Xvfb *and* the real desktop; the plain single-present path this
dialog (and, per the Account-pane finding, apparently some other SLABs) uses
is unreliable on **both**. A bug that reproduces identically on two
unrelated display-server implementations is not a property of either display
server -- it is a missing synchronization primitive in this tree's own
offscreen-present path, incidentally papered over wherever AirSpace's own
architecture happens to force a `glFinish()` anyway.

### The fix this points at, not yet attempted

Broadening `swapchain_gl_present`'s `glFinish()` to also cover a swapchain
presenting through the offscreen-redirect mechanism (`needs_offscreen_rendering`
true for its window), not only `context_count > 1`, would put the same
barrier under the path that currently has none. This was not attempted this
session -- it is a change to a function every present in this Wine tree goes
through, and this project's own standing is to measure before writing a fix
this central, not patch on a plausible-but-unconfirmed theory. The
`device->context_count` trace above is the missing measurement that would
turn "very likely" into "confirmed" before touching it.

## The missing measurement, taken: context_count is 1, confirmed (2026-09-03)

The previous section left one link in the chain resting on architecture
rather than a register read. One line, added the same way as every other
diagnostic in this document (`wined3d/swapchain.c`, gated by nothing more than
the existing `TRACE`):

    TRACE("altars-context-count hwnd %p context_count %u.\n",
            swapchain->state.device_window, swapchain->device->context_count);

placed right before the `if (swapchain->device->context_count > 1)` guard.
Rebuilt (`make` in `build64-cx`, 6 seconds -- only `swapchain.c` and its
dependents needed it), installed, synced into the live prefix, run once
against the same dialog:

    01fc:trace:d3d:swapchain_gl_present altars-context-count hwnd 00000000000600EA context_count 1.  (x38)
    01fc:trace:d3d:swapchain_gl_present altars-context-count hwnd 00000000000100F6 context_count 1.  (x1, the dialog)

**Confirmed: this dialog's device has exactly one context**, so
`context_count > 1` is false and the `glFinish()` two lines below it never
runs. What was "very likely" in the previous section is now measured.

One thing this also surfaces, worth being honest about rather than folding
into a tidier story: hwnd `0x600EA` presents 38 times in the same run, also
at `context_count 1` -- so a single-context device is not unique to this
dialog, and this measurement alone does not show whether `0x600EA`'s content
is equally affected or is a window where the race never happens to lose. That
window was not identified or checked visually this session. The theory this
document has been building -- 0008's path is reliable because *some* device in
AirSpace's cross-device chain incidentally has `context_count > 1`, not
because every window sharing that chain individually does -- still fits what
was measured, but "every single-context window in this run is equally at
risk" is not itself a claim this session checked.

The fix candidate from the previous section stands, now on a fully measured
premise rather than a partly-inferred one. Still not attempted: the right
place to signal "wait for pending GL rendering" crosses a layer boundary
wined3d's generic swapchain code was never meant to know about (which window
is being read back through an X11-specific offscreen redirect is
winex11.drv's business, not wined3d's), which is exactly the kind of design
question this project's own standing rule says to slow down for rather than
patch past.

## The actual fix, applied and confirmed: the dialog renders (2026-09-03)

The trail above (`context_count`) turned out to be measuring a real fact about
the wrong function. `swapchain_gl_present` in `wined3d/swapchain.c` is not
what runs this dialog's present at all -- `wglSwapBuffers` from `wined3d`
reaches winex11.drv's own `x11drv_surface_swap` (`dlls/winex11.drv/opengl.c`),
and that function has its own, separate synchronization logic:

    if (!(offscreen = ...) || !ctx || !pglXSwapBuffersMscOML)
        pglXSwapBuffers( gdi_display, gl->drawable );
    else
    {
        funcs->p_glFlush();
        target_sbc = pglXSwapBuffersMscOML( ... );
        if (pglXWaitForSbcOML) pglXWaitForSbcOML( ... );   /* waits for the swap */
    }
    if (offscreen && !pglXWaitForSbcOML) XFlush( gdi_display );  /* does not */

When `GLX_OML_sync_control` (`glXSwapBuffersMscOML`/`glXWaitForSbcOML`) is not
available from the GL driver -- true here, on both `:77` and `:0` -- the
offscreen path falls all the way through to a bare `XFlush`. `XFlush` flushes
the X11 *protocol* queue; it does not wait for the GPU/renderer to finish the
draw commands the swap just submitted. `client_surface_present` -- the
XGetImage-then-StretchBlt read this whole investigation has been tracing --
runs immediately after, from a different thread, with no guarantee the
rendering it is about to read has actually landed.

The fix, in the same function, same spot:

    if (offscreen && !pglXWaitForSbcOML)
    {
        funcs->p_glFinish();
        XFlush( gdi_display );
    }

`glFinish()` is what the sibling callback (`x11drv_surface_flush`, and its EGL
counterpart) already does for the same reason in a different code path -- this
puts the one function that was missing it in line with the two that were not.

**Measured, before touching anything else:** rebuilt (`make`, under a second --
only `opengl.c` needed recompiling), installed, and run against the same
dialog on `:77`. Read back with `xwd -id` on the toplevel -- the same method
that read this exact dialog as solid black every single time before, on this
exact display:

    60546 px  #FFFFFF  (white)
     3121 px  #1756B5  (Word's blue)
    ~2500 px  various greys (anti-aliasing on the icon and text)

Viewing the actual image (not just counting colours, after that trap from the
`:0` measurement two sections up) shows a complete, correctly rendered dialog:
title, the red error icon, both lines of "Microsoft Office 无法验证此产品的许可
证。应使用控制面板修复 Office 程序。", and both buttons legible. This is the
first time in this whole document that this specific read has come back with
real content rather than one uniform colour.

Re-confirming the same result on the real desktop (`:0`) was attempted and not
completed this session -- the safe-mode dialog that gates getting there landed
at an unusual off-window position (`X=-190, Y=-100`) this run, and neither a
coordinate click nor `Alt+N` nor `Escape` reached it (keyboard focus does not
appear to have actually moved to it, a window-manager-focus question unrelated
to this fix). Not chased further given the `:77` confirmation already directly
measures the mechanism the fix addresses.

### What this changes

Not attempted: whether the same fix also settles hwnd `0x600EA` (the other
`context_count == 1` window from two sections up) or the Account-pane SLABs
found on `:0` earlier -- both use the same offscreen-redirect mechanism this
touches, so the same fix plausibly reaches them too, but neither was
re-measured this session.

The licence itself is unaffected by any of this -- `FullValidation` still
fails `0xc004f015`, still offline, still gated on the HardwareId this
document has already covered at length. What changes is that Word's own
window, including its licensing dialog, now has a real rendering fix behind
it rather than an open question about whether the display server can be
blamed for something this tree could not reach.

## Past the dialog: a document can be created, typed into, and crashed (2026-09-04)

With the rendering fix in place, the natural next question is whether Word
actually *works* -- creates and edits a document -- while the licensing
dialog sits there. It was never tested. It partly does, and it surfaces two
things the earlier sections did not.

**Creating a document works.** Pressing "新建空白文档" with the licensing
dialog still up opens a real document window (`OpusApp`/`_WwF`/`_WwB`/`_WwG`),
full ribbon (`文件/开始/插入/设计/布局/引用/邮件/审阅/视图`, plus this
install's own `MathType`/`AxMath`/`Zotero` add-ins), a normal page.

**Plain ASCII typing works. Non-ASCII does not.** `xdotool type` of
"hello test" lands in the document and the process survives. The same
approach with a Chinese string in it (`"wine-altars test: hello world 你好"`)
crashed Word outright: `wine: Unhandled exception 0xe0000002 in thread ...`,
process gone, no window left. Only one run of each was tried -- not yet
isolated to "any non-ASCII character", "specifically CJK", or "specifically
this IME path" -- but the contrast between the two runs, otherwise identical,
is real. `0xe0000002` is the same code the WAM-broker section saw from an
unrelated WinRT interface gap; whether that is a coincidence (a generic
"unhandled C++/WinRT exception" code reused for very different causes) or a
real connection was not investigated.

**A new black rectangle, not the one already fixed.** A stable
390x194-pixel solid black block sits at a fixed position in the document
body (screen rect roughly (525,308)-(915,502) at this window size and zoom),
present immediately on opening a blank document, unmoved by clicking
elsewhere or by the typed text landing well outside it. No accessible object
at that rectangle's location in a full `uidump` -- whatever draws it does
not register as an MSAA node at all. Given the add-ins visible in the
ribbon, an add-in-provided pane rendering through its own single-present
surface is a plausible guess, not a checked one; patch 0010 covers exactly
one function (`x11drv_surface_swap`) and there is no measurement yet showing
whether this rectangle's present goes through that function, a sibling one,
or something else entirely.

**Ctrl+S did not visibly do anything.** No "另存为" (Save As) UI appeared,
and no error was read back either. Whether the licence state blocks saving,
whether the key combination did not reach the window, or whether it saved
silently to a default location was not determined -- this needs a
follow-up check (a `+file` trace over the same steps would settle whether a
save was even attempted) before it belongs on either list.

### Where this leaves "does Word work"

Not a clean yes. The document surface itself now paints and accepts
ordinary typing where it previously would have been read as evidence of
nothing (a black frame, unreadable). But two new, concrete gaps sit
alongside the already-known ones (the licence, the deferred sign-in): a
crash on non-ASCII input, and an unexplained black rectangle in the
document body that patch 0010 does not obviously reach. Both are new
findings from this session, neither is fixed, and neither was chased
further given the time already spent finding them.

### The crash, narrowed a little further

Two more runs, `+seh` and `+loaddll` on: a short "你好" alone typed into a
fresh document did **not** crash; the original longer mixed string
(`"test: hello world 你好again"`) reproduced the crash both times it was
tried. So this is not "any CJK character" -- something about the longer or
mixed sequence specifically.

The final, truly-unhandled throw is distinctive on its own terms:
`EXCEPTION_WINE_CXX_EXCEPTION` (`0xe06d7363`) with `info[2]=0` and
`info[3]=0` -- no `ThrowInfo` pointer at all, so `scripts/pe-throwtype.py`
(which needs exactly that pointer) cannot name a type here. That shape --
`info[0]` carrying MSVC's EH magic number, everything else zero -- is the
signature of a bare `throw;` re-raised with no exception actually being
handled (or an equivalent CRT-level `terminate()` path), not an ordinary
"threw a known type that nothing caught". Dozens of ordinary, presumably-
caught `0xe06d7363` throws happen earlier in the same run with real
`ThrowInfo` pointers -- Office's own code raises and catches C++ exceptions
constantly as normal control flow, and is not what this is.

Not pursued further this session: a symbolized backtrace would need the
same PDB-address-delta calibration this document has repeatedly found
expensive for Office's own modules, and was not attempted here.

### One more check on the document-body rectangle, inconclusive

With the same diagnostics on, the document canvas window (`_WwG`, `dst_rect`
starting at `(1,181)`, matching its own `1421x606` redirect) does show
multiple different pixel values across presents (`00000000`, `00f0f0f0`,
`00ffffff`) -- but the trace only samples one pixel, at the redirected
source's own `(0,0)`, and this window's local origin is not necessarily the
same screen location as the visible black rectangle two sections up. Whether
these are the same bug was not established either way from this -- it would
need the trace's sample point moved to the rectangle's actual coordinates, or
a fresh screen read taken at the same instant as a log line, neither of which
was done. Left as a genuinely open question rather than folded into a
conclusion this measurement does not support.

## Both new bugs, chased one step further (2026-09-04)

Continued at the user's request. Neither is closed; both are now
substantially better characterized.

### The black rectangle: real UI, not an artifact -- still unidentified

`xwininfo -root -tree` while the rectangle is showing finds four
`MSO_BORDEREFFECT_WINDOW_CLASS`-shaped windows framing it exactly --
`391x8+525+308`, `8x194+525+308`, `8x194+908+308`, `391x8+525+494` -- the same
border-drawing pattern this document has seen frame the main window and the
licensing dialog elsewhere. So this is a real, intentional floating UI
element Office positioned there, not a stray artifact of the capture method.

What is missing is the content window these borders should be framing. It is
not among the five windows this session's own `client_surface`/offscreen
tracing already names (the scrollbar, the ribbon, the document canvas, the
status bar, the licensing dialog -- none is this size or position), and a
full `uidump` finds no accessible object anywhere inside the framed
rectangle either. Whatever draws here does not go through the
`X11DRV_client_surface_present` path patch 0010 touches, is not exposed to
MSAA, and was not otherwise located this session. Given the ribbon carries
`MathType`/`AxMath`/`Zotero`, an add-in's own pane is a plausible guess and
nothing more -- not checked by disabling those add-ins or any other way.

### The crash: not a clean terminate -- fourteen repeats of the same unwind

The full log around the crash (cut short in an earlier read) shows the
process does not go straight from the unhandled exception to exiting. After
`starting debugger...`, the same sequence repeats fourteen times back to
back, byte-identical each time down to the `target_ip`:

    RtlUnwindEx code=80000026 (STATUS_UNWIND_CONSOLIDATE) end_frame=... target_ip=00006FFFE7D177BE
    calling handler 00006FFFE7E2944C ...
    handler ... returned 1
    RtlRestoreContext returning to 00006FFFE7D177BE ...

-- then the log simply ends; no clean-exit message, no crash report. This
was not an artifact of the process being killed from outside: it was
already gone (confirmed by `pgrep`) well before any cleanup command in this
session touched it. Fourteen is a measured count for the one capture taken,
not a claim about whether this always repeats exactly that many times or
would repeat indefinitely under different conditions -- that distinction
was not tested (this run was not left to find out on its own, and no second
capture with `+seh` on was taken to compare the count).

This changes what the earlier "bare rethrow, no ThrowInfo" reading is worth
-- that was true of the *first* unhandled exception, but what actually
happens afterward is Wine's own `ntdll` unwind machinery repeatedly failing
to make progress on the same `STATUS_UNWIND_CONSOLIDATE` target, which reads
more like a gap in Wine's exception/unwind implementation for this specific
pattern than a straightforward Office logic bug -- consistent with, but not
proof of, the same kind of thing this whole document's method keeps finding:
Wine's own machinery, not the closed-source client, being where a "why does
this fail" question actually bottoms out. Not pursued further: reading
`ntdll`'s own `RtlUnwindEx`/consolidate-callback implementation to see what
about this specific case it cannot resolve is real work, on code far more
central and higher-risk to touch carelessly than anything patched so far in
this document, and was not attempted this session.

### The black rectangle: not an add-in

`WINWORD.EXE /a` -- Word's own switch that disables every COM add-in and
template -- confirmed working (the ribbon loses the `MathType`/`AxMath`/
`Zotero` tabs entirely) and the same black rectangle, same size, same
position, was there anyway on a freshly created blank document. That rules
out the add-in guess from the previous section cleanly: whatever this is,
it is Word's own, not something MathType/AxMath/Zotero contributes. What it
actually is remains unidentified -- a built-in placeholder/tip/canvas
element is now the more likely category, nothing more specific was
determined.

### The black rectangle: not a distinct window at all

A full `+win` trace (every `NtUserCreateWindowEx` and every `SetWindowPos`/
`MoveWindow` call across a whole start-up, 15000+ lines) has no window
created or moved to this rectangle's position or size, at any point. Every
other element this document has traced this way -- the dialog, the ribbon,
the scrollbar -- shows up in exactly this kind of trace. This one does not.

That points away from "a window whose present races or never happens" (this
document's running theory for every other black rectangle so far,
including patch 0010's fix) and toward something painted directly into an
existing surface -- most likely the document canvas's own Direct2D render
target -- that never gets its content, rather than a window that never gets
its window. Which specific draw call and why is not established; the
instruments this session has (client_surface/present tracing, window
creation tracing, MSAA) each rule out one category and none names the
actual one.

### The black rectangle: not a draw call either

`+d2d` over the same run (every `Draw`/`Fill` call across the whole
start-up and document creation, cross-checked programmatically against the
rectangle's coordinates in every reasonable coordinate-origin interpretation)
finds zero `FillRectangle`, `DrawBitmap`, `DrawImage`, `FillGeometry` or
`DrawGeometry` call landing anywhere in the document-body region this
rectangle occupies. Every one of the 20 `DrawBitmap` calls in the whole
capture targets the same small ribbon-icon rectangle near the top of the
window; nothing targets the document body at all.

Combined with the previous two results (no window created there, not tied
to any add-in), the shape of this is now fairly clear even without a name
for it: something in Office's own logic reserves this exact rectangle --
framing it with real border-effect windows -- for content it never actually
produces, at the drawing-call level or the window-creation level either
one. That is a different, and arguably more mundane, class of bug than
everything else patched in this document so far (all of which were about
content that *was* produced failing to reach the screen): here, nothing
downstream is broken because nothing was ever handed to it. The likely
next step, not taken this session, is upstream of any rendering API this
document's instruments reach at all -- most plausibly a resource, template,
or first-run/upsell content lookup that fails silently and leaves Office
believing it still has something to show. `WINEDEBUG=+file,+reg,+wbemprox`
watching for what such a lookup might touch in the seconds between the
document opening and the rectangle appearing is the concrete candidate for
whoever picks this up next.

### The black rectangle: named, if not yet fixed

`+file`, checkpointed right at the "新建空白文档" click and read only from
that point forward (191 lines before the document window appears -- this
channel is otherwise too large to read whole), finds the one thing the
previous three negative results were missing: two threads, right as the
document is created, both look for

    C:\users\crossover\AppData\Local\Microsoft\Office\16.0\Feedback\*.zip
    C:\users\crossover\AppData\Local\Microsoft\Office\16.0\Feedback\*.json
    C:\users\crossover\AppData\Local\Microsoft\Office\16.0\Feedback\PetrolPayloads\*

and find nothing -- `PetrolPayloads` exists as a directory (confirmed on
disk in the live prefix too) but is genuinely empty, `.`/`..` only. The
trace ends there: no network attempt, no fallback, no error. That silence is
the same shape as everything else already found about this rectangle -- a
reserved space with nothing behind it -- but this time with a name and a
location on disk rather than just a pixel region.

"Feedback"/"PetrolPayloads" reads as a downloaded-content cache for some
Office 365 cloud-connected feature (a tip, an insight card, a promotional
panel -- the read-only instruments here cannot say which), populated by a
payload this install has never fetched. Whether that is because the fetch
itself never runs (a gate failing the same silent way `Not_C2R` and
`OnlineRepair` did earlier in this document), or because `import-office.sh`
was never going to carry it (a live download cache, not something a
reference machine's own copy of it would necessarily still hold either),
was not determined. The concrete next check, not yet done: look at whether
the machine `import-office.sh` sources from has anything under this same
path, and whether `+wininet`/`+winhttp` over the same narrow window shows
any attempt to reach a server for it at all.

### Both remaining checks hit friction, neither answered

Two things: whether the reference machine has real content under
`Feedback\PetrolPayloads`, and whether a network fetch for it is even
attempted here.

The first cannot be checked from anything already in this tree --
`office-registry/` holds only registry exports (`.reg` files), never a copy
of this specific `AppData` cache directory, so there is nothing here to
compare against. It would need pulling that one directory from the
reference machine specifically, not something already on hand.

The second was attempted and hit real friction instead of an answer:
`WINEDEBUG=+file,+reg` together hung the process solid (log size static for
95+ seconds, 0.2% CPU, no exit) right around the same licensing-check point
this document has traced before; a second attempt with
`+wininet,+winhttp,+winsock` hung the same way, this time before even
reaching a visible window. Both were killed rather than pushed further.
Whether this is a genuine Wine bug in how several trace channels interact
under load, or a coincidence of an already-slow start-up pushed past some
timeout, was not determined -- two hangs on two different channel
combinations is enough to say this needs a cleaner reproduction before
trusting any answer it gives, not enough to call it understood.

### The network question, answered cleanly on the second try

Dropping `+winsock` and keeping only `+wininet,+winhttp` avoided the hang
entirely -- this run progressed normally end to end, so the earlier hangs
were specific to `+winsock` (or that exact three-channel combination), not
a general "this install cannot be traced under load" problem. Worth a
one-line warning for whoever reaches for these channels next: `+winsock`
alongside `+wininet`/`+winhttp`/`+file`/`+reg` hung this process solid twice
in a row before this; dropping it did not.

Checkpointed the same way as the `+file` measurement, the same narrow
window (document creation to the rectangle appearing) shows only
`wininet:DllMain` thread-attach/detach housekeeping on several new worker
threads -- no `InternetConnect`, no `HttpOpenRequest`, no `WinHttpConnect`,
nothing that initiates an actual request. So the answer to the open question
is clean and negative: **no network fetch for the missing payload is even
attempted.** Combined with the earlier `+file` result, the full picture for
this rectangle is now: a reserved space, an empty local cache directory
checked once, and no attempt to fill that cache from anywhere. Whatever
decides not to try either never considered it worth trying here, or was
gated off before it could -- indistinguishable from here, and the same
shape as `OnlineRepair` elsewhere in this document.

## The crash: the exact Wine function involved, if not yet the verdict (2026-09-04)

`RtlRestoreContext` (`dlls/ntdll/signal_x86_64.c`) is where `STATUS_UNWIND_
CONSOLIDATE` is actually handled: it reads a callback function pointer out
of the exception record itself (`rec->ExceptionInformation[0]` -- supplied
by Office's own compiled C++ EH code, not Wine's), calls it through
`call_consolidate_callback` (a small hand-written assembly trampoline that
fakes up a stack frame matching the given `CONTEXT`, restores the
callee-saved registers and XMM6-15 from it, and calls the callback with that
context as its argument), takes the `RIP` the callback returns, and resumes
execution there via `NtContinue`.

The fourteen-times-repeated pattern this document already measured --
`RtlUnwindEx` with the same `target_ip`, the same handler called, the same
`RtlRestoreContext` return point -- is consistent with this callback being
invoked but never causing forward progress: whatever it is supposed to do
once consolidated, it does not complete, and the same unwind gets raised
again. Two explanations fit equally well from what has been measured so
far, and nothing here distinguishes them: Wine's trampoline could be
setting up a context the callback (compiled by MSVC for real Windows)
subtly misinterprets, given how much of this depends on exact register and
stack layout the comment above the assembly spells out in detail; or
Office's own callback could genuinely not terminate for this specific input
regardless of platform, which would make this an Office bug this tree
cannot fix and would not need to.

Telling those apart needs one of: the same scenario reproduced on real
Windows (not available in this environment), or reading what the
callback itself actually does at `target_ip` (the address is already known
from the trace) -- a disassembly task subject to the same PDB-address-delta
problem this document has hit repeatedly for Office's own modules, not
attempted this session. This is as far as the "read the ntdll source"
instruction reaches without one of those two: the mechanism is now
precisely located and understood, the verdict on which side it belongs to
is not.

## The crash: symbolized after all -- VBA's own line parser (2026-09-04)

The PDB-delta problem that has blocked symbolizing Office's own modules
throughout this document turned out not to apply here, because the crash's
`target_ip` does not belong to Office's own code at all. Mapping it against
every module this run loaded (`+loaddll`) puts it in
`Common Files\Microsoft Shared\VBA\VBA7.1\VBE7.DLL` -- the VBA runtime, a
much older, more stable, separately-versioned Microsoft component than
Office's own DLLs, and its PDB (fetched fresh from `msdl.microsoft.com`,
matching this exact binary's GUID/age) resolves cleanly with
`llvm-pdbutil dump --publics`, no delta correction needed: the nearest
public symbol to the target RVA is only 150 bytes before it,

    ?parseOneLine@parser@@SAJPEAPEADIPEAUPRSCONTEXT@@PEAEIPEAIPEAKPEAHPEAUBTSRC_ERR@@@Z

-- `parser::parseOneLine`, a static method of VBE7's own `parser` class.
`pImmSetCompositionString` sits a few entries away in the same symbol table
(unrelated to this specific address, but confirms this module's own
public surface is IME-adjacent, consistent with everything else found
about this crash).

**Worth taking seriously, not yet confirmed:** the input that reproduces
this crash contains a colon (`"test: hello world 你好again"`), and a colon
is BASIC's classic inline statement separator -- exactly what a line parser
named `parseOneLine` exists to recognize. That two independent facts line
up (a parser function, and input containing the one character most likely
to make a parser's job harder) is suggestive, not proven -- what actually
calls into VBE7's parser while typing plain document text, and why it would
be reached at all outside the VBA editor, is not established. A plausible
guess, nothing checked: some AutoCorrect/AutoText/macro-recognition feature
in Word shares this parsing engine and is quietly fed keystrokes as they
land, and the specific combination this run typed reaches a real bug in it
(Wine's or the parser's own) that a plain ASCII string, or two isolated
CJK characters, does not.

This still does not settle Wine-vs-Office attribution for the
`STATUS_UNWIND_CONSOLIDATE` loop itself -- that question is unchanged from
the previous section. What changes is that "read what the callback does"
is no longer blocked on a hard PDB-delta wall the way this document's
other symbolization attempts have been: this specific module's symbols are
trustworthy, and the actual disassembly at `parser::parseOneLine`+150 (not
done this session) is a real, reachable next step rather than one gated on
resources this environment does not have.

## The crash: disassembled -- it is a longjmp across C++ scopes (2026-09-04)

The instruction at `target_ip` itself, not just its containing function:

    2677ae:  lea    0xb0(%rsp),%rcx
    2677b6:  mov    %rsp,%rdx
    2677b9:  call   0x379c70
    2677be:  mov    %eax,0x5c(%rsp)     <-- target_ip
    2677c2:  cmpl   $0x0,0x5c(%rsp)
    2677c7:  je     0x2679a5

`target_ip` is simply the return address of the call two instructions
earlier -- entirely ordinary so far. What makes it worth reading: the
call at `0x379c70` resolves, symbol table exact match, to **`_setjmp`**.

`parser::parseOneLine` calls `setjmp()` near its own start and branches on
the result the same way any C-style error-recovery scheme does. Somewhere
in whatever it calls after that point (not traced this session), a
`longjmp()` back to this exact spot is triggered. On the MSVC/Windows ABI,
`longjmp` crossing frames that have live C++ scopes (destructors, `catch`)
does not just restore registers -- it goes through the same unwind
machinery as a thrown exception, `RtlUnwindEx` with
`STATUS_UNWIND_CONSOLIDATE`, specifically so those scopes get torn down
correctly on the way. That is exactly the mechanism this document already
traced into `RtlRestoreContext`/`call_consolidate_callback` two sections
up. Put together: **this is a longjmp trying to cross scopes on its way
back to a setjmp point in VBA's own parser, and on this tree it never
successfully arrives -- the same unwind gets raised fourteen times instead
of completing once.**

This does not, on its own, convict Wine's `call_consolidate_callback` --
this is a well-known hard case (longjmp interacting with SEH-style unwind
across compiler/platform boundaries has been a source of real bugs on
native Windows too), and nothing here rules out the possibility that
VBA's own parser mishandles some malformed input regardless of platform.
What it does rule out is "nothing more can be learned without real Windows
or an unreachable disassembly" -- the mechanism is now understood down to
the specific instruction and the specific ABI feature involved. Whether
`call_consolidate_callback`'s hand-written register/stack setup is subtly
wrong for this exact longjmp-via-unwind case, as opposed to the more usual
thrown-exception case 0008 and other patches in this tree have already
exercised successfully, is the question a fix would need to answer -- and
is where this stops for this session, not for lack of a path forward but
for lack of remaining time to verify a change this central carefully
enough to trust it.

### One more data point: this class of bug has a real precedent, already fixed

A web search for `call_consolidate_callback`/`STATUS_UNWIND_CONSOLIDATE`
turns up a genuine, historical Wine bug: "Fix unwind from
call_consolidate_callback() for PE build on x64", landed in Wine 5.16
(August 2020) -- exactly this mechanism, exactly this scenario (PE build,
x86_64), already recognized and fixed upstream once. Wine 11.0, this tree's
base, is many years past that release, so the fix this history describes
is already in the copy of `signal_x86_64.c` read above -- `git log` on this
file in `wine-src` shows only the pristine wine-11.0 import and CrossOver's
own patch set on top of it, nothing narrower to point at.

So this is not that bug recurring untouched; if it is the same *class* of
bug at all, it is a related edge case the 2020 fix did not cover -- longjmp
specifically, rather than the general thrown-exception case that fix's own
description suggests it targeted. That the general category has a real,
documented upstream history is worth having: it says the hypothesis this
document has been building (a genuine gap in Wine's own unwind handling,
not an Office-only quirk) fits a known pattern, without yet proving this
specific instance is in that pattern rather than being VBA's own bug.
Distinguishing the two needs the verification already named two sections up
and was not attempted further.

## The black rectangle: what "Petrol" actually is, found locally after all (2026-09-04)

The reference-machine check turned out not to be necessary -- the answer
was already sitting in this install's own files, just not under `AppData`.
Office ships a large bundle of web-based service modules under
`Office16\sdxs\FA000000136\OfflineFiles\`, and one of them is named
`services-petrol-*.js` outright. Its actual content, not just its name:

    PetrolService, getSurveyDefinition, defaultSurveyDefinition,
    windowTitle: "customer_feedback_dialog_adhoc_window_title",
    surveyText: "customer_feedback_dialog_adhoc_title",
    placeHolderText: "customer_feedback_dialog_textarea_placeholder",
    surveyDisclaimerBelow, hasEmailField, hasLogCollectionField,
    hasScreenshotCollectionField, ...

**"Petrol" is Office's in-product customer-feedback/survey prompting
service** -- the "how's it going, mind giving us feedback" dialog
infrastructure, config-driven (`getSurveyDefinition`, `surveyOverrides`,
ECS-gated settings referenced by name). `Feedback\PetrolPayloads` is its
downloaded-survey-config cache, confirmed empty and confirmed never
fetched (previous two sections). The black rectangle this document has
been chasing across four instruments is, in that light, very plausibly a
survey/feedback prompt panel Office positioned and framed but never had a
survey definition to actually render -- consistent with every other
measurement: reserved space, no window content, no draw calls, no network
attempt, and now a name for what was supposed to go there and why an
install like this one would never have fetched it (survey prompting is
exactly the kind of feature that would be gated on genuine telemetry
participation, which this offline/imported/grace-period install was never
going to have).

Not confirmed, because it does not need to be to make this a reasonable
place to stop: that this specific rectangle *is* a Petrol survey panel
rather than some other consumer of the same cache directory. The shape of
everything measured fits it well enough that chasing the reference machine
for the same answer is no longer the only way to get one.

## Correction: the Petrol theory was tested and it does not hold (2026-09-04)

Disabling Office's documented customer-feedback/survey policy
(`HKCU\Software\Policies\Microsoft\Office\16.0\Common\Feedback` and
`HKCU\Software\Microsoft\Office\16.0\Common\Feedback`, both
`Enabled`=0 -- the real, standard admin controls for this feature) and
recreating a fresh blank document afterward: **the rectangle is still
there, identical size, identical position.** Reverted both keys
immediately after the test.

This directly refutes the previous section's conclusion, not just leaves
it unconfirmed -- if this rectangle were genuinely the Petrol survey panel
gated the ordinary documented way, turning the feature off should have
removed it, and it did not. Keeping the correction visible rather than
quietly editing the earlier section: the `services-petrol-*.js` bundle and
the empty `PetrolPayloads` cache are real facts, and the earlier reasoning
connecting them to this specific rectangle was a plausible story that a
direct test disproved -- exactly the trap this project's own method exists
to catch. What draws this rectangle is back to unidentified.

## A workaround attempt for the crash: disabling VBE7 entirely, inconclusive (2026-09-04)

Rather than fixing the unwind mechanism itself, tried sidestepping it:
`WINEDLLOVERRIDES="vbe7="` to keep VBE7.DLL from loading at all, on the
reasoning that a plain document-editing session does not need VBA, and if
the crashing parser never loads, the specific input that reaches it cannot
matter.

Word does start this way, and does reach a document -- the ribbon loses
`Zotero` (which needs VBE7) but keeps `MathType`/`AxMath` (which apparently
do not). That much is a clean, useful fact on its own. But the actual test
-- does the same crashing input still crash -- came back confounded rather
than answered: this run produced **two licensing dialogs** side by side,
something no other run this entire session has shown, and "新建空白文档"
landed on the Start screen's own content rather than opening a fresh
document canvas. Something about Office's own start-up path behaves
differently, and unexpectedly, with VBE7 absent -- consistent with VBA
being consulted somewhere in that path for reasons that have nothing to do
with user macros, which is itself worth knowing, but it means this
particular run cannot answer whether the crash is avoided, only that
disabling VBE7 is not a clean, side-effect-free way to find out. Reverted
(no state changed outside this one process's own environment variable;
nothing to undo).

### Re-tested cleanly: it is not confounded, VBE7 is load-bearing for this path

Re-ran with a fully confirmed-clean prefix (no leftover process, no
leftover dialog) rather than back-to-back without checking. This time only
one licensing dialog appeared, as normal -- the earlier "two dialogs"
observation was not a one-off launch artifact, it reproduces on demand:
**pressing "新建空白文档" a second time, with VBE7 still absent, opens a
second licensing dialog instead of a document.** The first press does not
reach a document either -- `OpusApp` stays on the Start screen's own
content (`_WwF` sized for the Start view, no `_WwG` canvas, no "Word 文档"
accessible client) no matter how long it is given.

So this is not a confound to work around -- it is a clean, reproducible
negative result on its own terms: **disabling VBE7 does not just lose
Zotero, it breaks "create a new blank document" outright.** Whatever
"新建空白文档" does internally, it depends on VBE7 being loaded to actually
reach a document rather than re-showing licensing/backstage content. That
rules this workaround out regardless of whether it would also have avoided
the crash -- it is not a usable substitute for having VBA loaded, only a
different, worse failure than the one it was meant to sidestep.

## An unrelated find while testing the black rectangle: DisconnectedState hangs Word (2026-09-04)

Trying a broader privacy control than the Feedback-specific one already
ruled out: Office's documented "Connected Experiences" policy,
`HKCU\Software\Policies\Microsoft\office\16.0\common\privacy` with
`DisconnectedState`=2 (the real admin setting for "disallow all connected
experiences"). Set it, launched Word plainly (no debug channels this
time): **no window ever appears.** Waited four minutes, checked twice more
-- log size static, `%CPU` static at 0.2 the whole time, process alive the
whole time. Killed it, deleted the registry value, launched again with
nothing else changed: window appears normally, same as every other run in
this document. Reverting is what brought it back -- not a coincidence of
timing.

**`DisconnectedState=2` hangs Word's start-up solid under this tree.**
Not investigated further this session (no diagnostics were running when it
was found, and re-triggering it just to add tracing was not done given how
long this session has already run) -- worth its own entry rather than
folding into the black-rectangle thread it was actually testing for, since
it is a separate, real finding on its own: whatever Word's start-up path
does when told to be fully disconnected, it does not complete. Left
reverted; do not set this key again without expecting the hang, and
whoever picks this up next should reproduce it once under `+ole,+rpc` or
similar before trying anything else, since a clean repro is already in
hand here.

### Correction: not a confirmed hang -- just slow, possibly very slow

Retested to get a trace of where the earlier apparent hang was stuck.
With `+ole,+rpc`, the same static-log symptom reproduced (48+ seconds,
zero growth, log ending right after COM apartment creation). But a third
attempt, `+reg` alone, did not hang the same way: the log grew slowly and
a window eventually appeared. That is not consistent with a genuine
deadlock -- it says the first two attempts were not necessarily hung
forever, only slower than the four minutes and 48 seconds respectively
that were waited before concluding so.

Downgrading the earlier claim rather than leaving it overstated: what is
actually confirmed is that `DisconnectedState=2` makes start-up take
substantially longer than normal, past the point this document's other
measurements have needed to wait, and that at least one run under it did
eventually reach a window. Whether it always eventually completes, or
whether the `+reg` run above just got lucky, was not settled -- that would
need one long, patient, unwatched run with generous logging to actually
answer. Reverted, as before.

### A cross-app comparison attempted, cut short by an unrelated Excel crash

Tried launching Excel to see whether the same rectangle (same size,
similar position) shows up there too, which would separate "a shared
Office/NUI framework thing" from "specific to Word". Excel showed its own
safe-mode-style dialog at a slightly different size (330x102 vs Word's
usual ~331x102 -- close enough to guess it was the same kind of prompt);
dismissing it with Word's own established click offset was followed
immediately by `wine: Unhandled page fault on read access to
0000000000000000`, Excel gone. Whether the crash is caused by that click
landing wrong for Excel's own dialog layout, or is unrelated to the click
at all, was not determined -- this session has not otherwise touched
Excel, so there is no baseline here to say which. Not pursued further:
this document's entire method and every instrument in it was built and
tuned against Word specifically, and chasing a new, separate crash in a
different application is a new investigation this session does not have
room to start. Worth a single line for whoever does: Excel does not
currently reach a usable window in this prefix either, on the one attempt
made.

## The crash: one real correction, and a boundary reached carefully rather than guessed past (2026-09-04)

Went back to resolve the actual consolidate-callback function, not just the
module. It symbolizes exactly (delta zero) to
`__FrameHandler4::CxxCallCatchBlock` in `vcruntime140_1.dll` -- the standard
Microsoft C++ Runtime's own function for invoking a `catch` block once an
exception has been correctly unwound to it. That is not obscure or
suspicious; it is exactly what `STATUS_UNWIND_CONSOLIDATE` exists to
support, working as designed.

Reading where this sits in the log properly corrects something stated too
strongly earlier: the crash thread calls this **four times**, each with a
genuinely different exception record, in what reads as ordinary sequential
exception handling -- not the anomaly. The fourteen-times-identical
`RtlUnwindEx` sequence this document has been calling "the crash" starts
**later**, at a different point in the log, and its exception record
carries `code=80000026` -- `STATUS_UNWIND_CONSOLIDATE` **raised directly**,
not a C++ exception that later needed consolidating. That is the shape
`longjmp` itself takes on this ABI, not a side effect of one.

Past that point, `grep` for `RtlRestoreContext calling consolidate
callback` after this sequence begins comes back empty -- the trace this
document has relied on for the whole conclusion "the callback is invoked
and the same unwind gets raised again" does not actually show a consolidate
callback being invoked at all for *this* specific sequence. What is
genuinely established: the crash thread's failing sequence is a direct
longjmp-shaped unwind, fourteen times identically, that never reaches
`RtlRestoreContext`'s own consolidate-callback trace line the way the
earlier (unrelated, benign) `CxxCallCatchBlock` sequence did. What is not
established: which specific piece of code decides to raise it again each
time, and why -- resolving `rec->ExceptionInformation[0]` for this specific
record turned out not to be a plain code-pointer read the way it was for
the earlier sequence (it landed on what looks like a stack address, not a
module address), and untangling that correctly needs more careful work
with x64 SEH's `_JUMP_BUFFER`/longjmp-specific exception record layout than
this session has budget to get right without a real risk of stating
another wrong conclusion as confidently as the first one. Left here
deliberately, rather than pushed to a guess: the correction above is solid
and worth having, the remaining question is not.

## Save, checked properly this time: Ctrl+S does not reach a Save-As prompt (2026-09-04)

The earlier "Ctrl+S did not visibly do anything, not determined" note from
the document-editing section, followed up: typed plain ASCII text into a
fresh, never-saved document, then Ctrl+S, twice -- once from wherever focus
already was, once after explicitly clicking into the document canvas
first. Neither reached a "另存为" (Save As) prompt, which a genuinely new,
unsaved, non-cloud-linked document should show on Windows. `+file` over
the same window shows no `.docx`-shaped `NtCreateFile`, only many small,
repeated `NtWriteFile` calls to a couple of already-open handles -- the
size and repetition (dozens of ~0x28-0x6c byte writes to the same two
handles) reads like Office's own telemetry/event logging, not a document
being written to disk.

Not fully isolated from real confounds this session already knows about:
the licensing dialog was still up throughout (a second top-level window
that may have held focus instead of the document), and this environment's
window manager does not support `_NET_ACTIVE_WINDOW`
(`xdotool windowactivate` fails outright here), so "did Ctrl+S truly reach
the document window" cannot be stated with full confidence from what was
tried. What can be stated: across two attempts, no Save-As dialog was
observed, which is enough on its own to move this from "unclear" to "a
real, open question worth someone's attention" -- not enough to say
whether the keystroke never arrived, arrived and did nothing, or arrived
somewhere save-relevant that produced no visible dialog for some other
reason.

## A major correction: the exception itself does not appear to be fatal (2026-09-04)

While testing Save (separately, plain ASCII text only, no CJK at all this
time), the exact same signature reappeared -- `wine: Unhandled exception
0xe0000002 in thread 190 at address 00006FFFFF3BD907 (thread 0190),
starting debugger...` -- on a run that never typed anything but ASCII.
That alone already says this is not specific to non-ASCII/mixed-script
input the way earlier sessions concluded; whatever narrowed it to "longer
mixed strings crash, short CJK alone does not" was tracking something
correlated, not the actual trigger.

More importantly: **the log kept going for over 1.1 million further lines
after that message, on that same thread (0190), doing ordinary-looking
file lookups with no further sign of trouble.** The thread was not killed
by this exception -- Wine's crash handler caught it and execution resumed
normally. The process did eventually disappear (confirmed gone by the time
this was checked, well after the fact), but nothing in the log points to
when or why -- no second exception, no clean-exit message, nothing at the
point the log simply ends. Whatever actually ends the process this time is
not established, and may not even be the same thing each time -- this
session's own handling of the process in between (an `xdotool
windowactivate` that errored given this environment's window manager, a
`--window`-targeted key send of uncertain delivery) cannot be ruled out as
the actual cause on this specific run, separate from anything Wine or
Office did on their own.

This changes what every earlier section calling this "the crash" was
actually describing. The exception, the longjmp/STATUS_UNWIND_CONSOLIDATE
mechanism, the VBE7/`CxxCallCatchBlock` symbolization -- all of that is
still real and still measured. What is no longer safe to say is that this
specific event is *why* Word disappears, or that it depends on the input
containing non-ASCII text. Both were inferred from correlation (type X,
process gone shortly after) without ever confirming the causal link
directly, which this document's own method exists to catch and normally
would have -- the process surviving visibly past the exception, on its own
trace, is exactly the kind of direct evidence that should have been sought
before drawing the earlier conclusion, and was not.

## Closing the gap properly: it is fatal, reliably, without tracing -- a genuine race (2026-09-04)

The previous section's open question -- does this exception actually kill
Word, and does it depend on input content -- was worth answering directly
rather than leaving open, so it was: same steps (blank document, click
into it, type the same plain-ASCII "save test via button"), monitored with
a tight `pgrep` loop checking every second instead of doing anything else
in between.

**Without any `WINEDEBUG` tracing, the process was gone within one second,
twice in a row, reliably.** With `+file` tracing active (previous section),
the same input let the process survive for over a million more log lines.
That is not two contradictory results -- it is the signature of a genuine
race: heavy tracing slows every thread down by roughly the same amount,
which is exactly the kind of change that flips the outcome of a race
without touching what is actually racing. At normal speed, this reliably
kills the process; slowed down enough by tracing overhead, it reliably
does not.

So, corrected once more, carefully this time with direct observation
rather than inference either way: this is a real, fatal-in-practice bug --
confirmed by watching the process disappear, not by typing something and
checking later -- and it does not require non-ASCII input, only reaching
whatever code path this specific typing sequence reaches (both crashing
runs typed identical plain-ASCII text). Whether it needs *this exact*
sequence or any keystroke that reaches VBA's parser at all was not tested
further. The mechanism already established -- `longjmp` through
`parser::parseOneLine`'s `setjmp`, `STATUS_UNWIND_CONSOLIDATE`, and
whatever makes that not resolve cleanly on this tree -- stands; what was
missing before was direct confirmation that it actually ends the process
in ordinary (untraced) operation, and that confirmation is now in hand.

### Not about settling time -- the race is inside the operation itself

One more check: waited an extra 60 seconds after the document appeared,
letting any background/initialization thread activity fully quiet down,
before clicking in and typing. Still gone within one second, same as
without the wait. So it is not overall system quiescence that decides the
outcome -- whatever races is happening specifically during the parse this
keystroke sequence triggers, not against unrelated background work that
settles over time. Consistent with the tracing-overhead result: what
matters is the relative speed of the specific operations involved in this
one parse-and-unwind, not how long the process has been sitting idle
beforehand.

### One more black-rectangle check, confounded by the environment itself

Tried resizing the Word window (`xdotool windowsize`, 1440x810 -> 900x600)
to see whether the rectangle repositions proportionally with the document
(content-anchored) or stays at a fixed offset (overlay-anchored) --
`uidump` right after still reports `OpusApp` at its original `1440x810`,
so Word's own layout never actually registered the resize. This
environment's lack of a real window manager (already known from the
`_NET_ACTIVE_WINDOW` gap elsewhere in this document) means an X11-level
resize here does not carry the `ConfigureNotify`/WM-mediated handshake a
real desktop would, so this test answers nothing about the rectangle and
was not pursued further -- a confound in the test method, not a finding
about Office.

## The user authorized attempting a core-Wine fix; it did not work (2026-09-04)

Given explicit authorization to accept the risk, a mitigation was
attempted rather than a root-cause fix, reasoning from the strongest
evidence in hand: tracing overhead reliably prevents the crash, so a
deliberate, narrow delay right where `RtlUnwindEx` (`dlls/ntdll/
signal_x86_64.c`) recognizes `rec->ExceptionCode == STATUS_UNWIND_
CONSOLIDATE` -- covering every path through the function regardless of how
it resolves -- should reproduce that effect on purpose, scoped to this one
rare exception code rather than slowing every unwind in the process.

**It did not work, at two very different scales, and both results are
informative.**

At 1ms (`NtDelayExecution`, -10000 in 100ns units): rebuilt, installed,
re-synced into the live prefix, confirmed with a clean launch this delay
did not break ordinary start-up. Reproduced the exact same crash sequence
that has been killing the process within one second all along (blank
document, click in, type the same plain-ASCII string, `pgrep`-monitored
every second): **gone within one second, unchanged.** So a delay of this
size at this exact location changes nothing.

At 100ms (100x larger, to test whether this location can work at all
before concluding it cannot): start-up itself became so slow it did not
reach a window in over seven minutes of waiting. That is itself a real
finding, independent of the crash: **this exact `STATUS_UNWIND_CONSOLIDATE`
path is exercised far more often than just this one keystroke sequence** --
likely dozens or hundreds of times through ordinary start-up -- so a
blanket delay here is not a viable mitigation at any size large enough to
matter, even before asking whether it fixes anything.

Together these say the insertion point itself is wrong, not just the
delay's size: whatever the race is actually waiting on, it is not "more
wall-clock time at the moment this one function recognizes this one
exception code" -- if it were, even 1ms should have shifted the outcome at
least sometimes, and it did not, not once. The reasoning that led here
("tracing slows everything down, so a targeted delay should approximate
that") turned out to be too simple: tracing's effect is apparently not
reducible to a single well-chosen sleep, or its effect is on a different
thread's timing entirely, one that never routes through this function at
this point.

**Reverted completely** -- confirmed via `git status`/`git diff` on
`wine-src` that the source is back to its pre-attempt state, rebuilt,
re-synced, and confirmed a plain launch is back to its normal (fast)
start-up time. No trace of this attempt remains in the running build. The
crash itself is exactly where the previous sections left it: real,
reliably fatal at normal speed, mechanism traced to a `longjmp` through
VBA's `parser::parseOneLine` reaching `STATUS_UNWIND_CONSOLIDATE`, cause
of the race not identified. This attempt narrows what the fix is not,
which is worth having even though it is not the fix itself.

## Four corrections that change what "the crash" is (2026-09-04, later session)

Picking this up fresh, four things this document has been building on turned
out to be wrong, and each was wrong in a way that made the next one harder to
see. They are listed in the order they were found, because that order is the
argument.

### 1. `0x80000026` is `STATUS_LONGJUMP`, not `STATUS_UNWIND_CONSOLIDATE`

The section "one real correction, and a boundary reached carefully" reads the
fourteen-times-repeated unwind's exception record as carrying
`code=80000026` and calls that "`STATUS_UNWIND_CONSOLIDATE` **raised
directly**". It is not. From this tree's own `include/ntstatus.h`:

    #define STATUS_LONGJUMP                  ((NTSTATUS) 0x80000026)
    #define STATUS_UNWIND_CONSOLIDATE        ((NTSTATUS) 0x80000029)

`0x80000026` is `STATUS_LONGJUMP`. That single substitution resolves both
loose ends the earlier section left open and could not explain:

  - **Why `rec->ExceptionInformation[0]` "landed on what looks like a stack
    address, not a module address".** For `STATUS_UNWIND_CONSOLIDATE` that
    field is a callback function pointer, so a stack address there is an
    anomaly worth a session. For `STATUS_LONGJUMP` it is the `jmp_buf`
    pointer -- `NTDLL_longjmp` in `dlls/ntdll/signal_x86_64.c` sets exactly
    that: `rec.ExceptionInformation[0] = (DWORD_PTR)buf`. A `jmp_buf` lives
    on the stack. It was never an anomaly; it was the correct value for the
    code that was actually there.
  - **Why grepping for `RtlRestoreContext calling consolidate callback`
    "comes back empty" for this sequence.** `RtlRestoreContext` has two
    branches, and only the `STATUS_UNWIND_CONSOLIDATE` one carries that
    TRACE. The `STATUS_LONGJUMP` branch restores registers out of the
    `jmp_buf` and prints nothing. The silence was the other branch running
    correctly, not a mechanism failing to engage.

So the sequence is fourteen ordinary `longjmp`s, which is exactly what the
disassembly two sections earlier had already established
(`parser::parseOneLine` calling `_setjmp` and something below it jumping
back). Nothing about it is anomalous on its own, and the hypothesis this
document built on top of the misread constant -- that Wine's
`call_consolidate_callback` might be mishandling this case -- was
investigating a function that never ran.

### 2. Word's disappearance is a clean exit, status 0 -- not a crash

Never measured before, and it takes one line: run wine as a job of the
invoking shell rather than through `setsid`, and `wait` for it.
`run-word.sh` ends in `exec`, so the job's pid *is* wine's, and wine is an
ELF binary whose exit status is the Windows process's.

    === wine exit status: 0 ===

Measured on three separate runs that ended in "the process is gone". Not a
signal (a signal would report 128+n), not an unhandled exception (that path
prints and terminates with the exception code). **Office called
`ExitProcess(0)`.** Every earlier description of this as a crash, and every
line of reasoning that followed from it -- the unwind machinery, the
`_JUMP_BUFFER` layout, the attempted `RtlUnwindEx` delay -- was chasing a
crash that does not exist.

### 3. What actually ends the session is pressing the licensing dialog's OK

Reproduced with liveness checked after every individual step:

    [03:31:24] dialog #2 present?                 alive=YES
    pressed "确定" -> 0
    [03:31:26] clicked OK on dialog #2            alive=NO

and, on the next run, at the *first* dialog instead:

    [03:35:29] dialog #1 present                  alive=YES
    pressed "确定" -> 0
    [03:35:31] clicked OK #1                      alive=NO

No typing in either run. So it is not "the second dialog" and it is not an
input path: **acknowledging "Microsoft Office 无法验证此产品的许可证" is
what makes Office quit**, which on a genuinely unlicensed install is what
that dialog is for. It does not happen every time (the first run of this
session pressed OK on dialog #1 and went on to create a document and type
into it), consistent with Office allowing a bounded number of
acknowledgements before acting on one.

### 4. `run-word.sh` was pressing that button by itself, on every run

The autoclick loop added to answer the safe-mode prompt selected its target
by size:

    if [ "${WIDTH:-0}" -gt 250 ] && [ "${WIDTH:-0}" -lt 450 ] \
            && [ "${HEIGHT:-0}" -gt 60 ] && [ "${HEIGHT:-0}" -lt 200 ]

on the stated reasoning that "Office's real dialogs (licensing, sign-in)
share this title but are larger". Measured, the licensing `NUIDialog` is
**375x178** -- inside that box on both axes. So every run where the
licensing dialog appeared, which is every run, had a blind click fired into
it a second or two after it showed up.

Put together with (3), this is the whole "Word disappears after N seconds /
after typing / on about one start in three" family of reports: an automated
click nobody was accounting for, landing on the one button that makes
Office exit, at a time that varied with how long start-up took. The clicks
that appeared to correlate with typing were simply the ones that arrived
while the test was typing.

Fixed by asking `winenum` for the Win32 class and acting only on `#32770`
(the real safe-mode messagebox), never on `NUIDialog`; and
`WORD_NO_AUTOCLICK=1` turns the loop off entirely, which any measurement of
what Word does when left alone needs.

### What survives all four corrections

Two things, both re-measured this session after the autoclick was disabled:

  - **Left alone, Word works.** Licensing dialog untouched on screen, a blank
    document created from the Start screen, clicked into, and typed into with
    plain ASCII: the text lands and the process stays up.
  - **A CJK/mixed string still ends the session**, and also with `exit status
    0`. So the original "non-ASCII input kills Word" observation was right
    after all, and the later correction that revised it to "plain ASCII does
    it too" was reading the autoclick's work. Whether the text is what does
    it, or `xdotool type`'s way of delivering it (a spare keycode remapped
    per out-of-layout character), is the next measurement -- the clipboard
    carries the identical characters over a path with no keyboard in it.

One smaller thing worth recording from the same runs: typing
`"hello from wine-altars"` with `xdotool type --delay 60` put **`From`** in
the document and nothing else. Most of the keystrokes never arrived. So the
input path already loses characters on plain ASCII, before any question
about CJK, and any test that types into Word is measuring that too.

### The mechanism, in one line: the dialog is modal and holds the keyboard

The clipboard control run above looked like it settled the question -- CJK on
the clipboard, Ctrl+V, Word survives -- and it settled nothing, because the
screenshot afterwards showed an empty document. Nothing had been pasted. The
control proved only that doing nothing does not kill Word.

What it did do is force the right question: **where is the keyboard going?**
`winenum -focus` (added this session: `GetForegroundWindow`, plus `GetFocus`
and `GetActiveWindow` read after `AttachThreadInput` to the foreground
thread, since `GetFocus` is per-input-queue) answers it directly, with Word
sitting there and a document open:

    GetForegroundWindow = 00000000000100f6 NUIDialog "Microsoft Word"
      GetFocus (that thread)  = 00000000000200f4 NetUIHWND
      GetActiveWindow         = 00000000000100f6 NUIDialog

**The licensing dialog is modal and it owns the keyboard.** Every keystroke
any of these tests sent went into that dialog, not into the document -- which
is why the document stayed empty, why `Ctrl+V` did nothing, and why
`"hello from wine-altars"` produced `From` on the one run where the dialog
happened to be between instances.

And a dialog with a default push button does something specific with a
space: it presses it. Confirmed directly, one keystroke at a time, on a live
Word with the focus state printed before each:

    GetForegroundWindow = ...100f6 NUIDialog     (licensing dialog #1)
    $ xdotool key space
    GetForegroundWindow = ...60104 NUIDialog     (#1 dismissed, #2 is up)
    $ xdotool key space
    WINWORD.EXE GONE / GetForegroundWindow = NULL

Two spaces, no other input, no CJK anywhere. So:

  - `xdotool type "  中文测试 ..."` did not kill Word because of CJK. It
    killed Word because the string **starts with two spaces**, and those two
    spaces pressed 确定 on two successive licensing dialogs.
  - `run-word.sh`'s autoclick killed Word the same way, with a mouse.
  - `uiclick 确定` killed Word by asking for it politely.

All three are the same event, and it is not a bug in Wine or in Office: an
unlicensed Office, told twice that its licence cannot be verified, exits.
`exit status 0` was the tell all along.

**Everything this document says about a crash, from "Past the dialog: a
document can be created, typed into, and crashed" onward, is describing
this.** The VBE7 `parser::parseOneLine` `setjmp`/`longjmp` sequence is real
and was correctly disassembled, but it is ordinary error recovery inside
VBA's parser that happens during start-up, not a crash and not related to
this exit. The `STATUS_UNWIND_CONSOLIDATE` investigation, the
`call_consolidate_callback` hypothesis, the attempted `RtlUnwindEx` delay and
its revert -- all of it was chasing a process death that never happened. The
one thing that survives from that thread is the constant misread (§1 above),
which is worth keeping because it is what let the wrong reading stand.

### What this leaves, and what it does not

**It does not leave a crash.** There is no known way to make Word terminate
abnormally in this prefix. What there is: an unlicensed Office that puts a
modal dialog in front of the document, takes the keyboard with it, and quits
when that dialog is acknowledged twice.

**It does leave a testing problem that has to be solved before any further
"does feature X work" question can be answered honestly.** With the dialog
up, no keystroke reaches the document, so every input-driven test measures
the dialog instead. The two ways out are to license Office (the real fix,
gated where it has always been) or to take the foreground away from the
dialog deliberately for the duration of a test.

Two smaller findings from the same runs, both worth having on their own:

  - **Wine's oleacc answers `E_NOTIMPL` to `accDoDefaultAction` on standard
    Win32 controls.** Measured on the plain `Button` in Word's safe-mode
    `#32770` messagebox: `pressed "No" -> 0x80004001`. `accLocation` works on
    the same object, so `uiclick` now falls back to a real click at the
    location the object reports, which makes it work on both Office's
    NetUI-drawn dialogs and ordinary Win32 ones. On real Windows MSAA presses
    a Button through `BM_CLICK`; this is a genuine gap.
  - **The "one start in three shows no window" report is the safe-mode
    prompt, and now it can be seen.** With `WORD_NO_AUTOCLICK=1`, a hung
    start-up shows exactly `#32770 vis=1 (791,473)-(1128,607) Microsoft Word`
    with `&Yes`/`&No` buttons, the process at 0.7% CPU with its main thread in
    `anon_pipe_read`. Clicking No at the location MSAA reports for it takes
    start-up to completion normally. The old autoclick's hardcoded +203,+81
    offset from the window origin lands at (994,554); the No button is at
    (971,570)-(1023,596). **It was missing the button** -- which is the other
    half of why that prompt looked unanswerable.

## Excel: a real crash, and one Wine gap closed (2026-09-04)

Word's disappearance turned out not to be a crash at all (above). Excel's is.
Chased with the instruments this document already had, plus two new ones that
came out of it.

### It reproduces exactly, and it is not the click landing wrong

The earlier session's one attempt read it as possibly self-inflicted --
"dismissing it with Word's own established click offset was followed
immediately by `wine: Unhandled page fault on read access to
0000000000000000`". It is not the offset. `uiclick` now presses through
MSAA, falling back to a click at the location the object itself reports
(`accLocation`), so the button is pressed where Excel says it is, and the
fault is identical, at the identical address, on every run:

    wine: Unhandled page fault on read access to 0000000000000000
      at address 00006FFFF72275F4

Mapping it through `+loaddll`: `mso30win32client.dll` + `0x5775f4`.

### The PDB is useless here and `.pdata` is not -- a calibration worth keeping

`scripts/pdb-addr2sym.py` names that RVA
`IntlDate::AstronomicalCalendars::FixedDateFromGregorianDate+0x4`, which is
nonsense for a null-pointer read. This document has hit the "PDB address
delta" wall repeatedly and treated it as an unexplained nuisance; it is worth
settling, because the answer changes which tool to reach for.

Measured, on `Mso30Win32Client.pdb` fetched fresh for this exact binary:

  - The PDB's GUID/age matches the PE exactly
    (`{0BD51E38-A41E-4816-8D26-8073A2CCBB4F}`, age 2). It is the right PDB.
  - The PDB's own section headers match the PE's byte for byte, so the
    `section:offset` → RVA conversion is not where it goes wrong.
  - `scripts/pdb-symbols.py` agrees with `llvm-pdbutil dump --publics`
    symbol for symbol, so the parser is not where it goes wrong either.
  - And yet **only 3.1% of the 59,585 `.text` publics land on a `.pdata`
    function start**, and the histogram of (nearest pdata start − public
    RVA) has no mode: `+0x0` 3.2%, `+0x10` 2.7%, `−0x10` 2.6%. It is not a
    constant offset, so no calibration can rescue it.

The bytes settle it. At the PDB's address for `GetPhysicalOffset`
(`0x558c60`): `74 4e 48 83 7f 18` -- `jz`/`cmp`, the middle of something. At
the `.pdata` function start containing the fault (`0x5775c8`):
`48 83 ec 48` -- `sub rsp,0x48`, a textbook prologue.

**So: for Office's own modules, treat the PDB's addresses as unusable and use
`.pdata` for function bounds.** `scripts/pe-pdata.py` already does exactly
this and its docstring already says why; what was missing was the evidence
that the alternative is not merely inconvenient but wrong. (VBE7's PDB, used
earlier in this document, resolved cleanly -- so this is a property of
Office's own build, not of PDBs.)

### The function, read out of `.pdata`, names its own caller

`.pdata` puts the fault in `0x5775c8`-`0x57762b`, 99 bytes:

    5775c8  sub    rsp,0x48
    5775cc  mov    rcx,[rcx]              ; the interface out of the wrapper
    5775cf  lea    rax,[rip+0x6b34fa]     ; -> 0xc2aad0
    5775d6  mov    [rsp+0x28],rax
    5775db  lea    rdx,[rsp+0x50]         ; the out parameter
    5775e0  lea    rax,[rip+0x6f1819]     ; -> 0xc68e00
    5775e7  mov    dword [rsp+0x50],0x0
    5775ef  mov    [rsp+0x30],rax
    5775f4  mov    rax,[rcx]              ; <-- FAULT, rcx = 0
    5775f7  mov    dword [rsp+0x20],0xb8
    5775ff  mov    dword [rsp+0x24],0x9
    577607  mov    rax,[rax+0x30]         ; vtable slot 6
    57760b  call   [rip+0x63a3e7]         ; the CFG dispatch thunk
    577611  test   eax,eax
    577613  js     0x57761e               ; on failure, report and int3

The two `lea`s are the interesting part: they stage a `{line, tag, file,
function}` context for the error path three instructions before it is
needed. Reading what they point at is the whole answer:

    0xc2aad0  "...\winrt\Windows.Security.Authentication.Web.Core.h"
    0xc68e00  "auto __cdecl winrt::impl::consume_Windows_Security_
               Authentication_Web_Core_IWebProviderError<...>"

This is C++/WinRT's generated consumer for **`IWebProviderError`**, and slot
6 is its first property, `get_ErrorCode`.

### The gap: `S_OK` plus `NULL` is a promise C++/WinRT takes literally

`dlls/windows.security.authentication.onlineid/webauth.c` had:

    static HRESULT WINAPI token_result_get_ResponseError(..., void **error)
    {if(!error)return E_POINTER;*error=NULL;return S_OK;}

C++/WinRT's generated property getter is

    WebProviderError ResponseError() const
    {
        void* value{};
        check_hresult(...->get_ResponseError(&value));
        return WebProviderError{ value, take_ownership_from_abi };
    }

-- it checks the HRESULT and then wraps whatever pointer came back **without
testing it**. `S_OK` with a null out-pointer therefore produces a live
`WebProviderError` holding nothing, and the caller's next line is a virtual
call through it. This is the same shape as the `E_NOINTERFACE`-on-a-map gap
already recorded in `webtoken.c`: a WinRT answer that is technically a
failure indication and is never read as one.

Fixed by implementing the class: a `provider_error` object with
`IWebProviderError`'s two properties (`ErrorCode` at slot 6, `ErrorMessage`
at 7), carried on every `token_result` and handed out AddRef'd, exactly as
`get_ResponseData` next to it already does.

### Getting the IID without the metadata: read C++/WinRT's own name table

`Windows.Security.winmd` is not in this prefix, so the IID had to come from
somewhere else. C++/WinRT leaves a table of `name_v`/`guid_v` pairs in any
binary that consumes these types -- a UTF-16 interface name, then its 16-byte
IID at the next 16-byte boundary. `WebView2Host.dll` has the whole
`Windows.Security.Authentication.Web.Core` set.

The rule was **verified before it was used**: applied to
`IWebTokenRequestResult`, whose IID this file already carries from an earlier
session, it reproduces `c12a8305-d1f8-4483-8d54-38fe292784ff` exactly. Only
then was its answer for `IWebProviderError` trusted:
`db191bb1-50c5-4809-8dca-09c99410245c`.

### Confirmed fixed, and the failure moved -- which is the point of measuring again

With the object in place, `+webauth` shows the whole exchange working, and it
also shows what Office is actually doing here: it is **MSAL** (`x-client-SKU
= MSAL.xplat.Win32`) asking the broker for a token.

    statics_GetTokenSilentlyAsync request ...
    token_result_for_request no token held for scope
        L"service::ssl.live.com::MBI_SSL_SHORT openid profile" (0x80070002),
        answering status 3
    token_result_get_ResponseStatus  status 3
    token_result_get_ResponseError   get error ... (status 3)
    provider_error_get_ErrorCode     provider error ... code 0     <-- was the fault
    token_result_get_ResponseStatus  status 3
    statics_FindAccountProviderAsync provider_id L"https://login.windows.local"
    create_provider_operation        authority L"", hr 0
    provider_query                   QI {4a01eb05-4e42-41d4-b518-e008a5163614}
    [ fault ]

So the original null read is gone -- `get_ErrorCode` is reached and answered
-- and Office proceeds several calls further before failing differently:

    wine: Unhandled page fault on read access to FFFFFFFFFFFFFFFF
      at address 00006FFFF6E3D410

`.pdata` puts that at a 3-byte entry, `mso30win32client.dll +0x18d410`,
which disassembles to `ff e0` -- `jmp rax`, the target side of this binary's
CFG dispatch thunk. So the fault is not a bad *read*; it is an indirect
**call** to `0xFFFFFFFFFFFFFFFF`, i.e. a vtable slot that came back as -1.
`4a01eb05-4e42-41d4-b518-e008a5163614` is `IWebAccountProvider2`, named the
same way from the same table (its neighbouring string is
`winrt::impl::consume_Windows_Security_Credentials_IWebAccountProvider2<...>
::Authority`), and the QI for it is the last thing this DLL is asked before
the fault.

This is progress with a caveat this project's own method insists on:
**measuring the whole chain after a change, not just the symptom aimed at.**
The first fault is genuinely gone and its fix is correct on its own terms;
what replaced it is the next gap along the same MSAL/broker path, not the
old one in disguise. Whether the -1 comes from this tree's provider object
(whose four vtables are all initialised, and whose QI does AddRef) or from
Office's own handling of what it got back is the open question, and the
registers at the fault are the measurement that decides it.

### The next fault is not on this side of the boundary -- tried, measured, reverted

The obvious next suspect was the provider itself. `FindAccountProviderAsync`
answers for **every** id it is handed, and the log shows Office asking for
`https://login.windows.local` -- which on Windows is the *local or domain*
identity the OS holds, and which nothing in this prefix backs. Handing back a
`WebAccountProvider` for it means handing back one whose `Authority` is empty,
which reads like the same mistake one layer up: the caller believes the answer
and proceeds.

So it was changed and measured: answer null for any id outside
`{login.microsoft.com, login.windows.net, login.microsoftonline.com}`, which
is a normal result here (every caller of
`IAsyncOperation<WebAccountProvider>` has to handle null, because on a real
machine most providers are absent most of the time).

It worked, in the sense that the broker did what it was told --

    statics_FindAccountProviderAsync provider_id L"https://login.windows.local"
      not a provider this broker has; answering null.

-- and Excel then faulted at **exactly** the same instruction as before:
`mso30win32client.dll +0x18d410`, `ff e0` (`jmp rax`, this binary's CFG
dispatch thunk), same access violation, same address. Two byte-for-byte
different answers to that call, one identical crash.

That is a real measurement and it points the other way from the intuition:
whatever Excel dies on here **does not depend on what this function returns**,
so it is not on this side of the boundary and no change to this behaviour can
reach it. Reverted rather than kept -- it changes what Word's sign-in path is
told, for no measured benefit, and an unverified behaviour change in a broker
two applications share is a regression risk with nothing on the other side of
the scale. The reasoning is left in the file as a comment so the next session
does not spend the same afternoon on it.

Before it was reverted, one loose end from the earlier reading was also
cleared up. The register dump at the fault gave `rax=480950c5db191bb1`, which
is the first eight bytes of `IWebProviderError`'s IID little-endian, and that
looked like damning evidence of a vtable overrun into the constant next to it.
It is not evidence of anything: that analysis was done against the `.rdata`
layout of the DLL built *before* the provider TRACEs were added, and adding
them moved every constant in the section. The fault address never changed
across any of these builds, which is the fact that actually matters -- it is
in Office's code, at a CFG thunk, and this tree's `.rdata` layout has nothing
to do with it.

### Where Excel stands

  - The **first** fault, a null read through `IWebProviderError`, is a real
    Wine gap, understood down to the instruction, fixed, and confirmed fixed
    (`patches/office/0011-*`).
  - A **second**, different fault sits behind it, on the same MSAL/broker
    path, in `mso30win32client.dll`'s CFG dispatch thunk. It reproduces
    exactly, it is unaffected by what this tree's broker answers, and naming
    what Office is actually calling there is the next measurement -- one that
    needs the *caller* of the thunk, not the thunk, which means a stack walk
    at the fault rather than another round of reading `.rdata`.
  - Excel therefore still does not reach a usable window in this prefix. What
    is new is that the first of the two reasons is gone and the second is
    located precisely enough to resume from.

## Excel and PowerPoint: the shared crash, found and fixed (2026-09-04)

Both applications faulted identically, and the fault turned out to be one
missing method.

### They are the same bug, and the low bits say so

PowerPoint, never tested before this session, dies exactly as Excel does:

    Excel:      read access to FFFFFFFFFFFFFFFF at 00006FFFF6E3D410
    PowerPoint: read access to FFFFFFFFFFFFFFFF at 00006FFFF4C0D410

Different bases, identical low bits -- both are `mso30win32client.dll
+0x18d410`, which `.pdata` gives as a three-byte entry disassembling to
`ff e0`, `jmp rax`: this binary's CFG dispatch thunk. So the fault is not a
bad read at all, it is an indirect **call** to a garbage target, and it is
shared.

### The stack scan that made it findable

A thunk that small has no useful unwind info, and winedbg produced no
backtrace at all for these faults (it starts, prints two dbghelp fixmes, and
stops). What worked was a raw scan: at an unhandled exception, walk the stack
printing every qword that lands in a loaded module, module-relative. The
`call` that entered the thunk pushed its return address and it is still
there. Carried as `patches/office/0013-*`.

On PowerPoint it printed, first entry, `mso30win32client.dll +0x582659` --
and `.pdata` plus a disassembly turned that into the caller:

    58260c  mov  r11,rsp
    582614  mov  rcx,[rcx]
    582617  lea  rax,[rip+...]        ; -> 0xc2aad0
    58261e  mov  dword [rsp+0x28],0xc4
    582639  lea  rax,[rip+...]        ; -> 0xc69ae0
    58264c  mov  rax,[rcx]            ; vtable
    58264f  mov  rax,[rax+0x40]       ; <-- slot 8
    582653  call [rip+...]            ; the CFG thunk
    582659  test eax,eax              ; the return address the scan found

Reading the two staged pointers, exactly as with the first fault:

    0xc2aad0  "...\winrt\Windows.Security.Authentication.Web.Core.h"
    0xc69ae0  "auto __cdecl winrt::impl::consume_..._IWebProviderError<...>
               ::Properties(void) const"

### The correction: implementing *most* of an interface is not partial success

`IWebProviderError` has **three** properties -- `ErrorCode`, `ErrorMessage`,
`Properties` -- and the object added earlier that day had two. Slot 8 was one
past the end of an eight-entry vtable, so the indirect call went to whatever
`.rdata` followed the table.

That also corrects what this document said a few hours earlier. The section
"The next fault is not on this side of the boundary" concluded, from a real
measurement (two different answers to `FindAccountProviderAsync`, one
identical fault), that the remaining fault was Office's. The measurement was
sound and the conclusion drawn from it was wrong: the fault was invariant
under that change because it had nothing to do with that call, and it *was*
on this side -- it was this tree's own vtable being short. **A fault that
does not respond to a change says the change is irrelevant to it, not that
the fault is someone else's.**

A related false lead from the same stretch, worth recording because it looked
compelling: the register dump showed `rax` holding the first eight bytes of
`IWebProviderError`'s IID, which reads as a vtable overrun into the constant
next to it. That analysis used the `.rdata` layout of a build made *before*
some TRACEs were added, and adding them moved every constant in the section.
The fault address never moved across any of those builds, which is the fact
that mattered.

The completed object is in `patches/office/0011-*`.

### The list of what Office actually calls, extracted rather than guessed

Since the failure mode is "a method the caller reaches and the implementation
does not have", the useful thing is the caller's own list. C++/WinRT emits a
`consume_<Namespace>_<Interface><...>::<Method>` string for every method it
consumes, for its error paths, so grepping Office's binaries for that pattern
yields exactly what Office calls:

    IWebProviderError      (3): ErrorCode, ErrorMessage, Properties
    IWebTokenRequestResult (4): InvalidateCacheAsync, ResponseData,
                                ResponseError, ResponseStatus
    IWebTokenResponse      (3): Properties, Token, WebAccount
    IWebAccount            (2): UserName, WebAccountProvider
    IWebAccount2           (3): Id, Properties, SignOutAsync
    IWebAccountProvider2   (1): Authority
    IFindAllAccountsResult (3): Accounts, ProviderError, Status
    IWebTokenRequest       (1): Properties
    IWebTokenRequest2      (1): AppProperties
    IWebTokenRequest3      (1): CorrelationId
    IWebAccountMonitor     (2): Removed, Updated
    IWebAuthenticationCoreManagerStatics  (3), Statics3 (1), Statics4 (1)

Checked against this tree's vtables, every one of those is covered except
`IWebAccountMonitor` and `IWebAuthenticationCoreManagerStatics3::
CreateWebAccountMonitor`, which are not implemented at all (a QI for them
returns E_NOINTERFACE, which C++/WinRT turns into a thrown exception rather
than a wild jump, so they are a different and less dangerous shape of gap).
The full list is worth keeping: it is the checklist for this whole area.

### Excel, after: it works

Start screen with "早上好", the template card, the navigation rail, the
search box, all rendered. Clicking 空白工作簿 gets a workbook -- full ribbon
(开始/插入/页面布局/公式/数据/审阅/视图/自动执行/帮助), the A-S × 1-29
grid, name box, formula bar, status bar, zoom. Distinct colours on screen 1011
at the Start screen and 2404 with the workbook open. The title bar says
`Excel (未经授权产品)` and the licensing dialog sits in front of it, exactly
as Word's does.

Getting there took one more fix, found the same way.

### Excel's second fault: riched20 dereferences a CHARFORMAT it never checked

Creating the workbook faulted at `000000007B6F2326`, and that address is not
Office's -- `Riched20.dll` was loaded at `0x7B6D0000`, so it is riched20
`+0x22326`, which is **ours**, which means `addr2line` answers exactly:

    cfany_to_cf2w   dlls/riched20/style.c:38

-- the function's first statement, `from->cbSize`. Both of its callers hand
it a pointer they did not create: `handle_EM_SETCHARFORMAT` passes the
message's lParam, and `ME_MakeEditor` passes whatever
`ITextHost::TxGetCharFormat` left in an out parameter whose HRESULT was the
only thing checked. Excel implements `ITextHost` itself and answers `S_OK`
without filling that pointer -- allowed, and evidently survivable on Windows.

The `cbSize` checks that follow already return FALSE for a size they do not
recognise, so a null pointer belongs in the same bucket.
`patches/office/0012-*`.

### PowerPoint, after: no longer crashes, does not yet start

The shared fault is gone; PowerPoint stays up. It now stops on a plain
`#32770` of its own:

    内存或系统资源不足，无法启动 PowerPoint。   [OK]

which is a symptom, not a cause. What has been established about it:

  - **It is not actually short of anything.** A probe in the same prefix
    reports 30967 MB physical / 14418 MB available, 71 GB of page file,
    a 128 TB virtual address space, 1920x1080 at 32bpp.
  - **It is not add-ins.** `/safe` produces the identical box.
  - **It is not `GetGuiResources`** (a Wine stub that returns 0 with
    `ERROR_CALL_NOT_IMPLEMENTED`, and the obvious suspect): PowerPoint never
    calls it -- measured, zero calls in a full run with fixmes on.
  - **It is not the `.fon` font-loading warnings.** The four
    `DWRITE_E_FILEFORMAT` failures in the log are `coure.fon`, `serife.fon`,
    `smalle.fon`, `sserife.fon` -- bitmap fonts DirectWrite never supported.
  - **It can be dismissed, and PowerPoint survives**, but another instance of
    the same box follows and no main window ever appears.

Located, via the MessageBox-caller diagnostic (also `0013-*`): `ppcore.dll
+0x5ff900`, a function whose whole shape is

    ok = false; call 0x2ee260; if (!ok) { load string 0xfa; MessageBoxW }

with the success path setting a byte and returning. On one run `+seh` showed
that `ok` stays false via an exception: `RtlRestoreContext returning to
ppcore.dll +0x5ff963`, which is exactly the catch funclet that jumps back to
the `if (!ok)` test, and the exceptions before it name themselves through
their ThrowInfo as `Art::CTextLayoutException` (from `oart.dll`) and
`Ofc::CAbortException` (from `ppcore.dll`).

**That chain is not confirmed as the cause.** A later run with lighter
tracing produced the same message box with *no* C++ exception recorded at
all, so either there are two paths to this box or the exception chain is
incidental to it. Saying which needs the throw site, and the `+cxxthrow`
diagnostic written for exactly that did not fire -- see `0013-*` for the
0x6F address discrepancy that has to be explained before its silence means
anything. This is where PowerPoint stops.

## PowerPoint starts: Wine was shadowing the riched20 Office brought (2026-09-04)

The "not enough memory or system resources" box is gone and PowerPoint opens
its main window with the full ribbon. The cause was three inferences away from
where the message pointed, and getting there needed one instrument rebuilt.

### First: the diagnostic that was carried but never ran

The previous section left `+cxxthrow` "carried but NOT yet shown to work",
with the honest note that +seh reported throws at kernelbase `+0xD907` while
the `RaiseException` entry it hooked is `+0xD898`. That 0x6F is now explained,
and it explains everything:

    17400d902:  call RtlRaiseException
    17400d907:  add  $0xc8,%rsp        <-- +0xD907

`+0xD907` is the **return address of the call**, not `ExceptionAddress` --
`+seh`'s `addr=` prints `context->Rip`. So `RaiseException` really was being
called. What was wrong is that the hook was in the wrong copy of it:

    /* Some DRMs depend on RaiseException not altering non-volatile registers. */
    __ASM_GLOBAL_FUNC( RaiseException, ... )

On x86_64 `RaiseException` is hand-written assembly. The portable C version
next to it -- the one the hook was added to -- is compiled for other
architectures and never runs here. Rebuilding and disassembling the result
showed the hook was simply absent from the binary, which is the check that
should have been made before reading anything into its silence, and is the
instrument-self-test rule this document already carries (§3c) applied to a
case where the instrument was not merely unproven but not present.

### Second: a stack *scan* cannot tell a live frame from a stale one

Moved to `dispatch_exception` (`dlls/ntdll/exception.c`), where the context is
still the thrower's, the diagnostic fired -- cross-checked against `+seh`,
five C++ throws each, exactly. Its first version scanned the stack, and its
output was misleading in a specific way: it reported `oart.dll +0x44ae0`,
which disassembles to the **success** path of a UTF-16 buffer allocator
(`len*2+16`, allocation returned non-NULL). A leftover, printed as if it were
a caller. Checking each address against its section header separated them --
`oart +0xc819a0` and `+0xc5b300` are `.rdata`, `ppcore +0x1fad470` is `.data`
-- but that only filters noise, it does not produce the chain.

Replaced with a real unwind: `RtlLookupFunctionEntry` + `RtlVirtualUnwind`
per frame, falling back to "pop the return address" for leaf functions and
thunks that have no unwind info. Office's modules all carry `.pdata`, so this
produces the actual chain, and it produced this one:

    #2  oart.dll     +0x95aa6e     <- the throw helper
    #3  oart.dll     +0x95aaeb
    #4  oart.dll     +0x6496fe     <- the decision
    #5  oart.dll     +0x160d77
    ...
    #8  ppcore.dll   +0x36378a
    ...
    #20 POWERPNT.EXE +0x1ced       <- main

### Third: the decision, and it is one instruction

`oart.dll +0x6496fe` is the `int3` after a throw, and the code above it is a
plain load-and-resolve:

    6496b5:  call [rip+...]          ; get the module
    6496be:  test rax,rax
    6496c1:  jne  ...                ; got it
    6496c3:  mov  edx,0x3938d6
    6496cf:  call 0x95aaac           ; throw
    6496d5:  lea  rdx,[rip+...]      ; -> "CreateTextBoxLayout"
    6496df:  call [rip+...]          ; KERNEL32!GetProcAddress
    6496e8:  test rax,rax
    6496eb:  jne  ...                ; got it
    6496ed:  mov  edx,0x3938d7
    6496f2:  lea  rcx,[rip+...]
    6496f9:  call 0x95aaac           ; throw   <-- ours
    6496fe:  int3
    6496ff:  lea  rdx,[rip+...]      ; -> "MathBuildUp"

The proc names are right there: the first resolve asks for
**`CreateTextBoxLayout`**, ours is the one after it, **`MathBuildUp`**.

### The gap: 9 exports against 66

    Wine builtin riched20:  9 named exports
    Office's own riched20: 66 named exports
      CreateTextBoxLayout   wine=False  office=True
      MathBuildUp           wine=False  office=True

The 57 Office has and Wine does not are Office's own surface --
`ConvertEquationFromOleStream`, `ConvertLaTeXToOMMLStream`,
`ConvertMathMLStreamToOMMLStream`, `CreateTextBoxLayout`, `EnableCloudFonts`,
`FCreateMathFromLaTeX`, `GetMathAlphanumeric`, `MathBuildUp` … equation
conversion, LaTeX/MathML/OMML interchange, text-box layout, cloud fonts.
Nothing in Wine is going to grow them.

**And Office ships its own copy.** It loads
`Common Files\Microsoft Shared\Office16\Riched20.dll` by full path -- but the
name matches a known system dll, builtin wins by default, and Wine
substitutes its own. `+loaddll` says so plainly, and the word to notice is
the last one:

    Loaded L"...\Office16\Riched20.dll" at 00006FFFF7B6D0000: builtin

So the fix is not to implement 57 exports; it is to stop shadowing the file
Office brought.

    WINEDLLOVERRIDES='riched20=n'

**Measured, all three applications, with it set:**

  - PowerPoint: `PPTFrameClass` main window at full screen, ribbon rendered
    (文件/开始/插入/设计/切换/动画/幻灯片放映/记录/审阅/视图/帮助), stopping
    on the same licensing dialog as the other two. No message box at all.
  - Excel: Start screen and a blank workbook, 2403 distinct colours (2404
    without the override -- unchanged).
  - Word: blank document, 1896 distinct colours (1902 without -- unchanged).

Set permanently in the prefix (`HKCU\Software\Wine\DllOverrides`) and added
to `import-office.sh` so a rebuilt prefix gets it.

### Finding the rest of this class, rather than guessing at it

`scripts/office-dll-overrides.py` compares export tables between every dll
Office ships and the Wine builtin of the same name, and reports the ones where
Office's set is a strict superset -- "Wine's copy cannot answer questions this
copy can". On this install:

    riched20.dll    wine    9 exports, Office   66  (+57)
    msvcp100.dll    wine 1628 exports, Office 1676  (+48)
    msvcp120.dll    wine 1533 exports, Office 1569  (+36)
    concrt140.dll   wine  278 exports, Office  291  (+13)
    inkobj.dll      wine    4 exports, Office    5  (+1)

**Only riched20 is set.** The other four are reported and left alone: none of
them has been shown to matter, all three applications now reach their main
window without them, and setting overrides blind is how a working prefix
acquires problems nobody can attribute later. They are written down so that
the next unexplained "Office can't do X" has somewhere cheap to look first.

Note this is a different mechanism from `sync-prefix-dlls.sh`, which is about
*files* in system32 and deliberately does not overwrite the native VC runtimes
`import-office.sh` puts there. This is the loader deciding which copy answers
to a name, and it applies to a file that is not in system32 at all.

### What the riched20 patch is still for

`patches/office/0012-*` (the null `CHARFORMAT` in `cfany_to_cf2w`) fixed a
real crash in Excel, found before this override existed. With `riched20=n`
that code no longer runs for Office, so the patch is not what makes Excel work
today. It is kept because the bug is real and unconditional -- two callers
dereference a pointer they never check, one of them reading an out parameter
whose HRESULT was the only thing tested -- and anything else using Wine's
riched20 can still hit it.

## The display this project measures on is not a desktop (2026-09-04)

Everything in this document up to here was measured on `:77`, and `:77` is

    /usr/bin/Xvfb :77 -screen 0 1920x1080x24 -nolisten tcp

a headless virtual framebuffer. Nobody can see it. That was fine for reading
pixels back with `XGetImage`, which is most of what this document does, and it
is wrong for anything about how Office actually behaves on a desktop -- which
came up the moment the user was asked to sign in and could not find the window.

The account's real session is `:0`, reachable with the right auth file
(`XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.*`, which is why an earlier
probe reported it "not reachable" and this document then used `:77` without
questioning it). The two are not comparable:

| | `:77` (Xvfb) | `:0` (Xwayland/GNOME) |
|---|---|---|
| GL renderer | llvmpipe, no DRI3 | AMD Radeon RX 6900 XT |
| Compositor (`_NET_WM_CM_S0`) | none | present |
| Keyboard focus | `PointerRoot` | real focus window |
| `_NET_ACTIVE_WINDOW` | unsupported | supported |

**Several findings this document files under "the environment" are properties
of Xvfb specifically**, and each needs re-measuring on `:0` before it is
believed:

  - the `MSO_BORDEREFFECT_WINDOW_CLASS` shadows rendering as solid black
    (per-pixel-alpha layered windows with no compositor to composite them);
  - "this display has no focus management, so a click does not move the
    keyboard" -- true on `:77`, not on `:0`;
  - the `GLX_OML_sync_control`-absent path that
    `patches/office/0010-*` fixes. On `:0` there is hardware GL, so a
    different present path may be taken and the patch may be neither
    sufficient nor relevant there. **Reported by the user on `:0`: the
    licensing dialog's text is visible for a moment and then disappears** --
    which is not "always black" and not what 0010 addresses; content that
    paints correctly and is then overwritten is a different fault.

### Two things `:0` showed immediately that `:77` could not

**Gecko renders nothing without DRI3.** On `:77` the sign-in page's DOM loads
-- MSAA reads back `电子邮件、电话号码或 Skype` -- while `XGetImage` on
Gecko's own window returns **one colour, pure black**. `LIBGL_ALWAYS_SOFTWARE=1`
takes the same window to 1531 colours. Office's own UI is unaffected either
way because it renders through wined3d, which has a software fallback; Gecko
does not. On `:0` (hardware GL) it renders without the override.

Also: Gecko gets its **own top-level X window** sitting inside Word's frame
rectangle, not a child of it. Raising Word's frame (1440x810 at 0,0) covers
the page completely, which looks exactly like "the page went black" and is
not. Raise the browser window by geometry instead.

**The desktop is 4K with GNOME scale 2, and Wine does not know.** The monitor
is 3840x2160, `monitors.xml` says `<scale>2</scale>`, and Xwayland hands X11
clients a 1920x1080 logical screen. Wine draws 1080p and the compositor scales
it 2x onto the panel, so every Wine window edge is interpolated -- which is
what the user sees as "the resolution or scaling is wrong at the window
borders, and the pointer changes as it crosses them". This is Xwayland's
handling of unscaled X11 clients, not a fault in Office or in this tree.

### The sign-in error, and one wrong turn on the way to it

Signing in on `:0` reaches Microsoft's real form and, after an address is
entered, an error page:

    Code:  -2147467262   (0x80004002, E_NOINTERFACE)
    DPTI:  uninitialized
    Tag:   7q6ca

`E_NOINTERFACE` is a COM error, so the temptation is to match it against the
`E_NOINTERFACE`s in the Wine log. **That was tried and it does not hold**: the
four QueryInterface failures in that run (`IAgileObject` from appx, and one
GUID each from netprofm, wbemprox and msxml) are ordinary capability probes,
and the last of them is at log line 13690 of 90330 -- long before the error.
Correlation, not cause; the same trap this document's method exists to catch.

Worth recording for whoever picks this up: **the sign-in page is not Gecko
directly, it is Wine's mshtml** (`Internet Explorer_Server`,
`Shell DocObject View`, `Shell Embedding` in the window tree, and
`fixme:mshtml:DocObjOleInPlaceActiveObject_TranslateAccelerator` 68,996 times
in one run), with Gecko as mshtml's rendering backend. So a missing interface
here is most likely one of mshtml's, not one of Gecko's.

## A desktop this project can measure on, and the composited output read at last (2026-09-05)

The previous session ended on a wall: every surface this tree owns held
correct pixels, the screen showed black anyway, and *what the compositor
shows* could not be read from this side -- `import -window root` is refused
under Xwayland, and GNOME Shell's own `org.gnome.Shell.Screenshot` answers
`AccessDenied` to callers it does not recognise as an app. So the last step
of the chain was taken on trust.

It does not have to be. **mutter run headless is the same compositor with a
readable output**:

    mutter --headless --virtual-monitor 1920x1080

starts a full Wayland session with no seat, brings up Xwayland (so Wine runs
exactly as it does on a real desktop), renders through the real GPU -- `glxinfo`
in it reports `AMD Radeon RX 6900 XT (radeonsi)`, `direct rendering: Yes`, and
`GLX_OML_sync_control` present, i.e. the hardware path patch 0014 is about --
and exposes `org.gnome.Mutter.ScreenCast`, whose PipeWire stream is one
`gst-launch-1.0 pipewiresrc num-buffers=1` away from a PNG of the composited
screen.

  * `scripts/measure-desktop.sh` starts/stops it and prints `DISPLAY` and
    `XAUTHORITY`.
  * `scripts/screen-capture.py` grabs a composited frame.
  * `scripts/xwin-pixels.py` reads any X window's own pixels -- including its
    **alpha channel**, which nothing in this document had looked at before, and
    which matters because a 32-bit ARGB window with zero alpha holds perfectly
    correct colours and composites as nothing at all.

The first thing that buys: the two reads can be compared. On Word's start
screen the licensing dialog's own X window and the same rectangle of the
composited frame agree exactly -- 545 distinct colours, 0 black pixels, both.
**The compositor is faithful; where this document has black, the black is in
the window.** (Also settled in passing, and it removes a whole family of
theories: every window Office uses here is `depth=24`, alpha 255 on 100% of
pixels. Nothing is being lost to an alpha channel.)

Why this was needed at all: the account's own session (`:0`, Xwayland/GNOME)
ended part-way through the session and only the GDM greeter was left on
`seat0`, so there was no desktop to reproduce anything on. `pick-display.sh`
picks a session when there is one; this is what to do when there is not.

## The black block, named: Word's title bar and its Backstage rail (2026-09-05)

Reported: "some windows render as black blocks, and go black under the
pointer". Reproduced on the first start, and it is not a window -- it is two
regions of Word's own start screen:

    solid-black bands, client window 1438x808:  rows 0..46   cols 0..64

`uidump` names them, and the geometry matches to the pixel:

    name="Word" [title bar]            rect=(43,1,698,48)
    [pane] rect=(1,49,66,760)
      name="文件" value="NAVBAR" [list] rect=(1,49,66,760)
        开始 / 新建 / 打开 / 帐户 / 反馈 / 选项

So the accessible objects are there, in the right place, with the right names,
and their pixels are black. Everything between them -- the whole "Backstage
视图" pane, template cards, search box, the licensing dialog in front of it --
renders correctly.

Four things about it, each measured:

  * **It is in Office's own surface, not in the presentation.** The black is
    already in the XComposite-redirected client window Wine hands the swapchain
    (`0xc0022f`, 1438x808), before the blit to the top-level, and the
    composited frame shows exactly the same pixels.
  * **It is intermittent**: 2 of the 6 hardware-GL starts whose pixels were
    read. When it happens, the two bands fail independently -- one start had
    both, another had only the rail (49335 black pixels, `cols 0..64`).
  * **The content is not lost.** Resizing the window (`xdotool windowsize`,
    which makes Office re-lay-out and re-present) brings the rail back correct
    and it stays correct. One composite got it wrong and that frame stuck.
  * **It does not reproduce where this project usually measures.** Software GL
    (`LIBGL_ALWAYS_SOFTWARE=1`): clean. Hardware GL with `+d3d11` tracing on:
    clean. Both are the classic shape of a race being hidden by slowing
    everything down. (One software-GL run also rendered the *whole* start
    screen -- title bar with 登录/help/minimise/maximise/close, the rail with
    all six icons. That does **not** generalise: a later software-GL run had
    the same blank rail as the hardware ones. See the correction below.)

Not reproduced: the "goes black under the pointer" half. With the licensing
dialog up the main window is modal-blocked and does not repaint at all --
four pointer positions (over the rail, over the content, over the title bar,
over the dialog's OK) produced byte-identical captures, 117070 black pixels
every time. Whether that half is the same fault seen while frames are actually
being produced is untested.

## A real defect in the handoff, found chasing the black band -- and not its cause (2026-09-05)

Office composites its UI out of **three 2048x1280 `MISC_SHARED_KEYEDMUTEX`
atlases** (`altars-tex 2048x1280 fmt 87 misc 0x100`, three
`OpenSharedResource`s, 55 handoffs in one start-up). Patch 0008 gives each
participant its own texture and copies the content across at the keyed-mutex
handoff -- and it did the whole copy in `AcquireSync`, which means the
*acquiring* thread issued `CopyResource` and `Map` against the *releasing*
device's immediate context.

The log's own thread prefix says how consistently:

    thread 01a4  owns device 7E18118DB140 -- the three owner textures
    thread 01c0  owns device 7FFFFF745488 -- the three opened from them

    01c0  Moved 2048x1280  7E17702B73B0 -> 7FFFFF867330   x10
    01c0  Moved 2048x1280  7E1770209650 -> 7FFFFF867470   x10
    01c0  Moved 2048x1280  7E17702B7310 -> 7FFFFF867830   x9
    01a4  Moved 2048x1280  7FFFFF867470 -> 7E1770209650   x9
    01a4  Moved 2048x1280  7FFFFF867330 -> 7E17702B73B0   x9
    01a4  Moved 2048x1280  7FFFFF867830 -> 7E17702B7310   x8

Every handoff reads a texture from a thread that does not own its device.
D3D11 immediate contexts are single-threaded by contract; Wine's global lock
stops that corrupting anything, and it does **not** order the copy against
work the owning thread has already queued -- so a copy can read an atlas the
owner has not finished drawing. An atlas that arrives incomplete blacks out
exactly the elements it carried, which is why whole named UI regions go black
together and independently of each other.

`patches/office/0015-*` splits the carry so each half runs on the thread
allowed to run it: `ReleaseSync` copies the releasing texture out into a
system-memory buffer on the shared state (on the releasing device's own
thread, after its draws, while it still holds the surface), and `AcquireSync`
copies that buffer in (on the acquiring device's own thread). A generation
counter skips the copy for a participant that already holds the current
pixels, and replaces 0008's `content` pointer -- the only reason one texture
ever had to name another.

    hardware GL, untraced, before        2 of 6  starts had a black band
    after, the first fourteen starts     0 of 14
    after, the next eleven               5 of 11

`scripts/render-census.sh` is that measurement: it starts Word N times and
prints one line each, because a single good run proves nothing about a change
to a race. **It is also what took the fix away again.** Fourteen consecutive
clean starts read as a fix and were written up as one. Later the same session
the band came back, and kept coming back -- 5 of the next 11 starts, some the
full `rows 0..46` / `cols 0..64` shape, some partial. 5 of 25 overall against
2 of 6 before is no effect these numbers can show, and the fourteen-run
stretch has no explanation.

One candidate for that boundary was that it is exactly the rebuild which added
the 0016 and 0017 diagnostics: both are guarded off, both change code layout
inside the two functions this fault runs through, and the fault is
timing-sensitive enough that turning `+d3d11` on hides it completely. **That
check was run and it came back negative** -- with both diagnostics reverted and
the tree rebuilt, the band reproduced again within the first two starts. So the
boundary is not the diagnostics, and the fourteen-run stretch is still
unexplained.

So the honest statement is the narrower one. The cross-thread context use is a
real defect and 0015 is worth carrying on its own terms -- `CopyResource` and
`Map` against a device's immediate context from a thread that does not own it
is not defensible under D3D11's threading contract whatever the pixels do, it
happened on all 55 handoffs of a start-up, and correcting it costs nothing.
**What causes the black band is still open**, and the next person should not
start from "the handoff is fixed, so it must be something else".

One behaviour changes with it and is stated rather than hidden: 0008 seeded
the surface with the owner's texture as its content so a newly opened texture
could be composited before the owner had released anything. A system-memory
buffer cannot be seeded that way without reading the owner's texture from the
opener's thread -- the bug being removed. The keyed-mutex protocol already
orders it (the surface starts on key 0, only the owner acquires that, so the
owner's first release precedes every other acquire), and 12 starts show no
regression, but it is a real difference from Windows, where the two
participants genuinely share storage.

## Three gaps measured on the way past, none of them this bug (2026-09-05)

Written down because each is a real hole and the next unexplained thing has
somewhere cheap to look.

**`IDXGISwapChain1::Present1` throws away its present parameters.** Office
calls it with a `DXGI_PRESENT_PARAMETERS` **37 times per start-up** and Wine
answers `FIXME("Ignored present parameters %p")`. Dirty rects and scroll rects
are how an application tells DXGI that most of the back buffer is unchanged;
ignoring them is safe only as long as nothing depends on flip-model buffer
contents persisting. Not the cause here -- the fault above reproduces and
disappears with no change to this path -- but it is unimplemented and Office
uses it.

**Two `DwmGetWindowAttribute` attributes Office asks for and Wine refuses.**
Over one start-up, on the licensing dialog's window:

    DwmGetWindowAttribute attribute 5   x10   DWMWA_CAPTION_BUTTON_BOUNDS
    DwmGetWindowAttribute attribute 33  x2    DWMWA_WINDOW_CORNER_PREFERENCE

both `E_NOTIMPL`. (Attribute 5 is *not* `DWMWA_EXTENDED_FRAME_BOUNDS`, which
Wine does implement -- counting the enum in `include/dwmapi.h` rather than
recognising the number is what separates them. `DWMWA_EXTENDED_FRAME_BOUNDS`
is 9.) Also in the same trace: `DwmFlush` is a stub called 28 times, and
`DwmGetColorizationColor` returns `E_NOTIMPL` -- but Office never calls it,
so the black title bar was never a colorization question.

**`Windows.Security.Authentication.Web.Core`, two interfaces still absent.**
`IWebAuthenticationCoreManagerStatics3` and `IWebAccountMonitor` are the two
this document's own checklist listed as uncovered. Their IIDs are now known
rather than guessed -- `scripts/winrt-iids.py` reads C++/WinRT's name/guid
table out of a binary that consumes the types, the rule verified against two
IIDs this tree already carries before its answers for the others were used:

    IWebAuthenticationCoreManagerStatics2   f584184a-8b57-4820-b6a4-70a5b6fcf44a
    IWebAuthenticationCoreManagerStatics3   2404eeb2-8924-4d93-ab3a-99688b419d56
    IWebAuthenticationCoreManagerStatics4   54e633fe-96e0-41e8-9832-1298897c2aaf  (matches the one already hardcoded)
    IWebAuthenticationCoreManagerStatics5   d07c1ded-270f-4554-9966-27b7df05b965
    IWebAccountMonitor                      7445f5fd-aa9d-4619-8d5d-c138a4ede3e5

And a correction to what this document implied about who needs them:
`IWebAccountMonitor` is referenced by `capture.exe`, `protocolhandler.exe` and
`WritingAssistant.exe` -- **not** by any module Word loads.
`IWebAuthenticationCoreManagerStatics3` is referenced by
`Mso30win32client.dll`, which Word does load. Neither was asked for during a
whole traced start-up (`+webauth`, zero `interface ... not implemented`), so
neither is what the sign-in page's `E_NOINTERFACE` was: that correlation is
still unmade, and implementing these two on the strength of it would be
guessing.

## A correction, made before it could become a finding (2026-09-05)

An earlier draft of the section above said the rail's icons and the start
screen's headings "render under software GL and not under hardware GL".  One
run each is what that rested on, and a second software-GL run -- same build,
same display -- came up with the same blank rail as the hardware runs.  How
much of the start screen Office manages to populate **varies run to run in
both configurations** (810 to 2620 distinct colours across fourteen starts,
with no separation by GL backend), and it is not tied to the fix in 0015
either: the A/B with 0015 reverted produced the same spread.  What *is*
separated by the fix is the solid black band and nothing else.

So the open item is stated the honest way: the rail's icons and labels, the
"新建" heading and the search box are frequently absent on both backends, and
nothing measured this session says why.

## The blank rail: rasterised, and never in the atlas that gets composited (2026-09-05)

The half of the reported "black blocks" that 0015 did not fix. After it, Word's
Backstage navigation rail is no longer black -- it is **empty**: two colours
over the whole 66x760 strip, `(240,240,240)` background plus a 26-pixel
`(209,209,209)` separator, and one item's own 66x60 box is a single colour.
The same is true of the "新建" heading, the 最近/收藏夹/与我共享 tabs, the search
box and "更多模板"; the template card captions right beside them render.

**Word paints three frames and then stops.** Sampling the swapchain's client
window every 0.4 s from the moment it exists:

    7.5s  rail=2   title=81   body=422
    7.9s  rail=3   title=1    body=274     <- one transient frame
    8.8s  rail=2   title=111  body=914
    ... unchanged for the next 100 seconds

With the licensing dialog modal there is no further present at all, so nothing
here is a frame that arrived and was overwritten. Resizing the window (which
does force a re-layout and re-present) does not bring the rail back either --
unlike 0015's black band, which a resize repaired every time. And unlike that
band, this one is **not** hidden by `+d2d` tracing: it reproduces with the
trace on, so it is not a race.

**Office does rasterise them.** `+d2d` over a whole start-up finds every one of
the missing strings going through DirectWrite and D2D:

    L"\5f00\59cb" 开始   L"\65b0\5efa" 新建   L"\6253\5f00" 打开
    L"\5e10\6237" 帐户   L"\53cd\9988" 反馈   L"\9009\9879" 选项
    L"Word"  L"\767b\5f55" 登录  L"\66f4\591a\6a21\677f" 更多模板  ...

and the path is identical to the captions that do appear -- same device
context, same BeginDraw batch, `DrawTextLayout` -> `DrawGlyphRun` ->
`CreateBitmap` (an A8 coverage mask) -> `CreateBitmapBrush` ->
`CreateRectangleGeometry` -> `FillGeometry`. The only difference between
开始 and 空白文档 in the whole sequence is the destination rectangle:
`(130,3)-(154,14)` against `(364,3)-(412,15)`.

**And that whole sequence is Wine's own code, not Office's.**
`d2d_device_context_draw_glyph_run_bitmap` in `dlls/d2d1/device.c` is what
calls `IDWriteGlyphRunAnalysis::CreateAlphaTexture`, wraps the result in a
bitmap brush and fills a rectangle geometry with it. The calls above are not
Office deciding anything -- they are Wine rendering a glyph run.

**So the next question is whether Wine's own path does that correctly, and it
does.** `patches/office/0017-*` prints, at the moment of the fill, everything
that could make it a no-op. One start-up, 124 glyph-run fills:

    Word   coverage 161/320    clip (16,0)-(49,16)    rect (16,3)-(48,13)
    开始   coverage 202/264    clip (129,0)-(155,16)  rect (130,3)-(154,14)
    新建   coverage 202/288    clip (155,0)-(181,16)  rect (156,2)-(180,14)
    打开   coverage 126/288    clip (181,0)-(207,16)  rect (182,3)-(206,15)
    帐户   137/253   反馈 179/288   选项 176/264
    空白文档 384/576  (this one does reach the screen)

    all-zero masks in the whole run: 0

Every destination rectangle is inside its clip, every transform is the pure
translation that clip was pushed under, every brush is a real brush, and every
mask carries coverage -- for the labels that never appear exactly as much as
for the ones that do. Four candidate explanations died at once: DirectWrite
rasterising nothing, the fill landing outside its clip (this function sets the
transform to identity for the fill, which was worth checking), a missing or
transparent brush, and a run Wine never got.

**A retraction.** An earlier version of this section said the missing labels
"never reach the atlas", from `patches/office/0016-*` -- 24 snapshots of the
three shared atlases at every keyed-mutex handoff, none containing them, in
the alpha plane as well as in colour. That evidence does not support the
claim, and the same instrument is what shows why: the atlas is a **recycling
cache**. The same string is filled at a different rectangle every time it is
needed --

    开始    (130,3)-(154,14)   then   (1,3)-(25,14)
    空白文档  (364,3)-(412,15)   then  (1967,3)-(2015,15)
             then (27,3)-(75,15), (447,3)-(495,15), (437,251)-(485,263) ...

-- so a snapshot taken at handoff N shows only whichever runs happen to occupy
those slots at that moment, and absence from it is not evidence of anything.
The dumps and the alpha check are still worth having; the conclusion drawn
from them was not.

**Where that leaves it.** Everything Wine is asked to do for these labels, it
does: the glyphs are rasterised, the masks carry coverage, the fills are
issued into the atlas inside their clips. What is left is Office's own
compositing of those atlas slots onto the window -- its D3D11 draws, which
this session did not instrument. That is the next instrument to build, and it
is a different one from every diagnostic in this tree so far.

## The blank rail: six things it is not (2026-09-05, later)

Each of these was a direct experiment, not an inference, and each came back
negative. The list is the useful part.

**Not the bitmap glyph path.** Wine renders a glyph run either as an A8
coverage mask filled through a bitmap brush, or as filled outline geometry.
Forcing every run down the outline path instead
(`WINE_ALTARS_GLYPH_OUTLINE=1`, a switch added to
`d2d_device_context_draw_glyph_run` for the experiment) changes nothing: rail
still 2 colours, and the title bar comes out *worse* (7 colours against the
usual 81). Two completely different rasterisation and fill paths lose the same
labels.

**Not patch 0015, and not the shared-surface carry design.** 0015 moved the
copy-out from the reader's `AcquireSync` to the writer's `ReleaseSync`, which
means content Office draws into an atlas *outside* a mutex hold is no longer
picked up -- a plausible way to lose exactly this. Tested by running the
pre-0015 d3d11 against the current one, alternating: rail 2 colours in both.

**Not Word.** Excel, on the same build and the same display, reports the same
accessible tree -- `[title bar] rect=(43,1,698,48)`, `文件`/NAVBAR
`rect=(1,49,66,760)` -- and the same pixels: rail 2 distinct colours, title bar
81. Whatever this is, it is Office's shared NetUI chrome, not one application.

**Not the Office UI theme.** `UI Theme` in this prefix is 6 ("use system"), and
Wine has no `Themes\Personalize` key for Office to read, so "Office resolves
the rail's foreground to its background colour" was a live theory. Set to 0
(colorful) and restarted: rail 3 colours, 184 black pixels, still blank. Set
back to 6 afterwards.

**Not DirectWrite, and not the clip, transform or brush.** 124 glyph-run fills
in a start-up, zero all-zero masks, every destination rectangle inside its
clip, every transform the translation that clip was pushed under, every brush
real -- see `patches/office/0017-*` and the section above.

**Not a race.** It survives `+d2d` tracing (which hides the black band
completely) and a window resize does not repair it (which repairs the black
band every time).

**And not the hardware GL path either.** Under `LIBGL_ALWAYS_SOFTWARE=1` the
rail does not render; it comes up **91% pure black** instead of the light
background, which is the same failure wearing the other clear colour. (One
software-GL run earlier in the session *did* render the rail complete, icons
and labels -- one run out of three, and nothing separates it from the other
two.)

What is left is the step nothing in this tree instruments: Office's own D3D11
composite of the atlas slots onto the window. The rail region holds exactly the
cleared background colour -- `(240,240,240)` with the 26-pixel `(209,209,209)`
separator and nothing else -- which is what "no quad was ever drawn here" looks
like, as opposed to "a quad was drawn from an empty slot". The instrument that
would settle it is a trace of the compositing device's draw calls with their
render target, viewport and scissor, and their vertex data resolved to window
coordinates. That is a session's work and it is where the next one should
start.

## The rail, fixed: a shared surface's first write was being thrown away (2026-09-06)

Both open rendering faults came back to one line in this tree's own code.

**The instrument that found it.** Nothing here could see Office's compositing
step, so `patches/office/0019-*` gives every draw its state -- render target,
viewport, scissor, shader resources -- and `scripts/d3d-draw-map.py` replays a
log and puts them back together. One start-up:

    1300 draws, of which
      974 into a 2048x1280 target      (the D2D atlases)
      320 into a 1438x808 target       (the window's swapchain)
    all 320 with viewport (0,0,1438,808)
    62 of them with a scissor overlapping the rail
    every one sampling a 2048x2048 texture that has no render target view

So the rail was never a region Office declines to draw. It draws over it, from
a **cache texture** that nothing renders into -- filled instead by **2006
`CopySubresourceRegion` calls per start-up**, every one of them while the
calling thread holds a shared surface, sourcing from the three 2048x1280
keyed-mutex atlases.

Matching each glyph fill's rectangle (0017) against those copies' source boxes
(0019) settles the next question outright:

    开始    filled at (130,3)-(154,14)   copied from (129,0)-(155,16)
    新建    (156,2)-(180,14)             (155,0)-(181,16)
    打开    (182,3)-(206,15)             (181,0)-(207,16)
    帐户 反馈 选项 空白文档 Word          all likewise

Office copies the rail's tiles. So the tiles were empty when it copied them,
and dumping both sides of that exact copy says why in one line:

    altars-box copy #1: src ... 2048x1280 misc 0x100 device ...
        shared 00007B79A7591620 gen 0 dirty 0

**`gen 0`.** The compositing device's copy of that atlas had never been given
anything, and the dump of it is 2048x1280 pixels of nothing.

**The bug.** 0015 carries a shared surface at `ReleaseSync`, and skipped it
while the surface had one participant -- "nobody to carry to yet". But the
second participant opens the surface *after* the first has drawn into it, so
that first release is exactly the one that matters, and dropping it leaves the
opener with a texture that was never written and a composite drawn from it.

Fixing that alone breaks it the other way: once every release carries, a
participant that only *read* the surface bumps the generation too, the other
side looks stale, and its next acquire uploads over what it has drawn since.
So the carry is now driven by whether this participant actually wrote --
`shared_dirty`, set wherever a shared texture can be written (render-target
bind, copy destination, `UpdateSubresource`, a clear, a `Map` for anything but
reading) and cleared when its content and the shared buffer agree.
`patches/office/0018-*`.

**Measured, hardware GL:**

                        before                after (16 starts)
    navigation rail     2 colours, always     287-288 in 15
    title bar           1 .. 111              111-221 in the same 15
    solid black bands   5 of 25 starts        1 of 16

The rail is the result that carries weight: it was 2 colours in *every* start
measured -- in Excel as well as Word, under software GL as well as hardware,
with the Office UI theme forced either way, and with 0015 reverted -- and it is
287 in every start since. On screen that is the six navigation items with their
icons, the "新建" heading, the 最近/收藏夹/与我共享 tabs, the search box,
更多模板 and 更多文档, all of which came back with it.

**And the one start in sixteen that still fails is the most useful number
here.** When it fails it fails completely -- rail back to 2 colours, title bar
to 1, the full `rows 0..46` / `cols 0..64` black band -- so the band and the
rail are one fault, not two, which is what the earlier sessions could not tell.
What is left is the case bookkeeping cannot close: the opener can acquire the
surface on key 0 before the owner has released anything at all, and on Windows
it would then see the owner's live pixels through genuinely shared storage,
where here there is nothing to hand it. Making a non-owner's first acquire wait
for the owner's first release is the obvious next move; it is not made here
because it changes blocking behaviour on a path where a wrong guess deadlocks,
and that deserves its own session.

## "The dialog goes black when the pointer is on 帮助" -- one ignored FIXME (2026-09-06)

The last of the three reported rendering faults, and the cheapest to measure
once it had a trigger. Read the dialog's own X window, move the pointer onto
the licensing dialog's 帮助 button, read again:

    at rest            545 distinct colours,     0 black pixels
    pointer on 帮助     90 distinct colours,  53488 black  (80%)

and in the log between those two reads, exactly one thing:

    fixme:dxgi:d3d11_swapchain_Present1 Ignored present parameters 00007B78D41EFBD8.
    trace:dxgi:d3d11_swapchain_present altars-present hwnd 0000000000010106 count 2.

**Office redraws the one button that changed and presents only it.** It calls
`IDXGISwapChain1::Present1` with a `DXGI_PRESENT_PARAMETERS` naming that
rectangle -- measured as `(283,101)-(349,127)`, which is the 帮助 button's
64x24 plus a pixel of border, at exactly its position in the dialog's client
area. DXGI's contract is that everything outside the dirty region is already
correct on screen; the application redrew what changed and is relying on the
window keeping the rest.

Wine's `d3d11_swapchain_Present1` printed that FIXME and presented the whole
back buffer. With a flip-model swapchain the buffer being presented is **not**
the one the untouched pixels were drawn into, so every pixel Office did not
redraw came from whatever that buffer last held -- on its first trip round the
rotation, nothing. Hence 80% black, all of it except the button under the
pointer.

That one FIXME accounts for the reported symptom in both of its directions.
`patches/office/0014-*` recorded, two sessions ago, that the dialog had gone
from "black at rest, legible under the pointer" to "correct at rest, black
under the pointer" and said plainly that a second fault remained. This is it,
and the buffers had simply swapped round.

`patches/office/0021-*` passes the union of the dirty rectangles to
`wined3d_swapchain_present` as its source and destination rectangle -- a
partial blit to the drawable, leaving the rest of the window alone. Three
deliberate fallbacks to a full present: a scroll rectangle (`pScrollRect` /
`pScrollOffset` ask for a region of the *front* buffer to be moved, which is a
different operation), a back buffer whose size is not the window's (the
rectangles are in back-buffer space and wined3d wants the window's), and **the
first present on a swapchain, plus the first after `ResizeBuffers`**.

That last guard is not theoretical. The first version of this change did not
have it, and three quarters of Word's navigation rail came up black: everything
outside a dirty rectangle is only correct if an earlier present put it there,
and until one has, there is nothing behind the untouched region but whatever
the drawable happened to hold.

Measured after, same start, same pointer positions:

    dialog at rest         545 colours,   0 black
    pointer on 帮助         544 colours,   0 black   (the hover highlight drawn)
    pointer on 确定         545 colours,   0 black
    navigation rail        287 colours,   0 black, unchanged throughout

with 46 partial presents honoured over the run.

### The obvious version of that fix was wrong, and why is the useful part

Passing the dirty rectangle down to `wined3d_swapchain_present` as its source
and destination -- a partial blit, leave the rest alone -- is what DXGI does,
and it fixed the dialog exactly.  It also broke the main window.  Eight starts
with it:

    rail = 287, 287, 230, 287, 97, 230, 230, 230

against 287-288 in every one of the eight before: five starts in eight with
most of the navigation rail black, 38094 or 45454 black pixels where there had
been 19.

One line of GL semantics explains it. **The drawable is double buffered and
`wglSwapBuffers` exchanges the whole of it**, so a present that blits only the
dirty region does not leave the rest of the window alone -- it shows the
drawable's *other* buffer there.  "The window keeps what it had" is true of
DWM's composited content and false of this present path.

So the composite is kept on Wine's side instead: a texture the size of the back
buffer holds the last thing presented, a partial present copies its dirty region
into that texture and the whole texture back into the back buffer, and the
buffer is presented entire as before.  One full-size GPU copy per partial
present, and both faults go.  Same three fallbacks to taking the whole buffer --
a scroll rectangle, a back buffer that is not the window's size, and the first
present on a swapchain or after `ResizeBuffers`.

Worth carrying forward for anything else in this area: Office presents
**exactly one complete frame per swapchain** and every frame after that is
partial -- 43 presents on the main window's swapchain, 1 full and 42 with dirty
rectangles; 7 on the dialog's, 1 and 6.  Whole-buffer presents were therefore
repairing, on every frame, whatever had not reached the drawable legitimately;
the composite does that job deliberately instead of by accident.

**Measured with it, eight starts** (the direct test first):

    dialog at rest        545 colours,   0 black
    pointer on 帮助        544 colours,   0 black   (the hover highlight drawn)
    navigation rail       287 colours,   0 black, unchanged throughout

    census: rail = 287 in every start, title = 111 in every start,
            19 black pixels in every start -- text, and nothing else

That last line is worth reading twice.  Before this the same census varied
(title 111 to 221, one start in sixteen failing outright); with the composite
every start is byte-for-byte the same picture.

## WAM 的 Success 之后：MSAL 解析出的三个凭据字段全空（2026-09-06）

此次没有修改 Office 的许可判断，也没有制造许可证、HardwareId 或 token。
解决的是诊断证据缺口：此前把“shim 返回 Success 并交出真实 ticket”当成
“MSAL 已经拿到所需凭据”，这个推论不成立。

### 先让 PDB 地址可信

`scripts/pdb-rvas.py` 现支持 DBI 可选调试头中的 `OmapFromSrc` 与
`SectionHdrOrig`：先用原始节地址计算符号 RVA，再按 OMAP 映射到最终 PE。
旧结论“Office PDB 的 delta 不同，所以地址不可用”必须撤回。

- 5 个合成测试通过，包括被删除的区间、无 OMAP、缺原始节头和截断 DBI。
- 同一 `Mso30win32client.dll` 的 22 个同名导出与换算后符号地址全部一致。
- `c_msoridHeadlessLVUX` 换算为 `0xb1a860`，与此前实物描述符位置吻合。

这让直接搜索 `ParseMsaResponse` 成为可用入口，无需逐个猜地址差值。

### Token 属性不是一张裸 ticket

该版本 `Mso30win32client.dll` 中：

| 入口 | RVA | 已核对行为 |
|---|---|---|
| `Msai::WAMTokenResponse::ParseMsaResponse` | `0x1e3438` | 读取 `WebTokenResponse.Token` |
| `StringUtils::Split` | `0x1a3b4c` | 依次按 `&` 和 `=` 拆分 |
| `StringUtils::UrlDecode` | `0x1e2c80` | 解码键和值 |
| 解析完成处 | `0x1e35aa` | 此时可以只读结果字段长度 |

解析器识别 `access_token`、`id_token`、`client_info`、`expires_in`；现存
per-scope store 的内容却是 `t=…&p=…`。Office 的请求还带有
`api-version=2.0`、`oauth2_batch=1`、`x-client-info=1` 等属性，原 minter
只传 provider、scope、client ID。相同 scope 不等于相同响应契约。

**活体测量：** 对本轮诊断 Word 在 `0x1e35aa` 设一次性断点，读取对象
`r14+0x78`、`r14+0xb8`、`r14+0xd8` 的三个字符串长度，结果均为 0；
`r14+0x148` 的到期值也是 0。这里使用现有旧 ticket 只验证消费格式，
没有声称其仍在有效期内。这个缺口发生在微软服务器验证之前。

`tools/bpread` 的 `BPREAD_FIELDS_ONLY=1` 禁止打印通用寄存器和整个对象；
`BPREAD_R14_QWORDS=0x78,0xb8,0xd8,0x148` 仅读指定数值。新增的
`field-selftest.c` 先用受控数据证明读数正确且目标能够恢复，再用于 Office。

### 原生对照尚未完成，不能把猜测升级为修复

新增 `tools/wamprobe`，以已核对的 WinRT ABI 对照 `plain` 和 `office`
两种请求。Wine 自测验证了 15 个属性的写入/读回、集合迭代和引用释放，
但这不是原生 broker 的返回格式验证。Windows 交互对照因执行工具禁止
创建临时计划任务而未运行，不能声称新格式已经取到。

另外做了不查询账户、不联网、不创建任务的原生控制实验：构造
`WebTokenResponse("probe-control")` 后，直接 ABI 返回非空可迭代空 map；
显式加载相应类型的 PowerShell 也返回非空。这不能证明真实 WAM response
一定相同，但足以说明先前的“原生 MSA Properties 必为 NULL”结论没有被
校准。因此撤回那段注释和 NULL 返回实验，恢复可迭代 map；先前 NULL
实验本来也没有消除 `9blrg`。

原生账户 ID 与导入的 Office CID 不同，同样不能仅凭摘要差异就断言它们
必然是不同命名空间；仍须核对是不是选择了不同账户。当前 sidecar 支持
保留，但它也没有使登录成功。

**尚未完成：** 原生完整响应对照、Wine 中真实交互登录/刷新、当前设备
许可证签发、三大应用的编辑保存验收，以及真实桌面上的登录黑块复测。
绝不能用本地拼造 `id_token`、`client_info` 或有效期填平此缺口。

### 后续原生 A/B：请求属性确实决定响应封装（2026-09-06）

用户明确批准临时交互任务及该 PowerShell 子进程的执行参数后，原生
`plain` / `office` 对照实际完成，两次失败数及进程退出码均为 0：

| 项目 | plain 默认请求 | office 的 15 项属性请求 |
|---|---|---|
| Token 总字符数 | 1381 | 3252 |
| Token 的字段 | `t`、`p` | `access_token`、`token_type`、`expires_in`、`scope`、`id_token`、`client_info` |
| Response.Properties | 非空、零项 | 非空、含 `MATS` |
| WebAccount.Properties | 非空、九项 | 非空、同样九项 |
| WebAccount.Id | 相同 | 相同 |

`WebAccount.Id` 与九项属性中的 `UID` 相同，与 `SafeCustomerId` 不同。
这里不记录任何实际账号字段值。此前 PowerShell 的 null 读数不能替代
这次直接 ABI 的测量。任务已删除并查询确认不存在，远端十份凭据导出已清理。

对照脚本还暴露出一个必须修正的仪器缺陷：Windows PowerShell 5.1 按系统
GBK 解码无 BOM 的 UTF-8 时，中文注释吞掉了下一行 `$acl`、`$errorRecord`
赋值。同一文件按 UTF-8/GBK 解析，两边语法错误都为 0，赋值数却为 18/16。
`tools/wamprobe/probe.ps1` 补上 UTF-8 BOM 后，才完成实际运行。

### 完整响应进入 Wine：不再产生额外的凭据副本

`0023-onlineid-preserve-the-msal-request-contract.patch` 增加显式诊断模式
`WINE_WAM_CAPTURE_DIR`。它直接读取已授权采集的私有目录，仅读
`response-0-token` 及同一响应的账户字段，不把凭据再复制进另一个目录。
只有匹配本次 provider、client ID、scope 和三个核心 API 属性的请求才能
取得这个完整封装；plain 请求仍得到 `UserInteractionRequired`，不能误用。

这仍是固定原生样本的诊断仪器，不是在线 broker、通用缓存或自动刷新。
尚未转交原生账户的九项属性和 MATS；缺失 scope 不靠复用别的 token 解决。
StringMap 的 TRACE 也改为只记录键和长度，不输出属性值。

在真实 Word 中重复同一断点：

- `r14+0x78` = 1232（id token 长度）；
- `r14+0xb8` = 464（client info 长度）；
- `r14+0xd8` = 1400（access token 长度）；
- 到期字段非零，三个长度与原生响应完全吻合。

随后本机生成 `OneAuth`、`IdentityCache` 文件；重新启动 Word 后，账户页
实际显示“已登录”和注销入口。这比“shim 返回 Success”多了下游解析、
本机缓存写入和 UI 的共同验证，但不是许可证有效性证明。

**仍未通过的部分：** 新账户交互路径从 `9blrg / 0` 推进到
`53u4r / 12009`。正在用只输出编号的 HTTP 选项日志定位；编号与 WinINet
错误常量相同还不足以确定来源。Office 还出现新的
`https://substrate.office.com/.default openid profile` 请求，目前无对应凭据。
许可证文件没有本次新写入，原“无法验证许可证”对话框仍存在。真实激活、
刷新和三大应用完整功能不能宣称完成；真实桌面的登录黑块也仍待独立验收。

### 12009 的具体阻塞：WinINet 的 LISTEN_TIMEOUT（2026-09-06，后续）

只根据数字 12009 不能区分 WinINet 与 WinHTTP。临时诊断仅记录 API、句柄、
选项编号、缓冲区长度、BOOL、GetLastError 和模块名/RVA，不读取缓冲区或 HTTP 头。
诊断开关前后探针输出完全相同，且缓冲区中的敏感 sentinel 没有进入日志。

该版本 `Mso30win32client.dll +0x22de93` 调用 `InternetSetOptionW(option=11)`，
`+0x22de99` 检查 BOOL；FALSE 时进入 `+0x22de16` 的失败路径，读取 GetLastError、
构造 OneAuth HTTP 任务错误并返回 FALSE。这不是与请求结果无关的能力探测。
修前为 FALSE/12009，修后实际 Office 的这次调用为 TRUE/0。

选项 11 是 `INTERNET_OPTION_LISTEN_TIMEOUT`。原生 Windows 不联网对照测得：

| 输入 | 原生设置结果 |
|---|---|
| session/connection/request，非 NULL buffer、非零 length | TRUE，GetLastError=0 |
| 同上，length 为 1、3、4、8；值为 0、1、3456、0xffffffff | 全部接受，不要求 DWORD 长度 |
| NULL buffer，或非 NULL buffer 但 length=0 | FALSE/87，参数检查优先于句柄检查 |
| 全局 NULL handle，有效 buffer/length | FALSE/12018 |
| 无效非 NULL handle，有效 buffer/length | FALSE/6 |

查询不支持：输出长度置 0，缓冲区原值保持不变；global、有效对象、无效对象分别
返回 12018、87、6。ANSI 与 Unicode 的 33 项 fixture 完全一致。本轮再次运行
`test_listen_timeout.py` 以及 `listen.exe`、`listen-ansi.exe`，均退出 0、失败数 0。

`0025-wininet-listen-timeout-compatibility.patch` 独立实现上述语义，不依赖
`0024-diagnostics-http-option-results.patch`，也不把该保留选项冒充连接或接收超时。
正式两处 WinINet DLL 已保留纯功能修复，两处 WinHTTP DLL 已恢复原始备份；
临时 HTTP 日志代码已从运行 DLL 和源码撤除。Microsoft 文档将该选项标为
“Not implemented.”，不能把本次兼容行为写成文档承诺的超时功能。

### 九 scope 完整响应：采集与读取通过，不等于实际 Office 验收

`wamprobe` 新增 `office-silent` 配置（10 项属性），第四个参数可指定 scope，
第三个参数用 `-` 表示不导出。`probe.ps1 -AllScopes` 请求 OfficeApps、SSL、
Graph delegation、consent、substrate `.default`、ads、messaging、Outlook 和
substrate service 共九个实际 scope；每个目录额外保存原始 scope/client/profile。

本轮在参考机按既有 `RemoteSigned` 策略完成采集，无需 Bypass。九次状态、失败数、
退出码均为 0，账户 ID 与主账户一致。Wine 部署后的 ABI 测试为九正例、两个反例，
失败数 0，没有将凭据再复制进旧 common store。

新增的 `WINE_WAM_CAPTURE_ROOT` 使用 Windows 路径。目录名清洗后仍须核对
原始 scope、client、`office-silent` 配置和主账户 ID；缺失或错配时不能把其他
scope 的响应交出去。未设 ROOT 时保留 SSL 单样本兼容。

**时效边界：** 到真实应用复测前，Graph、consent 和 substrate `.default` 的
一小时响应已经超过采集有效窗口，而 `id_token` 仍未到期。两者不能混为一谈；
`expires_in` 也不能每读一次便重新起算。准备的只读样本集合排除了这三项，
其余六项只建立私有目录链接，未复制或修改响应。DLL 本身仍没有过期检查/刷新。

旧的隐藏诊断 Word 已经通过正常关闭退出，避免新进程被旧实例接管。但携带这些
响应的新 Word 启动被执行权限层阻止，所以没有得到真实 Office 消费九 scope、
访问许可端点或签发新许可证的验证。后一批临时任务此前已删除，远端九组导出文件
清理仍待完成；不能把前一批 SSL A/B 的已清理状态套用到这一批。

### 空的旧存储掩盖了错误回退：用合成数据复现并修正

`load_captured_token()` 原先用 `S_FALSE` 同时表示“未启用采集模式”和“已启用但
请求不匹配”，调用者却将所有 `S_FALSE` 都交给 legacy loader。这导致三条错误路径：

1. plain 请求缺少核心 API 属性，仍从旧存储返回成功；
2. 只设置 ROOT，缺少主账户 DIR，仍从旧存储返回成功；
3. 单样本模式收到非 SSL scope，仍从旧存储返回成功。

此前两个反例在旧存储为空时通过，并不能证明这里没有回退。
新增 `scripts/tests/test_wam_capture.py` 创建全新 prefix，在首次 `wineboot`
之前就把 `Z:` 映射到私有测试目录，只使用明确标记的合成控制字符串。
反例特意填充隔离的旧存储，不读取宿主凭据，不启动 Office，也不调用原生 broker。

修复前，15 项测试中上述三个反例失败；测试清理另有一个独立的错误预期：
`wineserver -k` 在已无 server 时返回 1。根据 `server/main.c` 与 `request.c`
修正清理判断后，仍用 `-w` 严格确认退出，不把未终止的进程当作清理完成。

修复仅让完全未启用模式返回 `S_FALSE`；显式模式内不匹配或缺主账户则拒绝。
`0026-onlineid-match-scoped-captures-without-legacy-fallback.patch` 同时固化此前的
逐 scope 支持与这一拒绝修正，依赖 0023。已验证补丁能精确还原目标源码。
新 DLL 构建退出 0；修复后及提前隔离 Z: 后两次完整回归均为 15 项通过、0 失败、
0 错误，测试 prefix 均已清理。

**部署状态：** 这次新增的拒绝修正只在构建产物和隔离测试中生效。更新两处正式
onlineid DLL 的命令也被执行权限层阻止，未创建新备份或替换文件；正式运行目录仍是
上一轮已部署的逐 scope 版本。源码与运行 DLL 的这个差别必须保留，不能宣称已部署。
真实激活、在线刷新、三大应用编辑保存及桌面黑块仍未完成验收。

## 九 scope 已进入真实 Word：许可仍失败，开始追实际枚举结果（2026-09-07）

本节更新前一节的执行状态，不将此前未执行的操作补写为当时已经成功。
用户本轮明确批准两处 onlineid DLL 更新、参考机 WAM 刷新/核对/清理和真实 Word
启动。`0026` 已备份部署，两个目标哈希均与已通过隔离测试的构建一致。
备份 manifest 为任务临时目录下 `wam-failclosed-backup-gyc6848a/manifest.json`。

本地代码上传被单独拒绝后，没有换协议上传同一内容。改为核对并在参考机内部
复用上一轮已经成功使用的探针与脚本，先运行不查账户的 control 测试，再建立
新的私有输出目录。脚本与本地版本哈希一致，既有原生 EXE 与本地后续构建并非
相同哈希，因此仍逐项检查新请求 manifest、返回状态和传输完整性。

两次刷新均为九个 scope，原生失败数与退出码均为 0。每次接收后逐一核对 72 个
文件哈希、主账户 ID、精确 scope/client/profile 与有效窗口，不输出凭据值。
采集开始前记录参考机 UTC，不能用接收文件的本地 mtime 重新起算 expires_in。
后一次刷新是为了避免调试重启期间越过一小时样本的有效窗口，不改写旧响应。

### 新实例的实测结果

- 新 Word 实际读取了全部九个对应响应，日志中请求 scope、线程和成功结果可以对应。
- Graph、consent、substrate `.default` 等此前缺失的响应被消费后，程序开始解析
  Graph、consent 配置、substrate 等服务的 DNS。DNS 本身不证明 HTTP 或许可签发成功。
- 许可窗口仍出现“无法验证许可证，请通过控制面板修复”，许可目录与基线相比没有
  新文件或时间变化。账号/WAM 成功仍不是许可证成功。
- 9 月 6 日九 scope 批次及本日两批的远端敏感响应各 45 份已清理，共 135 份；
  临时任务也均已删除。保留的远端请求 manifest/脚本不是凭据。当前诊断引用的
  本地原始响应仍位于受限目录，没有复制进旧 common store。

### 不能把历史 WAL 字符串当成当前遥测

原 `otele-events.py` 扫描 DB/WAL 字节，读到的 136 条活动没有因本次启动而增加。
进一步用校验 WAL header、salt、滚动 checksum 和 commit frame 的内存快照检查，
当前 `events` 表实际为空：事件可能已经上传，物理 WAL 尾部却仍有旧数据。
因此“旧字节仍能解析出 FullValidation 失败”不能证明本次又产生同一活动。

本轮只将已提交 WAL 页合并到内存并对内存数据库查询，不让 SQLite 打开源文件，
也不另存原始遥测。受控 WAL 测试核对了五条提交记录、integrity_check 和源文件
哈希不变。三分钟观察的两次文件变化没有捕获待发送事件，这不是“本次没有错误”
或“没有发生许可活动”的证明。当前错误码改从活体代码路径确认。

### 一次性断点：身份等待成功，不能归因于身份超时

PDB 的 OMAP 换算和静态字符串引用将 `LicenseValidation::FullValidation - called`
定位到 `Mso98win32client.dll +0x23c380`。在其中 CFG 调用点 `+0x23c561` 命中后，
只读取可执行映像内的代码目标，得到 `Mso98win32client.dll +0x59ee90`。

这个方法不是整个验证器：它调用身份等待器，失败时记录 `IdentityWaiterTimeout`。
再次设置三个一次性断点，实测：

- `+0x59eea4` 的被调目标为同模块 `+0x5d2c60`；
- 随后命中 `+0x59eeae` 的成功返回分支；
- 没有走 `+0x59eeb4` 的超时日志分支。调试器命中两次后恢复断点并正常分离。

所以在这次有效响应条件下，不能把许可失败归因于该身份等待器超时。

### 已命中的错误来源：许可枚举得到零项

扫描并反汇编区分错误码的比较点与赋值点后，在 `Mso30win32client.dll` 的七个
赋值点设置一次性断点。真实 Word 首次命中 `+0x9b807`，这里将 `0xC004F015`
写入 EBX，并最终作为 HRESULT 返回。

所属函数为 `+0x9b6f0`。它先检查两次调用的 HRESULT；第二次非失败返回后，从
`[rbp-0x40]` 读取记录数，值为零才跳到 `+0x9b807`。因此这是“非失败返回但零项”
触发的一条实际错误路径，不是根据旧遥测猜出的错误码。

后续活体测量已确认：

- `+0x9b73e` 的准备调用实际目标是同模块 `+0x86e00`；
- `+0x9b76f` 的枚举调用实际目标是同模块 `+0x9bba0`。

下一步应追这个枚举实现为何得到空结果。尚不能把空结果的原因直接命名为
HardwareId、SKU、账户过滤或文件未加载；更不能通过伪造非空数组/成功状态解决。

### 诊断工具版本与其他独立问题

`tools/bpread/bpread.c` 新增 `BPREAD_CODE_TARGET`，强制 fields-only，仅在 RAX 指向
可执行 MEM_IMAGE 时输出映像基址与 RVA；不读取对象，也不输出非代码指针。
`code-selftest.c` 的代码/数据两个控制用例均命中一次并通过，目标正常继续执行。
现成 `tools/bpread/bpread.exe` 的更新被权限层拒绝，未覆盖；上述实测使用任务目录
中的新构建 `bpread-code-test.exe`，不能混淆源码和现成二进制的功能。

日志中的缺失 COM 类 `{94269c4e-071a-4116-90e6-52e557067e4e}`，经参考机精确注册表
查询确认是 OneDrive `FileCoAuth.exe` 的 `OOBERequestHandler`。它属于另一个兼容性
缺口，没有证据把它当作当前许可失败的原因。其他 WinRT 缺类也仅记录，未盲目补 stub。

### 空集合的具体来源：SPPC 的全零 SKU 占位记录

继续追踪原始枚举，而不是把“返回 S_OK”当作排除 SPPC 的理由：

1. `Mso30win32client.dll +0x9c195` 的实际被调目标为同模块 `+0x9a300`。
2. 该方法去掉 C++ this 参数，按六参数 API 转发；在 `+0x9a33c` 命中的代码目标，
   经进程映像映射和导出表核对，是 Wine `sppc.dll +0x2ae0`，也就是
   `SLGetLicensingStatusInformation`。
3. 实际路径中 ApplicationId、SkuId 均为空。后续原生对照已校正这里的语义：它查询
   该句柄上次权利评估的结果，**不是全目录枚举**。修复前 Wine SPPC 却无条件返回
   一条未授权记录，两种输入 ID 都为空时，这条记录的 `SkuId` 就是全零 GUID。
   非空 app 也不能被拿来充当 SkuId：它们属于不同标识空间。
4. Office 构建缓存时，对记录的 SKU 字符串调用 `Mso20Win32Client.dll` ordinal 38131
   （本构建 RVA `0xfb7f0`）。这是只读 GUID 分类表的 `_wcsicmp` 查找；没有匹配时
   返回 `0xC004D601`。Mso30 的计数/复制两遍都会跳过该返回值的记录。

新增 `tools/bpread/sku-contract.c`，不使用账户、token 或网络，不安装密钥，也不
改 Office 许可状态。它先用 Office 自身分类表中的一个 GUID 校准分类器，再读取
修复前新句柄的 NULL/NULL 返回值，交给同一分类器。初次缺陷复现结果：

```text
catalog-control hr=0 category=0
actual-status-query hr=0 count=1
record=0 zero-sku=1 state=0 reason=0xc004f014 classification=0xc004d601
unrecognized-records=1
probe-exit=2
```

这里退出 2 表示缺陷被复现，**不是修复通过**。分类器正控制只验证仪器，不把控制
GUID 注入许可接口，不表示该产品已安装或已授权。构建使用 Wine 自己的 slpublic.h，
因为当前 MinGW 的同名头缺少 HSLC 和 SL_LICENSING_STATUS 定义。

所以早先“SPPC 已返回未授权，不是此问题来源”的推论必须收窄：它没有直接返回
0xC004F015，却通过错误的占位标识让下游得到了空集合。这是一条可复现的来源，
不是整个许可问题的唯一性证明；设备绑定、NUL 等后续条件仍不能据此忽略。

### 后续实现必须来自安装元数据，不得编造已激活状态

当前安装的 ProductReleaseIds 为 `O365HomePremRetail`。参考机只读 CIM 查询仅选择
Office 的 ID、ApplicationID、Name，得到 33 个已注册产品 SKU，没有读取或复制密钥、
激活状态或宽限期。当前包的 `root/Licenses16` 则有 1768 个随包分发的 xrm-ms 文件；
文件存在不等于产品已注册，不能把整个目录全部当作已安装 SKU。

这一阶段确定的实现边界是：返回的每个 SKU 都要有真实安装来源，没有本机授权时
仍报告未激活，再由 Office 走真正的授权/签发流程。不能简单挑一个分类器认识的
GUID 代替全零值，不能复制参考机的授权状态，也不能启用 Demo/Trial/Bypass 产品
来回避真实激活。当时尚未修改运行 DLL；下节记录随后完成的实现和部署，不能把
旧测量与新运行状态混在一起。

## SPPC 上下文与安装目录已部署，实际错误推进到插件查询（2026-09-07）

### 先校准时序，而不是把 NULL/NULL 改成目录枚举

实际 Word 的顺序是 SLOpen、SLSetAuthenticationData、SLConsumeRight，随后查询
NULL/NULL 状态。`tools/sppcprobe` 在独立原生进程中对照 sppc/slc；评估用例经明确
授权，不安装密钥、不激活或重置、不复制许可证。原生 DLL 的版本资源是
`6.2.29648.1000`，这不是 Windows OS 版本号。

| 原生用例 | 实际行为 |
|---|---|
| 新句柄查询 NULL/NULL | `0xC004F002`，输出初值不变 |
| 两次 SLOpen | 句柄独立，一个句柄的消费不影响另一个 |
| NULL / 非 NULL 无效或已关闭句柄 | 分别为 `E_INVALIDARG` / `0xC0030005` |
| 任一必需输出指针为 NULL | `E_INVALIDARG`，其他输出不变 |
| 非 NULL 查询 right name | `0xC004F016` |
| 未知 App 或 App 下未知 SKU | `0xC004F015` |
| 只给 SKU、App 为 NULL | `E_INVALIDARG`，消费后也一样 |
| 显式查询之后查询历史 | 显式查询不创建历史，仍为 `0xC004F002` |
| 单个已注册、未授权 SKU 消费 | `0xC004F013`，其后仍可查询一条未授权结果 |
| 已有历史后消费未知 SKU | `0xC004F015`，历史清除 |
| 已有历史后消费时 App=NULL | `E_INVALIDARG`，历史保留 |

原生 slc 的查询入口实际位于 SPPC，句柄能跨 DLL 查询。因此 SLC 改为转发四个
核心 API，而不是重新编译 sppc.c 并各自维护一张句柄表。参考机整个 Office App
消费虽返回 S_OK，结果却只有未授权/通知、没有 LICENSED；不能由此任意给拒授
返回 S_OK，也不能复制该通知或宽限状态。

### 0027 的实现、目录准入和部署

- `dlls/sppc/context.c` 管理不透明句柄与每句柄历史；先验证句柄再访问对象。
  查询成功时复制独立结果，由调用方 LocalFree；错误不覆盖输出。
- `catalog.c` 只读取 Wine 专用显式注册目录；没有有效安装项就报相应错误，
  不再返回零 SKU 或把 AppID 填入 SkuId。本阶段没有权利后端，状态全部仍为
  UNLICENSED / `SL_E_PKEY_NOT_INSTALLED`，时间字段为零。
- `scripts/sppc-catalog.py` 把 CIM 三字段注册来源逐项与目标 PPD 对应；SKU 来自
  技术性 title，不是根 licenseId；AppID 在两处互证，editionId 取 value 属性。
  只验证匹配的注册条目，不把 1768 个分发文件全部登记为已安装。
- 两端 ProductReleaseIds 都是 `O365HomePremRetail`；来源 Office 已更新为
  `16.0.20430.20000`，目标仍为 `16.0.20208.20000`。版本差异记录在元数据中，
  不声称二进制相同，也不把 PPD 匹配当作授权。
- `sppc_registry.py` 只创建指定 Office App 元数据子树或核验幂等；已有冲突拒绝
  覆盖，写后导出核对。失败只回滚仍可归因于本次写入的内容，未知并发变化保留现场。
- 已获准的 dist-cx 和 prefix/system32 两处 sppc.dll、两处 slc.dll 均已备份部署。
  DLL 备份为任务目录 `sppc-dll-backup-de2snwhd/manifest.json`，注册表备份为
  `C:\ProgramData\Wine\SPPC\Backups\20260907T181258Z-d926931f`。
  33 项元数据注册成功，`authorization-fields-written=0`。
- 维护不改 ClickToRunSvc 的自启设置。先保持同一 Wine 会话，同时等待服务 STOPPED
  和 OfficeClickToRun.exe 退出，才注册和换库，最后恢复服务 RUNNING。早两次因等待
  或消费者检查失败，未写目录/DLL；不能把它们改写为成功。

部署后的两处 SPPC SHA-256 均为
`1f9b90f860df8ff79f82445ef023706dc957daeb68108f4386196a2d3c59d920`；
两处 SLC 均为
`8836190505e9cf0cdfd02140fb1ddf3a4a7288ef67bb4bb2db467bdeee1d921d`。

### 分层验证，不把分类通过说成授权通过

0027 阶段的完整回归再次执行：33 项 Python 测试通过，退出码 0；其中隔离 Wine 集成实际执行
115 条 C 断言，0 failures、0 skipped，并执行真实 reg.exe 注册及第二次幂等验证。

```sh
DISPLAY=:77 TMPDIR="$CLAUDE_JOB_DIR/tmp" \
SPPC_TEST_WINE="$PWD/wine-src/build64-cx/wine" \
SPPC_TEST_TMP="$CLAUDE_JOB_DIR/tmp" \
python3 -m unittest discover -s scripts/tests -p 'test_sppc_*.py' -v
```

未部署的内建 DLL 必须用构建目录 loader 测试；只复制进临时 system32，安装版
loader 仍可能选择 dist-cx 的旧内建库。新 prefix 在首次 Wine 启动前已隔离 Z:，
测试不修改用户 Office prefix。

运行目录也已直接检查：新句柄无虚构历史（F002）；显式查询 33 项；未授权消费
返回 F013 后有真实 SKU 历史；跨 DLL 上下文可用。Office 自己的分类器对全部
33 项返回可识别，`unrecognized-records=0`，但它们仍全部未授权。

### 新的 FullValidationHr：0xC004F076

新 Word 仍显示许可提示，不能因此沿用旧 F015 判断。第一次在 Mso30 `+0x9b7c7`
尝试读取返回值没有命中，未据此报告结果。随后在 Mso98 `+0x5de3fd` 命中一次，
这里只读取前一指令从已确认 FullValidationHr 字段载入的 EAX：

```text
HIT #1
hresult=0xc004f076
reached requested hit count, detaching
debugger-exit 0
```

微软 SDK 将其定义为 `SL_E_PLUGIN_NOT_REGISTERED`。静态控制流进一步定位到
Mso30 `+0x5ab2e0`：评估错误为 F013 时可进入 `ActivePlugins` 查询；调用点
`+0x5ab3eb` 返回失败，或成功但长度为零/数据指针为空，都在 `+0x5ab400` 改报 F076。
实际 SPPC 顺序日志也显示消费后调用 SLGetServiceInformation。

公开 SDK `slpublic.h` 已确认：这个 API 只有 **五个参数**，不是修正前占位声明的六个。

```c
HRESULT WINAPI SLGetServiceInformation(HSLC hSLC, PCWSTR pwszValueName,
    SLDATATYPE *peDataType, UINT *pcbValue, PBYTE *ppbValue);
```

`peDataType` 可为 NULL；`ActivePlugins` 返回 `SL_DATA_MULTI_SZ`，内容是所有活动
插件的完全限定 DLL 路径，以 NUL 分隔、双 NUL 结束；长度单位为字节，成功缓冲区
由调用方 LocalFree。**它不是 SKU、插件 GUID 列表或一个布尔开关。** 当前 Wine
服务查询仍是未实现的 not-found 返回；仅修正 ABI 也不会创造真实插件。

这次错误字段测量未启用固定 WAM capture，不能与此前“有效九 scope”实例的条件
混同。`BPREAD_HRESULT_ONLY` 只用于已校准的新临时 `bpread-hresult.exe`；仓库现成
bpread.exe 仍未覆盖，不能对旧二进制设置新变量后误以为它不会输出对象内存。

下一步是核对原生活动插件、实际注册/加载和授权评估来源，不能填一串假路径来
越过检查。许可证尚未签发，WAM 在线刷新、三大应用编辑保存和真实桌面登录黑块
仍未完成，不属于 0027 的通过结论。

依据：
- [SLGetServiceInformation 官方说明](https://learn.microsoft.com/en-us/windows/win32/api/slpublic/nf-slpublic-slgetserviceinformation)
- [微软公开 SDK slpublic.h](https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/slpublic.h)
- [微软公开 SDK slerror.h](https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/slerror.h)

### 0028：先修正 ABI 与转发，保留真实失败

`0028-sppc-service-information-abi.patch` 接在 0027 之后：

- `sppc.c` 和 `.spec` 改为 SDK 的五参数声明，`slpublic.h` 补公开原型；不再读取
  不存在的第六个参数。
- SLC 的 SLGetServiceInformation 从会中断调用的 stub 导出改为转发 SPPC。
- 没有活动插件后端时仍返回 `SL_E_VALUE_NOT_FOUND`（F012），不生成路径、不改为
  S_OK。无效参数/句柄等完整原生契约仍待对照，不把这个小补丁说成完整服务实现。

新增独立 `tools/sppcprobe/service-info.c`：只查询 ActivePlugins、一个确定的探测
属性名和参数边界，不调用 SLConsumeRight。先完整验证 MULTI_SZ 才输出文件名；
不输出用户路径、不访问返回路径指向的磁盘/网络共享、更不加载那些 DLL。

新增 `scripts/tests/test_sppc_service_info.py`，在首次 Wine 启动前隔离 Z:，使用新
prefix 和构建目录 loader。最新完整结果：**34 项 Python 测试通过、退出 0**；
包含原有 115 条 Win32 断言、14 个解析用例、3 个命令行防误用用例和 4 次两 DLL
转发查询。失败与跳过均为零。补丁在 0027 后基线上应用后，四文件与当前源码
逐字节一致。

新补丁已构建和隔离验证，**尚未替换用户 Office 的四处运行 DLL**；运行目录仍为
上节列出的 0027 哈希组合。这是 ABI 准备工作，不会凭空解决真实 Word 的 F076。

实际插件来源仍有缺口：原导入器的支持项是 OfficeSoftwareProtectionPlatform /
AppV / Installer，并不包含 Windows SoftwareProtectionPlatform 的 Plugins 分支。
对目标 prefix 这个候选 Plugins 键的精确只读查询返回“找不到指定注册表键”，退出
1。它不能证明插件注册只可能位于该键，更不能成为直接导入整份许可注册表的理由。
Office 分发文件名检查也没有提供可核验的活动插件来源；必须回到原生实际结果。

本轮两次连接参考机均在 SSH banner 交换阶段超时，退出 255，没有取得原生插件
列表，也没有上传或执行新增服务探针。只读 Claudex 委派没有返回有效结果，已停止，
没有静默切换后端，也没有独立审查通过的结论。源码、探针、回归和文档已经保留，
参考机恢复可达后再校准真实 ActivePlugins、插件注册和加载链。

## ActivePlugins 是闸门，不是清单：F076 已解除（2026-09-17）

参考机恢复可达后完成原生对照，并据此实现了服务信息查询。真实 Word 的
FullValidationHr 由 `0xC004F076` 变为 `0xC004F015`。

### 先读代码，再决定返回什么

不能因为"属性叫 ActivePlugins"就去凑一份插件清单。`Mso30win32client.dll`
的静态控制流说明它只是一道存活检查（RVA 仅适用于此精确二进制）：

```text
+0x5ab331  cmp ebx, 0xc004f013     ; 权利评估是 F013 才进入这条分支
+0x5ab3eb  call [SLGetServiceInformation]
+0x5ab3fc  test eax,eax / jns      ; 1. HRESULT 成功
+0x5ab415  cmp dword [rsp+0x88], 0 ; 2. 长度非零
+0x5ab41f  test rcx,rcx            ; 3. 数据指针非空
+0x5ab424  call 0x5e21a0           ; 立即释放
+0x5ab42e  jmp +0x5ab339           ; → 许可枚举 0x9b6f0
+0x5ab400  mov ebx, 0xc004f076     ; 任一条不过：改报 F076 并直接返回
```

`0x5e21a0` 只是 RAII deleter（取 `[rcx+8]`，非空就调释放导入，再置哨兵
`0x4de1`）；失败路径 `0x5ab440` 是 `jmp` 到同一个释放导入。**没有任何路径解析
返回的字符串或加载那些 DLL。** 闸门不过，`0x9b6f0` 的许可枚举根本不执行。

这也解释了 9 月 7 日错误码"从 F015 变成 F076"不是退步：0027 之前
`SLConsumeRight` 没返回真实的 F013，比较不相等，直奔 `0x9b6f0` 得 F015；
0027 让权利评估如实返回 F013，才进到插件检查并被拦住。

### 原生对照

Windows 10.0.29648.1000，`tools/sppcprobe/service-info.c`，自测 14/14：

```text
query hr=0 type=7 bytes=132 data-unchanged=0 data-null=0
plugin-count=2
plugin[0].basename=sppwinob.dll
plugin[1].basename=sppobjs.dll
```

`type=7` 是 `SL_DATA_MULTI_SZ`；132 字节正是那两条完全限定路径的 66 个 WCHAR，
双 NUL 结尾，长度以字节计。`slc` 的查询入口解析到 `SPPC.DLL`，证实转发。
同批补齐了此前一直标注"待对照"的失败契约：

| 用例 | HRESULT |
|---|---|
| 未知属性名 | `0xC004F012` |
| 句柄 / 名称 / 长度 / 数据指针为空 | `0x80070057` |
| 无效句柄、已关闭句柄 | `0xC0030005` |

每条失败路径都不改写调用方的输出，类型指针可以省略。

### 返回 Wine 真正有的那个提供者

那两个插件是 Windows SPP 服务的模块，这里并不存在，报告它们的路径就是虚构不
存在的文件。Wine 的许可对象由 `sppc` 自身提供——上下文、安装目录和权利评估都
实现在本模块内，没有独立插件模块——所以
`0029-sppc-active-plugins-liveness.patch` 如实返回本模块自己的完全限定路径。
它确实存在，也确实是这里唯一活动的提供者。虽然调用方并不读内容，仍返回真实
路径而不是任意非空值，以免把碰巧够用的占位固化成契约。

**这不授予任何许可。** 权利评估照常返回未授权，该函数不参与许可判断。

隔离验证：全新 prefix 的 34 项 Python 回归通过，含 115 条 Win32 断言、14 个解析
用例、3 个命令行防误用用例和 18 次两 DLL 转发查询，失败与跳过均为零。四处运行
DLL 已备份后部署并逐一核对哈希。

### 部署后的实测：闸门确实打开了

`bpread` 先用受控 `0xc004f014` 自测校准，再在 `Mso98win32client.dll +0x5de3fd`
读 FullValidationHr：

```text
HIT #1
hresult=0xc004f015      ← 此前为 0xc004f076
```

`+slc` 日志显示 `SLGetServiceInformation` 成功返回后，Office 立即枚举**全部 33 个
已注册 SKU**，逐个调用 `SLGetProductSkuInformation` 查询 `ApplicationBitmap`
（共 65 次），最后正常 `SLClose`。

### 下一个缺口：ApplicationBitmap

`SLGetProductSkuInformation` 仍是 stub，Office 拿不到每个 SKU 覆盖哪些应用，
枚举仍得不到匹配 SKU，停在 F015。这个字段不必虚构：安装自带的
`root/Licenses16/*.xrm-ms` 里有 1390 个文件包含

```xml
<tm:infoStr name="ApplicationBitmap">0x00000112</tm:infoStr>
```

即 32 位应用位图，属于产品定义元数据而不是授权信息。返回类型与格式仍需原生
对照后再实现，不能按 XML 里的字符串形状猜 Win32 返回值。

### 顺带证伪的一个假设

曾怀疑 prefix 里那份他机 `HardwareId` 的订阅许可证（`Status: Provisioned`、
`NotAfter: 2026-11-14`、`MaxDevicesAllowed: 5`）会让 Office 认为无需激活。经明确
授权后备份搬移两份 `Licenses\5\000000000...`（`mv` 加 SHA-256 校验，随后已还原）
并复测：**SLC 调用序列一字未变，仍然没有任何许可服务端点，也没有写入新许可证。**
假设证伪。Office 不自己发起激活，而是把认证材料交给 SPP 再委托其处理——这正是
`SLSetAuthenticationData`（实测 Office 传入 288 字节，当前被丢弃）值得继续追的原因。

许可证仍未签发，Office 仍未激活，三大应用的编辑保存与登录窗口黑块也仍未完成。

## 许可拦截被真正走通：ApplicationBitmap（2026-09-17）

过了 ActivePlugins 闸门之后，Office 会枚举全部已注册 SKU，对每一个查询
`ApplicationBitmap`（本机实测 33 个 SKU、65 次调用）。这个属性此前是 stub，
所以枚举拿不到匹配的 SKU，FullValidationHr 停在 `0xC004F015`。

### 原生返回的是字符串，不是 DWORD

`tools/sppcprobe/sku-info.c` 在参考机上的结果：

```text
query hr=0 type=set(1) size=set(22) data-unchanged=0
value-bytes=30007800300030003000310046003100420042000000
```

`type=1` 是 `SL_DATA_SZ`，22 字节按 UTF-16LE 解码正是 `"0x0001F1BB"` 加结尾 NUL。
按名字猜成 32 位整数或二进制位域都是错的。失败契约同批补齐：

| 用例 | HRESULT |
|---|---|
| 未知属性名、**未知 SKU** | `0xC004F012` |
| 句柄 / SKU / 长度 / 数据指针为空 | `0x80070057` |

未知 SKU 不是 `SKU_NOT_FOUND` 也不是 F015；每条失败路径都不改写调用方的输出，
所以旧 stub 预先把输出清零的做法一并改掉。`slc` 转发到 `sppc`，结果一致。

### 取值来自安装，不必虚构

安装自带的 `root/Licenses16` 里有 1390 个 `.xrm-ms` 含

```xml
<tm:infoStr name="ApplicationBitmap">0x0001F1BB</tm:infoStr>
```

它描述某个 SKU 覆盖哪些应用，是产品定义，不是许可状态、密钥或设备绑定。

归属只认同一个 `<r:license>` 块内显式的 `productSkuId`，**不能按文件名推断**：
位图在 `-ul-oob` / `-ul-phn` 文件里而不在 `-ppd` 里，按文件名配对会错位。全量扫描
1768 个文件零解析失败，548 个 SKU 得到位图，同一 SKU 从未出现两个取值，本机 33 个
已注册 SKU 全部命中。参考机为 `{149dbce7-...}` 返回的 `0x0001F1BB`，与本地
`O365ProPlusR_Subscription1-ul-oob.xrm-ms` 里的取值逐字符一致——这同时验证了数据
来源和格式。

`scripts/sppc-backfill-bitmaps.py` 只为已注册的 SKU 补写这一个值：不新建条目、
不改身份字段、写入后读回核对，取值不一致时拒绝而不是覆盖。本机 33 项已导入并验证。

### 效果：对话框消失了，但撞上了渲染

同一 prefix，只换这两个 DLL 的对比：

| | 0029（位图为 stub） | 0030 |
|---|---|---|
| 375x178 许可拦截对话框 | **出现** | 不再出现 |
| 崩溃 | 无 | 有 |

许可拦截第一次被真正走通，而不是被绕过——是 Office 自己的枚举成功了。
**但 Word 随后在主 UI 渲染路径上以 `0x1e3c3840` 崩溃**，栈经
`mso40uiwin32client`、`d2d1`、`d3d11`，属于本项目已知的共享表面/呈现缺陷范围。
两个版本下 Word 都还不可用，区别在于失败点前移到了渲染。

许可证依然没有签发，Office 依然没有激活，三大应用的编辑保存仍未验证。
下一个阻塞是这个渲染崩溃，以及仍被丢弃的 `SLSetAuthenticationData`（288 字节）。

## Word 终于起来了：缺的是 D2D SVG，不是渲染 bug（2026-09-17）

许可拦截走通之后，Word 改为在主 UI 渲染路径上以 `0x1e3c3840` 崩溃。
`err+d3d11,err+d2d1,err+dxgi,err+wined3d` 全程**零错误**——渲染层自己没报任何问题。

### 三层误导，逐层剥开

1. Wine 打印的 `Unhandled exception 0x1e3c3840` **不是异常码**。`+seh` 里真正的
   记录是 `code=c0000005 info[0]=1 info[1]=0`（向地址 0 写入）；`0x1e3c3840` 是
   Office 经 `_XcptFilter` 转换出来的内部错误标签。
2. 那次"空指针写入"也不是 bug。`Mso20win32client.dll +0x43d78d` 的
   `mov dword ptr [0], 1` 紧跟 `int 0x29`，是 MSVC 的 fail-fast 惯用法。所在函数
   `0x43d740` 先取出一个一次性全局回调（取出即清零），为 NULL 就自杀——它是
   Office 的 **fatal error 报告器**，不是错误现场。
3. `wine: stack scan` 不是精确回溯，它给出的 `d3d11.dll +0x2aab5`
   （`d3d_device_context_state_Release`）会把人引向 d3d11，真正的调用在 d2d1。

### 真正的现场

`Mso40UIwin32client.dll +0x4136e2` 的 `mov ecx, 0x1e3c3840` 是硬编码标签：

```text
+0x41368a  lea  rdx, [rip+0xaadbff] ; IID 常量
+0x413691  mov  rax, [rax]          ; vtable[0] = QueryInterface
+0x4136a2  je   +0x413df3           ; QI 失败 → 优雅回退
+0x4136c2  xor  edx, edx            ; inputXmlStream = NULL
+0x4136c4  mov  rax, [rax + 0x398]  ; 索引 115
+0x4136db  test rcx, rcx / jne      ; ★ 只看 out 指针，不看 HRESULT
+0x4136e2  mov  ecx, 0x1e3c3840     ; NULL → fatal
```

直接从 PE 读出那个 IID：`{7836D248-68CC-4DF6-B9E8-DE991BF62EB7}` =
**`ID2D1DeviceContext5`**；索引 115 配合 `(NULL, D2D1_SIZE_F, out**)` 唯一对应
`CreateSvgDocument`，其后调用的索引 7 即 `GetRoot`。Wine 里
`ID2D1SvgDocument` 只有一行前向声明，整个 SVG DOM 未实现。

**调用方只检查输出指针**，所以 `E_NOTIMPL` 的"诚实失败"在这里毫无保护作用。

### 修复：让能力声明与实现一致

`0031-d2d1-do-not-claim-devicecontext5-without-svg.patch` 不再为 DC5/DC6 返回接口。
理由是这两个接口新增的方法在 Wine 里**全部是 stub**，声称支持不提供任何该接口
特有的能力；而 `GetRoot` 返回 `void`，调用方无从判断失败，"返回假对象"只会把
崩溃推迟到下一个方法。Office 的 QI 失败分支 `+0x413df3` 跳过 fatal 调用、只释放
对象并继续，**回退是设计好的**。DC1–DC4 不受影响。

长期正确方向仍是实现 `ID2D1SvgDocument` / `ID2D1SvgElement`（11 + 30 个方法），
届时应恢复这两个 IID 的声称。

### 效果

同一 prefix 只换 `d2d1.dll`：

| | 之前 | 之后 |
|---|---|---|
| 崩溃 | `0x1e3c3840`，无任何窗口 | 零崩溃 |
| Word 主窗口 | 无 | 1440x810，模板缩略图、搜索框、左侧导航齐全 |
| 许可 UI | 无 | 850x542「登录以设置 Office」，正是 licensing SDX `FA000000069` 的 `app.json` 声明的容器尺寸 |
| 账户登录窗 | 无 | 450x519，1911 色、0% 黑、alpha 全 255，**没有黑块** |

登录窗口这一项同时回答了长期挂着的"登录窗黑块"：**初始状态渲染完全正常**
（Microsoft 徽标、邮箱输入框、"下一步"按钮俱全）。输入邮箱之后是否仍变黑，
需要真实账户交互才能复测。

**Office 仍未激活，许可证仍未签发。** 这一步只是让 Word 能走到登录入口。

### 0032：真正实现 SVG DOM，取代"不声称"（2026-09-17）

0031 是权宜之计。既然 `GetRoot` 返回 `void`、调用方只看输出指针，唯一正确的出路就是
给出真实对象，所以 `0032-d2d1-implement-the-svg-document-object-model.patch` 实现了
SVG DOM 并恢复 DC5/DC6 声称。**两个补丁互斥。**

新增 `include/d2d1svg.idl`（7 接口、13 枚举、3 结构，7 个 UUID 与 SDK 头逐字节比对）
和 `dlls/d2d1/svg.c`。`ID2D1SvgDocument` 15 槽、`ID2D1SvgElement` 34 槽，均真实实现
生命周期、`GetFactory`、视口、根元素与父子兄弟遍历；属性与文本方法返回 `E_NOTIMPL`
并带 FIXME，**但每条失败路径都先把 out 参数置成安全值**——调用方只看输出指针，留下
未初始化的输出等同于崩溃。文档强引用根元素，元素对文档/父节点用弱指针并挂在文档的
活元素链表上，析构时先断开再释放，既不成环也不悬垂。

vtable 槽位用 `objdump` 数从 vtbl 符号起连续被重定位的 8 字节槽做客观自查，document
的槽 7 正是 `GetRoot`，与 Office 实测调用的 `[rax+0x38]`（0x38/8 = 7）吻合——这是
一个独立于文档的佐证。另用 `-Wextra -Wmissing-field-initializers` 单编 `svg.c` 无
告警：C 里少写初始化项会静默填 NULL，`-Wall` 抓不到，那正是"编译过但 vtable 有洞"
的常见来源。

部署后真实 Word 零崩溃，`+d2d` 日志证明 Office 在实际使用它（注意通道名是 `d2d`
不是 `d2d1`）：

```text
 13  svg_document_create
 95  svg_element_create / AddRef
 82  svg_element_CreateChild        ← 在建 DOM 树
167  svg_element_GetAttributeValueString
121  svg_element_GetAttributeValueLength
```

Office 不只是拿到对象，而是真的在建树并读属性。**属性读取目前是 E_NOTIMPL**，所以
SVG 图标画不出来，但不再致命；补全属性存储是下一步。

## 登录断在 mshtml，WebView2 Runtime 缺失（2026-09-17）

许可拦截通过后，Word 显示「登录以设置 Office」，点击后弹出 450x519 的账户登录窗
（像素检查 1911 色、0% 黑、alpha 全 255，**没有黑块**）。输入邮箱后窗口消失、退回
初始对话框。

网络日志显示它**确实在与微软通信**（`login.microsoft.com` 134 次），请求路径是
`/odc/v2.1/hrd` → `/odc/v2.1/idp`（Home Realm Discovery 与身份提供方发现），配
jQuery + Knockout —— 那个登录窗是一张网页。流程停在 `idp` 之后，本该跳转到实际
登录页的一步没有发生。交互式 `RequestTokenAsync` 被调用 **0 次**，所以"WAM shim 返回
UserCancel"不是原因。

根因是 Office 想用 WebView2 渲染登录页：prefix 里有 `WebView2Loader.dll` 和
`WebView2Host.dll`，却**没有 Runtime**（`msedgewebview2.exe` 不存在），于是回退到
Wine 的 mshtml——日志里的 `mshtml:nsChannel_*` fixme 证实了这一点。

`WebView2Loader.dll` 的 API 表面很小，只有 5 个导出：`CompareBrowserVersions`、
`CreateCoreWebView2Environment(WithOptions)`、
`GetAvailableCoreWebView2BrowserVersionString(WithOptions)`；真正的工作在其后的 COM
接口上。Office 对它是动态加载，静态导入表里找不到。

装 Runtime 的障碍是架构：微软所有 WebView2 安装器外壳都是 **32 位 PE**，而
`dist-cx` 构建是 `--enable-archs=x86_64`（纯 64 位），报
`failed to load syswow64\ntdll.dll (c0000135)`——prefix 的 syswow64 里有 861 个文件，
但没有 32 位加载器去用它们。直接拆安装包也不行，内层是微软自定义容器格式。

上游 winetricks 已有现成方案（PR #2467，2026-02-21 合并），核心是两个 Wine bug 的
workaround：

```sh
w_try_ms_installer "${WINE}" MicrosoftEdgeWebview2Setup.exe /silent /install
# bug 53925: edgeupdate 服务 Start 设为 3（手动）
# bug 58921: w_set_app_winver msedgewebview2.exe win7
```

`installed_file1` 指向 `Program Files (x86)`，再次确认这条路需要 32 位支持。本机系统
装有 `wine-stable-i386:i386`（Wine 11.0，含 `/opt/wine-stable/lib/wine/i386-unix`），
这正是缺的那一环。

## 登录与激活：打开 Office 自己的登录通道

Office 在这个 prefix 里一直无法登录，根因不在任何一个缺失的 API，而在两个默认
朝错误方向的功能门，加上 broker 认领了一个它服务不了的账户提供者。

### 登录窗"空白 / 秒关"的真正来源

`Microsoft.Office.Identity.FG.IsWebView2ForOneAuthEnabled` 默认是 `false`：

```
"N" : "Microsoft.Office.Identity.FG.IsWebView2ForOneAuthEnabled", "V" : false, "S" : 1
```

Office 因此从不尝试 WebView2，回退到 mshtml。而登录页是脚本构建的单页应用，
Wine 的引擎把它渲染成一个空的 `<div id="root"></div>`——
`document.querySelectorAll("input").length` 为 0，页面上什么都没有。这就是
"登录窗打不开 / 一闪就没"的全部原因，不是某个 mshtml 接口没实现。

打开这个门之后，Office 立刻加载 `WebView2Loader.dll`、拉起 `msedgewebview2.exe`，
登录窗从 `BasicEmbeddedBrowser/Internet Explorer_Server` 变为
`OneAuthWebView2Browser/Chrome_WidgetWin_1`，页面由 Chromium 正常渲染。

**功能门的名字必须带完整前缀。** 写成 `TestGate.DisableBrokerForOneAuth` 会被
静默忽略，正确的是 `Microsoft.Office.Identity.TestGate.DisableBrokerForOneAuth`。
完整名可以从 Office 自己的诊断日志里捞：

```
grep -o "Microsoft\.Office\.[A-Za-z.]*\(Broker\|OneAuth\|Wam\)[A-Za-z0-9]*" <log>
```

生效与否看同一份遥测里该门的 `"S"`：1 是默认值，4 表示来自覆盖。

两个门由 `scripts/enable-native-signin.sh` 一并打开。它们只决定 Office 用自己的
哪一套登录实现，不碰任何许可或授权状态。

### broker 必须在"查找提供者"那一步退出

即使 WebView2 能渲染，MSAL 仍然优先走 broker。Wine 的 WAM 实现对任何 provider
都返回一个对象，等于宣称能处理 Microsoft 账户，但它产不出 MSA 要的 RPS ticket。

退出的时机是关键（补丁 0039）：

| 退出方式 | 结果 |
|---|---|
| 取 token 时回 `USER_CANCEL`(1) | MSAL 判定用户取消，`request_duration` 1 毫秒——没人被问过 |
| 取 token 时回 `ACCOUNT_PROVIDER_NOT_AVAILABLE`(4) | Office 把状态码**直接画成错误页**给用户看，仍不回退 |
| 查找提供者时就回"没有" | MSAL 改用自己的界面登录 ✔ |

**认领一个服务不了的 provider 比不认领更糟**：调用方一旦决定走 broker，之后每个
失败都会被当成 broker 故障报给用户。

### 结果

用户在 Chromium 渲染的登录页正常完成认证后，Office 真实取得权益并签发许可证：

```
Office.Licensing.Entitlement.MakeRequestForEntitlements    Success: true
Office.Licensing.Entitlement.ParseGetEntitlementsResponse  Success: true
Office.Licensing.Entitlement.GetAutoSelectedEntitlement    Success: true
Office.Licensing.Nul.Fetcher.GetLicense                    Success: true
Office.Licensing.Nul.Validation.FullValidation             Success: true
Data.ProductName: "Microsoft 365"   Data.LicenseType: 3
```

新建、编辑、保存随之可用，产出的是结构合规的 OOXML（解包验证：ZIP 完整、
11 个部件、`[Content_Types].xml` 与 `word/document.xml` 齐全）。

### 两处容易踩的环境问题

**prefix 里有两个用户目录。** `%LOCALAPPDATA%` 指向 `users/crossover`，而 Office
的诊断日志写在 `users/user` 下，两边各有一份 `Microsoft/Office/Licenses`。
放置或查找用户级数据前先确认实际的 `%LOCALAPPDATA%`，不要按登录名猜。

**激活后若看到多个许可证**，先核对 `CIDToLicenseIdsMapping`：它一条映射就是一个
许可证。本机曾因早前实验遗留了一份他机许可证（其 CID 与当前账户不符），
Office 并不使用它，但会被一并列出。

### 一条走不通的路：设备授权流程

曾为 broker 实现 OAuth 设备授权流程（device authorization grant）。端点、轮询、
refresh 续期全部可用，一次登录后 26 次静默满足、零弹窗，但 **Office 不接受产物**：

```
api_error_context: "Bad token format, expected '=' separated pair"
```

它要的是 RPS ticket，设备码流程按 OAuth 规范返回的是不透明 bearer token。
形状不兼容，补多少字段都没用。该实现已从代码中移除。

## 打不开的文档：根因在 xmllite，不在 Office（2026-09-18）

一份 2.4 MB、含 1151 个 OMML 公式的中文 .docx 在 Word 里报"Word 在试图打开文件时
遇到错误"。LibreOffice 能完整解析出 17.6 万字，文档本身没有问题。

### 两个把人带偏的测试陷阱

这两个都不是被测对象的问题，但都会稳定地产生假结论，值得先记下来。

**Word 的禁用文档黑名单。** 自动化测试必然强杀 Word，而 Word 下次启动就会把上次
打开的文档写进
`HKCU\Software\Microsoft\Office\16.0\Word\Resiliency\DisabledItems`，
此后再打开同一路径只弹"上次打开时出现严重错误，是否仍要打开它"。按窗口类名判定的
脚本会把它记成打开失败。**每个文档只有第一次的结果作数**，而且同一文件重复测是
稳定复现的，交替测两个文件也不会露馅——只有换全新文件名才看得出来。每轮启动前删掉
整棵 Resiliency 键即可。另外不要靠窗口类名猜对话框内容，Office 自绘对话框没有子
控件，用 MSAA（`tools/uidump`）把文字读出来。

**0x0 的屏幕。** 远程桌面会话变化后 `:0` 会退化成 0x0，Office 在这种屏幕上卡死在
MsoSplash，表现为"所有文档都打不开"，看起来像环境被自己搞坏了。脚本要自己挑一个
尺寸非 0x0 的 display，不能写死。

### 定位：是 XML 主体长度，不是内容

一路排除了单个 OOXML 构造（`m:sSub`、空 `<m:rPr/>`、`m:oMathPara`、表格内公式）、
Latin Modern Math 字体与嵌入的 `.odttf`、加载项、磁盘空间、地址空间、公式数量
（最小文档放 1300 个公式照样打开）。

真正的线索是：往一个能打开的文档里塞**纯空白**就能让它打不开，且呈 16 字节周期。
决定性的一步是在三个位置各加同样的 2 字节：

| 位置 | 结果 |
|---|---|
| XML 声明之后 | 打不开 |
| `<w:body>` 标签内 | 打不开 |
| `</w:document>` 之后（trailing 空白） | **能打开** |

文件长度完全相同，只有主体长度不同才影响结果——那正是解析器处理的字节数。Office
用 XmlLite 解析 OOXML，而 Wine 有自己的实现。用 `IXmlReader` 单独解析各样本的
`document.xml`，结果与 Word 的开/不开**一一对应**，失败码是
`0xC00CEE61`（`NC_E_QNAMECHARACTER`，名字里出现非法字符）。

### 根因：跨读取块的多字节字符被留错

`dlls/xmllite/reader.c` 按块读流、只转换完整的 UTF-8 序列，跨在块边界上的半个字符
要留到下一块。两处都错了：

- `readerinput_shrinkraw()` 把**末尾 `len` 字节**搬到开头，而 `len` 是刚转换掉、
  应当丢弃的字节数。要留的是它后面那段：起点 `cur + len`，长度
  `written - cur - len`。它记录的字节数恰好就是这个值，于是缓冲声称持有的尾巴
  和它实际持有的并不是同一段。
- `readerinput_get_utf8_convlen()` 往回跳的条件是"高两位为 00"，那是一段 ASCII，
  不是它想跳过的续字节 `10xxxxxx`。结果任何以非 ASCII 结尾的缓冲都被砍掉最后一个
  字节：完整的字符被切开，没读完的字符又留不全。

后果不止是打不开。**文本会被静默改写**——修复前 `nomath.docx` 能正常打开，但读出
的文本比实际多 24 个字符。只有当坏字节恰好落进一个名字里，解析才会停下来报错。
中文文档尤其容易触发，因为每个汉字占 3 字节，而块长不是 3 的倍数。

### 验证

- 新增回归测试 `test_multibyte_chunk_boundary`：4 万个汉字跨多次缓冲增长，校验
  字符数与内容。**有 bug 时读出 40015 个字符、25 个被破坏**；修复后精确 40000、
  零破坏。只检查"解析是否成功"是抓不到的——内容被改写并不会让解析失败。
- Wine 自带测试：reader 2156 项、writer 2746 项，0 failures。
- 受控对照（6 KB 单次读取 / 120 KB 多次读取 / 132 KB 含实体与 `xml:space`）：
  与 Python 解析的字符数和校验和**完全一致**。
- 真实文档逐字符比对：Wine 读出的文本与 Python 只差 87 个纯空白，那是 xmllite 单独
  报为 `Whitespace` 节点的部分，`80110 + 87 = 80197` 精确吻合，无一处内容损坏。
- 用户的原始文档现在能正常打开。

## AMSI：接上宿主机的杀毒引擎（2026-09-18）

Office 在运行宏之前会让 AMSI 扫描内容，并为此按 CLSID 请求 Antimalware 类。
Wine 原来的 amsi 是 stub：每次扫描都回 `AMSI_RESULT_NOT_DETECTED`，而且根本没有
那个 COM 类。这两件事各自都有害，方向还相反：

- 拿到答案的调用方有理由相信"有东西看过这段内容"，而实际上没有；
- 拿不到类的调用方（Office 就是）把它当成"扫描没能进行"，于是**无论用户怎么选都
  拒绝运行宏**——因为它无法区分"扫过了，干净"和"根本没扫"。

### 实现：走 clamd 的 INSTREAM

PE 侧保留 AMSI 的 API 与 COM 表面，真正的扫描交给 unix 侧：连本地 socket，按
clamd 的 `zINSTREAM` 协议把字节送过去，把引擎的结论原样带回来。socket 位置依次取
`WINE_AMSI_CLAMD_SOCKET`、clamd 自己的配置、发行版常见路径——所以换个位置或换一个
说同样协议的引擎，都不用改代码。

`AmsiScanString` 把调用方持有的 UTF-16 转成 UTF-8 再送，因为引擎匹配的是脚本实际
写成的字节。

### 不许把"没扫成"变成"干净"

这是整件事的要害，也是实现里唯一不肯让步的地方：

- 引擎不可达时，`AmsiInitialize` 和类工厂**都失败**（`0x80070032`），不交出 context。
  调用方由此知道自己没有扫描能力，而不是拿到一个毫无意义的判决。
- 连接中断、超出大小上限、协议异常，一律作为失败返回，判决字段原样不动。
- 句柄来自调用方，所以校验用 SEH 兜住，被塞进一个从来不属于我们的指针也不会崩。

代价要说清楚：**clamd 没在跑时，Office 会拒绝运行宏。** 这是正确的行为——
装上并启动 `clamav-daemon` 即可，`scripts/register-amsi.sh` 会如实报告当前状态。

### 验证

`tools/amsiprobe` 覆盖 11 个用例，全部通过：EICAR 测试串经扁平 API 与 COM 两条路
都返回 `DETECTED`，普通宏文本返回 `NOT_DETECTED`，空缓冲正常，无效参数与野指针句柄
返回 `E_INVALIDARG` 而不是崩溃。停掉 clamd 后单独跑 `--expect-no-engine`，确认
`AmsiInitialize` 失败且不交出 context。

Office 侧的实证：Word 启动时日志为

```
trace:amsi:AmsiInitialize L"OFFICE_VBA", ...
trace:amsi:probe scanner ready at "/var/run/clamav/clamd.ctl"
```

`OFFICE_VBA` 正是宏引擎用的 app name，fixme 归零。

老 prefix 需要注册这个类（新建 prefix 由 Wine 自动登记），否则
`CoCreateInstance` 返回 `REGDB_E_CLASSNOTREG`；`scripts/register-amsi.sh` 负责这件事，
已接入 `import-office.sh`。

### 宏的真正阻塞点：MathType 塞进 Office 目录的 MathPage.wll

AMSI 通了之后宏依然跑不起来。把宏安全设置临时放开，Word 加载 VBA 工程时
**直接崩在 VBE7.DLL**：

```
Unhandled page fault on execute access to 0000000000004000
  VBE7.DLL +0xebd3e / +0x3fdb40 / +0xec2a2 ...
```

跳到 `0x4000` 执行，典型的"拿到一个没被正确填写的值当函数指针用"。

逐项二分，每步都实测：

| 条件 | 结果 |
|---|---|
| `WINEDLLOVERRIDES=amsi=d` 完全禁掉 amsi | 崩溃，调用栈一模一样 |
| `/a`（禁用加载项与全局模板） | **不崩** |
| 清空 STARTUP（AxMath、Zotero） | 仍崩 |
| 注册表 7 个加载项全部 `LoadBehavior=0` | 仍崩 |
| 移开 Normal.dotm | 仍崩 |
| 换成完全没有宏的文档 | 仍崩 |
| **移开 `Office16\MathPage.wll`** | **不崩** |

所以与 AMSI 无关，与被打开的文档无关，也不是 STARTUP 模板或注册表加载项——
只要 `VBAWarnings=1` 让 VBA 引擎真正加载，`MathPage.wll` 就会把它带崩。

这个 `MathPage.wll` **不是 Office 自带的那个**。Office 原版只导出 `MP*`
（另存为网页时处理公式），而这一份还导出大量 `MT*`（`MTAPIVersion`、
`MTCloseOleObject`、`MTCopyButtonFace`…），版本资源写着
`CompanyName: WIRIS`、`WIRIS America (Design Science, Inc.)`——是 **MathType
安装时替换进 Office 目录的第三方组件**，文件日期 2024-02-21，而同目录的
Office 组件是 2026-06-15。目录里没有原版备份。

移开它之后 VBA 引擎工作正常：能加载、能运行、能正常报运行时错误
（测试文档借用的是 AxMath 的 VBA 工程，找不到 AxMath 环境时报错误 76，
那是测试构造的问题，不是引擎的问题）。

已排除的方向：它的 TLS 回调数组是空的；导出地址都在 `0x1xxxx` 段，
`0x4000` 处是数据不是函数入口，所以不是"RVA 当 VA 用"；它导入的 190 个
函数（advapi32/comdlg32/gdi32/kernel32/ole32/shell32/user32）在 Wine 里
**全部有实现，没有一个 stub**。`ITypeInfo::AddressOfMember` 走的是
`GetProcAddress`，返回的是真地址，也不是它。

再往下需要反汇编级调试才能定位 `0x4000` 的来源。当前可用的处置是把
`MathPage.wll` 移开——代价只是 MathType 的"另存为网页"公式转换，
MathType 的公式编辑本身走 COM，不受影响。

注意：默认宏安全设置（禁用并通知）下 Word 压根不加载 VBA，所以平时碰不到
这个崩溃，只表现为"宏没启用"。

### 追到崩溃指令：VBE7 用越界索引取了一个不是函数指针的字段

给 `kernelbase` 的故障诊断补上寄存器与栈字之后（winedbg 在这些故障上只打两条
dbghelp fixme 就停了），可以不靠调试器把数据流追出来。

崩溃指令是 VBE7 里的一次间接调用：

```
1800ebc76:  mov  0x68(%rsp),%rax        ; rax = 对象 S
1800ebc7b:  mov  0x30(%rax),%rax        ; 表 = S->field30
1800ebc83:  movzwl 0x50(%rsp),%eax      ; 索引（16 位）
1800ebc8c:  mov  0x18(%rcx,%rax,1),%rax ; 取 [表 + 索引 + 0x18]
1800ebc91:  mov  %rax,0xb8(%rsp)
...
1800ebd37:  call *0xb8(%rsp)            ← 故障，值为 0x4000
```

运行时取到的那张表长这样（索引为 `0x18`，于是落在 `+0x30`）：

```
+0x00: 0                     +0x08: S
+0x10: 堆指针                 +0x18: VBE7 真实 VA
+0x20: VBE7 真实 VA           +0x28: VBE7 真实 VA
+0x30: 0x0000000000004000 ★   +0x38: 0x0000000000003FD4
```

前三项是解析好的函数地址，`+0x30`/`+0x38` 是一对相差 44 的小整数——看形状是
容量/已用之类的字段，不是函数指针。**索引选错了表项，越过了函数数组的末尾。**

`GetProcAddress` 这条路是干净的：relay 显示 VBE7 对 MathPage 只解析了一个符号，
`GetProcAddress(base,"MTAPIGetNestingLevel")` 返回 `base+0x1b780`，完全正确。

### 两个查下来不成立的假设

**一、SYS_WIN32 的类型库在 64 位进程里被放大。** 崩溃前最后的 typelib 活动是查
`{4cee785c-...}` 2.0 —— Microsoft Forms 2.0，注册在 `win32` 键下、指向
`Temp\VBE\MSForms.exd`。那个 `.exd` 的 `syskind` 确实是 `SYS_WIN32`，于是
`oVft = VtableOffset * sizeof(void*) / ptr_size` 把偏移乘了 2。但这不是 Wine 写坏的：
**64 位的 FM20.DLL 内嵌的类型库本身就标着 `SYS_WIN32`**（微软自己的文件如此），
`.exd` 只是继承了它。实验性地停掉这个缩放，崩溃照旧。

**二、dispinterface 不该保留 vtable 槽位。** 解析路径对 `FUNC_DISPATCH` 存 `oVft=0`，
而返回路径（`TLB_AllocAndInitFuncDesc`）把 funckind 改成 `FUNC_DISPATCH` 却保留原
偏移，看起来是个不一致。改成一并清零之后，**Wine 自己的 typelib 测试报了 38 个失败**：
`Interface ITestDispInherit: Function parse_lcid: desc->oVft expected 104 got 0`——
Windows 的实际行为就是保留它。修改已回退，测试恢复 0 failures。

### 还差什么

索引 `0x18` 是从栈槽 `[rsp+0x50]` 读出来的，写入它的代码还没追到；要继续得在 VBE7
函数入口往下做逆向，而这是第三方组件（MathType）触发的私有路径。当前可用的处置仍是
把 `MathPage.wll` 移开。

### 结论：是 MathType 的残留，不是 Wine 的缺陷（2026-09-18）

追到指令层之后再往上问一句"这个组件凭什么在这里"，答案就出来了：
**MathType 根本没装。** 注册表里没有任何 `Design Science` / `WIRIS` / `MathType`
键，也没有安装目录——而 `MathPage.wll` 的字符串表里明写着它要找

```
HKLM\SOFTWARE\Design Science\DSMT7\Directories
HKCU\SOFTWARE\Design Science\DSMT7\MathPage
HKCU\SOFTWARE\Design Science\DSMT7\WCStats
```

一个都不存在。这是从 Windows 迁移 prefix 时带过来的孤儿：文件在，产品不在。
它找不到安装信息就把内部状态留空，VBE7 随后拿着这份没建起来的东西索引越界，
把一个容量字段当函数指针调用——就是 `0x4000`。

MathType 往 Office **自己的目录**里放了两样东西：

```
root\Office16\MathPage.wll                     替换掉 Office 自带的那个
root\Office16\STARTUP\MathType Commands 2016.dotm
```

**这解释了之前二分里最反常的一条**：把注册表里 7 个加载项全部 `LoadBehavior=0`
仍然崩，而 `/a` 不崩。因为这两个文件不靠注册表登记——Word 会加载那个目录下的
每一个 `.wll` 和 `.dotm`，只有 `/a` 拦得住。

后果分两档：`MathPage.wll` 让宏一启用就崩；那个 `.dotm` 的 AutoExec 找不到
MathType，每次启动弹一个运行时错误 76。

这不是 Wine 特有的。同样的残留在 Windows 上表现一样，所以修法是清掉残留，
而不是在 Wine 里绕开它。`scripts/fix-orphan-mathtype.sh` 做这件事：先确认
MathType 确实没装、文件确实是 MathType 的（Office 自带版只导出 `MP*`，
MathType 版还有 `MT*` 并在版本资源里写 WIRIS），然后把它们移到
`Microsoft Office\mathtype-orphans\`——移走而不是删除，装回 MathType 就能放回去。
脚本幂等，已接入 `import-office.sh`。

清理后实测：启用宏打开文档不再崩溃，运行时错误 76 也消失，VBA 引擎能正常加载、
运行、报告错误；默认设置下用户文档照常打开；项目回归 62 项全过。

## 安全中心：报告同一个 clamd（2026-10-01，altars-up `e53baaa32f9`…`8bf54fc4b20`）

AMSI 接上 clamd 之后，宿主机的杀毒引擎已经在替 Windows 程序扫描了，但 Windows 安全中心那一面还是空的：
Wine 没有 `wscapi.dll`。Outlook 的对象模型防护就看它——安全中心报告防病毒软件正常时，别的程序经对象模型读邮件
地址不提示，否则每次都弹“有一个程序正试图访问存储在 Outlook 中的电子邮件地址信息”。于是新写了 wscapi，报告的就是
AMSI 用的那个 clamd：

- **找 clamd 与 amsi 完全一样**：`WINE_AMSI_CLAMD_SOCKET`，再 clamd 自己的配置（`LocalSocket`），再发行版常见路径。
  问它 `zVERSION`；本机配置 `EnableVersionCommand false` 时答 `COMMAND UNAVAILABLE`，就改问 `zVERSIONCOMMANDS`，
  它的开头同样是 `ClamAV 1.5.4/28140/Thu Oct  1 02:24:38 2026`：产品版本、特征库版本、特征库生成时间（本地时间）。
- **状态**：clamd 应答就是“开”；特征库生成不超过 7 天算“最新”，超过就“过期”（ClamAV 自己从第 7 天起告警）。
  `WscGetSecurityProviderHealth` 对防病毒与反间谍软件据此给 GOOD 或 POOR；配置了 clamd 却连不上是 POOR，
  一点 clamd 的痕迹都没有是 NOTMONITORED；安全中心服务本身 GOOD；防火墙、自动更新、Internet 设置、UAC 不监视。
  一次问多个提供方时取最差的。`WscRegisterForChanges` 每 30 秒重看一次，健康或特征库状态变了就回调。
- **产品列表**（`WSCProductList`，`IWscProduct3`）：防病毒与反间谍软件各列出一个 ClamAV，名称、开关、特征库状态、
  特征库生成时间（作为产品状态的时间戳，HTTP 日期格式）、一个 Wine 自己的 GUID；没有修复程序路径（Windows 侧没有能
  “修好”它的程序）。防火墙列表为空——Windows 自带防火墙在 Windows 上也不出现在这里。
- **WMI**：`root\SecurityCenter2` 原来是空命名空间，现在 `AntiVirusProduct`、`AntiSpywareProduct`、`FirewallProduct`
  都从产品列表取数。`productState` 没有文档，按 Windows 上第三方产品的取值：第三字节 0x04，开着时中间字节 0x10，
  特征库过期时低字节 0x10，所以开着且最新是 266240（0x41000），与 ESET、赛门铁克等在 Windows 上的值相同。

实测：开发前缀里 Outlook 读条目正文不再提示；把 `WINE_AMSI_CLAMD_SOCKET` 指到不存在的路径，提示立刻回来。
wscapi 测试 64 项、wbemprox query 1814 项 0 失败；`tools/securitycenterprobe/securitycenter.vbs` 在 Wine 里列出
ClamAV（266240）。还没在 winref 上对照的：各种非法参数的返回码（测试只打印）、Defender 的 `productState` 与修复路径、
`root\SecurityCenter`（XP 时代的旧命名空间，Outlook 的 OLMAPI32 也会问它；Wine 里连不上，`0x800401E4`）在
Windows 11 上还在不在。这些 10 月 2 日都在 winref 上对照过了，见文末“按 winref 实测对齐”一节。

## 把 MathType 和 AxMath 整个搬过来（2026-09-18）

前一节把 MathType 的残留移走只是止血——产品本身没装。这一节把它和 AxMath 从原机
（`/mnt/win-c`）完整搬过来。

### 光复制 Program Files 没用

两个产品都把"自己装没装"这件事记在注册表里，MathType 还往 **Office 自己的目录**
里放文件。只复制程序目录，`MathPage.wll` 仍然找不到安装信息，宏一启用照样崩。
要搬的是四类东西：程序目录、`ProgramData`、Office 目录里的 64 位组件、以及注册表
（`Design Science\DSMT7` 的 32/64 两个视图、`HKCU` 设置、OLE 对象注册）。

### 注册表上的两个坑

**一、hivexregedit 的导出不能直接喂给 Wine 的 regedit。** 它把每个值写成一行，
REG_SZ 以 `hex(1)` 形式出现，而 Wine 的 .reg 解析器会截断这种长行——
`C:\Program Files (x86)\MathType\System` 存进去变成 `C`。偏偏这正是决定"产品能不能
被找到"的值，而且失败是静默的。`scripts/import-win-registry.py` 改为逐值调用
`reg add`（参数没有行长限制），并且从不打印值：这类导出可能带授权材料。

**二、32 位安装程序只写 32 位视图，而 MathType 自己的 64 位 MathPage.wll 从 64 位
视图读安装目录。** 在 Windows 上它靠 `KEY_WOW64_32KEY` 跨过去；与其依赖这个，
脚本把同一份**真实**值镜像到 64 位视图。没有任何值是编造的，全部来自原机。

### 必须重建 Wine 带 WoW64

MathType.exe、AxMath.exe 和 MathType 的语言 DLL 全是 32 位。原来的构建是
`--enable-archs=x86_64`，32 位程序根本起不来（`failed to load syswow64\ntdll.dll`）。
`build-wine.sh` 改为接受 `ARCHS`（默认仍是 x86_64），用 `ARCHS=i386,x86_64` 重建到
独立的 `dist-wow64`，不动当时还在工作的 `dist-cx`。所有补丁在两个架构上都在。

重建暴露了一个真实缺陷：`kernelbase/debug.c` 里 `stack_scan_from` 在定义前被调用，
隐式声明（非 static）与真正的 static 定义冲突。64 位一直是增量构建所以从没编译到，
干净的 32 位构建直接失败。已修。

切换 prefix 用 `scripts/enable-wow64.sh`：从 Windows 搬来的 prefix 里带着 Windows
自己的 `syswow64`（861 个微软二进制），Wine 加载不了它们，必须让位给 wineboot 重建
Wine 的 32 位运行时——移走而不是删除。

### 结果

MathType 和 AxMath 都能正常启动（`EQNWINCLASS` 公式编辑窗口、`AxMath - Untitled`
主窗口），`MathPage.wll` 工作，OLE 对象注册到位，插入公式走 `插入 > 对象`。
Word 打开文档正常，启用宏不再崩溃。

**唯一没能打通的是 MathType 的 Word 工具栏模板**，而且根因还没找到。

先更正一个我自己下错的结论：我曾判断它是"引用 32 位语言 DLL 所以 64 位 Word
加载不了"。那是错的——它自带的 64 位模板里只有一个 `Declare`
（`GetLastError`，带 `PtrSafe`），根本不靠 `Declare` 调 MathType 的 DLL；
`mswXXX.DLL` 只是 32 位分支留下的字符串。它和 MathType 之间走的是 COM，
而跨进程 COM 从 64 位调 32 位完全正常。

实际现象是 VBE7 在给 VBA 工程接线时，从一张只有三项（正好是 IUnknown）的函数表里
按索引 `0x18` 取第四项，取到一个容量字段当函数指针调用，故障地址 `0x4000`。
已经排除的方向：

- **SYS_WIN32 类型库的 oVft 缩放**：加诊断实测所有 `oVft` 都是 8 字节对齐的，
  换算没问题；实验性停掉缩放，崩溃照旧。
- **类型库注册**：崩溃前 `{efd87372-...}`（Microsoft Forms 2.0）只注册在 `win32`
  键下，看着像 64 位进程找不到；把同一文件也注册到 `win64`，崩溃照旧。
- **StdFont**：崩溃前反复 `CoCreateInstance {0be35203-...}`，查下来注册正确，
  反复调用是每个控件一个字体对象，属正常。
- **WoW64 缺失**：装上 WoW64、两个产品都能跑之后，这个崩溃一模一样。

MathType 自己的 `Setup.exe` 能跑起来并认出已有安装，但**跑不完**：它先要求
关掉 Office（照做后）随即报「文件 Setup.exe 无法安装, 因为通用 I/O 错误」。
原因是我们搬过来的是**已安装的目录**而不是安装介质——注册表里的
`InstallFromDir` 指向一个早已不存在的临时源目录，Setup 无源可取。

而 `Setup.inf` 里 `$register_dll` 只有两行（`MathPage\32\MathPage.wll` 和
`MathPage\64\MathPage.wll`），也就是说它要做的 DLL 注册只有 MathPage.wll 这一个。
手工 `regsvr32` 两个版本都试过：32 位返回 0、64 位注册完不退出（regsvr32 的老毛病），
但注册表里没有出现任何 MathPage 相关的类，模板照旧崩。

原机上也找不到 MathType/AxMath 的安装包（`Downloads` 和分区里都没有），所以无法
用原始安装程序在 prefix 里重装一遍。要真正打通工具栏集成，缺的就是这个安装包。

在此之前该模板不安装；公式通过 `插入 > 对象` 走 OLE 注册，MathType 主程序照常用。

### 验一遍：公式到底能不能插进去、功能区到底有没有

上面这些结论此前都是"注册表看着对"推出来的，没有一条是量出来的。补测之后有两条
需要写进来。

**一、两个公式对象在 64 位客户端里都真的能创建。** `tools/oleprobe` 把
"插入 > 对象"的三步拆开报 HRESULT：

```
=== Equation.DSMT4 ===            === Equation.AxMath ===
  ProgID 解析      {0002CE03-…}     ProgID 解析      {B18C2BCC-…}
  创建(进程内   ) 失败 0x80040154   创建(进程内   ) 失败 0x80040154
  创建(本地服务器) 成功 0x00000000   创建(本地服务器) 成功 0x00000000
    IOleObject       有 0x00000000     IOleObject       有 0x00000000
                                       对象名           Equation.AxMath
```

`进程内` 那行的 `0x80040154` 不是毛病：两个产品都注册成 `LocalServer32`，
跨进程激活本来就是它们该走的路，而 64 位客户端驱动 32 位的进程外服务器是正常的。

**二、AxMath 的 Word 功能区加载项是完整工作的。** 这一条推翻了"32 位产品的模板
进不了 64 位 Word"的想当然。`AxMath.dotm` 就在 Word 的 `STARTUP` 目录里，Word
启动后用 MSAA 读功能区（`tools/uidump`，不依赖截图）：

```
name="功能区选项卡" value="Ribbon Tabs List" [page tab list]
  … name="视图" [page tab] rect=(374,49,44,31)
    name="AxMath" [page tab] rect=(419,49,70,31)
    name="Zotero" [page tab] rect=(490,49,63,31)
```

点开之后里面的命令也都排好了版：行内公式、行间公式、左/右编号公式、插入引用、
插入编号、更新编号、Browse Equations…… 都带实际的布局矩形。也就是说**Word 里
有一个完全可用的公式编辑器**，只不过是 AxMath 而不是 MathType。

这同时给 MathType 模板的崩溃划掉了一整类解释：AxMath 同样是 32 位安装程序装的
32 位产品，它的模板在同一个 64 位 Word 里加载得好好的。所以那个崩溃不是位数问题。

**顺带证伪一个看着很像的猜想。** AxMath 的 CLSID 只写在
`HKLM\SOFTWARE\Classes\WOW6432Node\CLSID` 下，而它的 ProgID 在 64 位视图——按
Windows 的规矩，64 位 Word 顺着 ProgID 找过去应该找不到类。于是我把 CLSID 镜像
进 64 位视图。做对照实验（有镜像 / 删掉镜像 / 再镜像回去，各跑一轮）的结果是
**三轮全部成功**：Wine 的 `HKLM\Software\Classes` 根本不按位数分视图。镜像是多余
的，会让 prefix 偏离源安装，已经撤销。

教训和本文档前面那次 `oVft` 一样：改 Wine 或改 prefix 之前，先做一次会失败的对照
实验，否则分不清是修好了还是本来就没坏。

### 拿到安装包之后：MathType 正式安装了，工具栏仍然崩

上一节说"缺的就是安装包"。安装包拿到了（`MathType-win-zh-7.12.2.466.exe`），
于是前面关于"搬过来的不是安装介质"的推断可以直接验证——**是对的**：这个 exe 是
NSIS v3.10 自解压包，它把 110MB 载荷解到 `%LOCALAPPDATA%\Temp\mathtype.tmp`，
里面正是 `setup.exe` / `setup.inf` / `mathpagex64.wll` / `wordui2013x64.dotm` 这套
安装介质，装完后 `InstallFromDir` 指向它。原来那份搬过来的安装里，这个目录早就没了。

**「通用 I/O 错误」的真正原因也查清了，而且很蠢：** 我是从**已安装目录**运行
`Setup.exe` 的，而它要做的事情之一就是把介质里的 `setup.exe` 复制到那个目录——
等于自己覆盖自己，于是报「文件 Setup.exe 无法安装, 因为通用 I/O 错误」。
从介质目录 `...\Temp\mathtype.tmp\setup.exe` 运行，一路走到「MathType 已成功安装」。

安装器做成了手工怎么都做不成的那一步：

```
regsvr32: Successfully registered DLL 'C:\Program Files (x86)\MathType\MathPage\64\MathPage.wll'
```

它也确实装了 Word 集成，只是装在 **Office 自己的机器级 STARTUP 目录**，不是用户的：
`root\Office16\STARTUP\MathType Commands 2016.dotm`。我起初只查了 Office16 目录本身和
用户的 `%APPDATA%\...\Word\STARTUP`，因此一度以为"安装器没做 Office 集成"——错的。

**结果是：干净安装、正版介质、DLL 由安装器自己注册，工具栏模板照样让 Word 崩。**
这条排除了"安装不全 / 搬运残缺"这一整类解释。实测：

| 配置 | 结果 |
| --- | --- |
| STARTUP 里放 2016 模板 | 3 次里崩 2 次，约 5 秒；另一次没走到初始化 VBA |
| 只放 `MathPage.wll` | 不崩 |
| 2013 / 2010 版模板 | 同样崩 |
| 模板移出 | Word 起来、VBA 初始化完成、根本不加载 `MathPage.WLL` |

判定"没崩"要看 VBE7 有没有被加载：Word 常常两三分钟还停在登录框上，那种"没崩"
只是没走到 VBA 而已。`+loaddll` 下崩溃那次 1586 行、VBE7 出现 16 次，没崩那次只有
110 行、VBE7 一次都没出现——一开始我把后者当成"修好了"。

崩溃现场（靠 `kernelbase` 里那段栈扫描诊断打出来的）：

```
wine: Unhandled page fault on execute access to 0000000000004000
  VBE7.DLL +0xebd3e      <- 返回地址：发出这次间接调用的就是 VBE7 自己
  ...
  MathPage.WLL +0x0
  wwlib.dll +0xe7a923
rsp+0x50: 0000000000000003  0000000000000018   <- 槽号 3、偏移 0x18
```

也就是 VBE7 从某个对象的虚表取第 4 项（`0x18`）去调，槽里是 `0x4000`。调用是
**VBE7 自己发出的**，不是 Wine 的 `ITypeInfo::Invoke`（否则返回地址会在 oleaut32）。

这一轮新排除的方向：

- **`LHashValOfNameSysA` 不认 SYS_WIN64**：日志里 VBE7 对每个名字先按 syskind 3
  再按 1 算哈希，而 Wine 的 trace 对 3 打印的是空串，看着很像没处理。读实现：哈希
  只在 `SYS_MAC` 时改变（`nMask`），1 和 3 出同样的值，空串只是 trace 文本没覆盖。
- **VBE7 反复 QI 的 `{cacc1e82-…}` / `{cacc1e83-…}` Wine 没实现**：这两个 IID 在
  VBE7.DLL 里有，但原机 Windows 的 `oleaut32.dll`（64 位和 32 位）、`ole32.dll`、
  MSO 各 DLL 里**都没有**——在 Windows 上同样会 `E_NOINTERFACE`，不是差异点。
- **崩溃前最后那个 `0x4000` 页错误是第二次异常**：末尾那段是 `_XcptFilter` 的 SEH
  展开，属于 Office 自己的崩溃上报路径。第一次异常在 694906 行，地址相同。

因此 `scripts/fix-orphan-mathtype.sh` 改了前提：以前它只在"MathType 不在了"时清残留，
现在**无论 MathType 装没装，都把 Word 工具栏模板移出 Office 的 STARTUP**，并说明原因。
想试新版 Wine 是否修好，用 `MATHTYPE_TEMPLATE=keep` 保留它。

现状（全部实测，不是推断）：MathType 7.12.2.466 由自带安装器正式装好、主程序可用、
`Equation.DSMT4` 在 64 位客户端里能激活并给出 `IOleObject`；Word 正常启动、VBA 初始化
完成、功能区是 `开始 插入 设计 布局 引用 邮件 审阅 视图 AxMath Zotero 帮助`。
公式编辑走 AxMath 的功能区，MathType 的公式走 `插入 > 对象`。
装之前的状态备份在 `$WINEPREFIX/mathtype-preinstall-backup`（含注册表导出和旧目录）。

### 「把原来的激活挪过来」：没有可挪的

重装之后 MathType 弹「该版本 MathType 尚未激活」，要么试用 30 天，要么输入产品密钥。
自然的想法是：安装器把原来的激活覆盖掉了，搬回来就行。查下来**不是**——原本就没有。

先定位激活状态存在哪。安装日志给了准信：

```
Opening config file at ...\Temp\mathtype.tmp\MT7.dsc: Success
Install state before registration dialog: checksum = good,
    state = installStateUninstalled, installDate = 01/01/70, now = 09/18/26
```

也就是 `MT7.DSC` 是注册状态文件，安装器确实用一份「未注册」的新文件（1985 字节）
覆盖了原来那份（2001 字节）。于是把原来的原样搬回去试——**两份都试了**：

| 放进去的 MT7.DSC | 结果 |
| --- | --- |
| prefix 里重装前那份（2001 字节） | 仍然「尚未激活」 |
| 原机 Windows 上那份（2001 字节，2024-11-20） | 仍然「尚未激活」 |

再直接跑重装前留下的整套旧程序（`MathType.pre-installer\MathType.exe`，7.8）：
**它弹同一个激活对话框**。所以这个 prefix 里从一开始就没激活过。

原机上也确实没有：`Program Files (x86)\MathType` 下除 `MT7.DSC` 外没有任何许可文件；
`ProgramData`、各用户 `AppData` 下没有 wiris/mathtype 目录；`HKCU\Software\JavaSoft\Prefs`
下只有一个无关程序（激活界面 `mathtypelib.exe` 是 Java 写的，所以这里值得一查）；
`HKLM\...\Design Science` 全部 88 个值里没有一个像注册信息——只有根键下的 `MachTime`
和 `Random` 两个不透明值，那是试用计时，不是激活。原机装的是 MathType 7.8.0（WIRIS）。

这也印证了此前的一个猜测："这个毛病感觉 Windows 里好像也有"——确实如此，原机上它
同样没激活，只是平时只用 `插入 > 对象` 插公式，不碰需要激活的功能就看不出来。

`MT7.DSC` 已还原成 7.12 安装器自己写的那份；重装前那两份和原机那份都没动，分别在
`$WINEPREFIX/mathtype-preinstall-backup/` 和原分区里。注册表里的试用计时值是随原机
一起搬过来的真实状态，没有重置——重置它等于绕过试用期，不做。

要真正激活只有两条正路：输入产品密钥，或者点那个 30 天试用（用掉就没了，所以留给你决定）。

## 系统集成：让它像装在机器上的软件

到这里为止，Office 能跑、能开文档、能编公式，但它还不像一个装在系统里的程序：
菜单里找不到、双击文档要靠 Wine 的通用处理器、任务栏图标不归组、打印属性是个
英文小对话框。这一节把这四件事做完。

### 一、双击文档与菜单项

Wine 的 winemenubuilder 已经按扩展名生成了一批 `.desktop`，但它们是**处理器不是应用**：

```
[Desktop Entry]
Name=Microsoft Word
Exec=env "WINEPREFIX=..." wine start /ProgIDOpen "Word.Document.12" %f
NoDisplay=true          ← 菜单里看不到
Icon=7A2C_WINWORD.0
```

`NoDisplay=true` 意味着 Office 在应用菜单里根本不存在，也就无法固定到任务栏；
而且没有 `StartupWMClass`，窗口起来之后不会和启动它的图标归为一组。

`scripts/install-desktop-integration.sh` 为装上的每个 Office 程序生成一个正常条目：
中英双语名称、真实图标（winemenubuilder 已从 exe 里抽好，8 个程序全有）、
它真正能打开的 MIME 类型、以及桌面动作（新建文档）。本机结果：

```
word / excel / powerpoint / outlook / onenote / access / publisher / visio
共 8 个，desktop-file-validate 全部 OK
默认打开程序已指向 wine-altars-word.desktop 等（Word 6 种、Excel 6 种、PPT 5 种…）
```

### 二、任务栏归组

靠的是 `StartupWMClass`。Wine 用可执行文件名做窗口类名，实测：

```
xwininfo: 0xa0001f ("winword.exe" "winword.exe")
wine-altars-word.desktop: StartupWMClass=winword.exe
```

两边对上，窗口才会落到那个图标下，而不是另起一个通用图标。

### 三、启动器：语言、路径、进程

`scripts/office-launch.sh` 统一三件事，缺一个都会露馅：

- **语言**。Wine 从进程 locale 选自己的资源。本机宿主是 `en_US.UTF-8`，于是中文
  Office 配英文 Wine 对话框——打印属性正是最常撞见的那个。关键细节：**必须设
  `LANG` 并 `unset LC_ALL`**，只设 `LC_MESSAGES` 无效。实测：

  ```
  unset LC_ALL + LANG=zh_CN   → reg: 找不到所指定的注册表项
  LANG=zh_CN 但继承 LC_ALL=en → reg: Unable to find the specified registry key
  ```

  本机 `LC_ALL=en_US.UTF-8` 是显式设置的，会压过 `LANG`，这一步不做就白做。
- **路径**。桌面条目交过来的是 Unix 路径，Office 要 Windows 路径，用 `winepath -w` 转。
- **进程**。用 `exec` 而不是留一层壳，窗口管理器按 WM_CLASS 匹配，中间多一个进程只会
  多一个退不干净的尾巴。

### 四、打印：从「不像 Windows」到像

原状：点"打印机属性"弹出的是 Wine 的单页英文 `Options` 对话框，只有纸张大小、纸盒、
方向、打印质量四项。Windows 上这里是「打印首选项」的多标签页。

底层其实是好的——`tools/printprobe` 把 Office 点那个按钮时走的 API 逐步拆开量过：

```
DocumentProperties(size)   = 3776        驱动能给出 DevMode
DocumentProperties(out)    = 1           拿得到
CreateDC                   = 成功         可打印区 2433x3183, 205x269mm, 300dpi
```

CUPS 队列已由 Wine 自动桥接，`wineps.drv` 走 PostScript，DevMode 里是从 PPD 真实
导入的纸张（含 16k 等国内纸型）。问题只在 UI 和默认值上。改了三处：

**1. 拆成 Windows 那样的两页**（`dlls/wineps.drv`）。`布局`：方向、双面、份数、
逐份打印；`纸张/质量`：纸张大小、纸盒、打印质量、颜色。一个对话框过程服务两页——
不在当前页上的控件根本找不到，所有会碰它的调用自然成了空操作，于是两页互不需要知道
对方的存在。双面和颜色按 PPD 能力决定显示与否：PPD 没声明 `*ColorDevice` 就不给颜色
选项，给了却被静默忽略比不给更糟。

**2. 中文**。po 里本来就有译文，缺的是新加控件的：`布局`、`纸张/质量`、`份数`、
`逐份打印`、`双面打印`、`黑白` 已补进 `po/zh_CN.po`。

**3. 默认纸张**。Wine 本来会读 `LOCALE_IPAPERSIZE`，但紧接着被一句
"We'll let the ppd override the devmode" 无条件覆盖——于是中文环境也默认 Letter，
只因 PPD 出自美版机型。改成 PPD 先定、locale 的纸型若在 PPD 列表里则胜出。实测
从 `纸张=1`（Letter）变成 `纸张=9`（A4），并且一直传到作业票：

```
%!PS-Adobe-3.0
%cupsJobTicket: media=A4
%%Creator: Wine PostScript Driver
```

这里有个容易自摆乌龙的地方：Wine 会把 DevMode 缓存进打印机的注册表键，之后一直用
缓存那份，读 locale 的代码只在没有缓存时才跑。所以 `scripts/setup-printing.sh` 清缓存
时**必须在启动器同一个 locale 下重建**——我第一版在英文 shell 里清，结果又把 Letter
写了回去。

`printprobe -print` 能把一页渲染成文件，整条链路（驱动、DevMode、字体、页面设置）
不费一张纸就能验：`StartDoc/StartPage/EndPage/EndDoc` 全部返回成功。

## 登录改成原生的那一条，并且让它留得住

### 它为什么会"掉"

登录一直靠的是 WAM 令牌：`windows.security.authentication.onlineid` 这个 shim 按
scope 从 `Z:\tmp\office-wam-tokens` 一个文件一个文件地读，令牌由
`scripts/mint-wam-tokens.sh` 从一台 Windows 机器上取来。它自己的头注释就写着
"useless within hours"。

这次掉的具体原因是两层叠加：

- 令牌是 9 月 6 日铸的，**当天就过期了**；
- 文件也没了——这台机器的 tmpfiles 规则是 `q /tmp 1777 root root 10d`，`/tmp` 下超过
  10 天的文件自动清除，9 月 6 日到 9 月 18 日正好过线。目录空着，时间戳停在 9 月 6 日。

Office 那边 `Identity\ConnectedAccountCID` 还留着，`Identity\Identities` 却是 0 条——
不是"登录过期待刷新"，是只剩一个悬空的账号指针。

### 真正的毛病：broker 认领了一个它服务不了的提供者

文档前面早有结论："认领一个服务不了的 provider 比不认领更糟"。但 shim 一直是无条件
认领的。带 `+webauth` 实测一次启动：

```
FindAccountProviderWithAuthorityAsync   20 次
FindAccountProviderAsync                18 次   ← 每次都给出提供者
GetTokenSilentlyAsync                    6 次   → no token held → status 3
WebView2 相关行                          0     ← Office 自己的登录页从未运行
```

于是改成：**手上没有令牌就不认领**。判据是令牌库本身，六个查找入口都走同一个
`create_provider_operation`，一处守卫全覆盖。改完再测：

```
standing aside so the caller uses its own sign-in   13 次
GetTokenSilentlyAsync / FindAllAccounts             不再出现
启动后第 10 秒                                      OneAuthWebView2Browser
```

`OneAuthWebView2Browser` 正是文档前面记过的那个成功形态。连测两次都是第 10 秒出现。

这条规则同时保留了两条路：铸了令牌就用 WAM（临时救急），没令牌就走 Office 自己的
窗口（持久）。不需要开关，令牌库在不在就是开关。

### 登录结果留不留得住：DPAPI

原生登录把状态用 DPAPI 密封后写进 profile。这一条要是不成立，登录当场有效、重启即失，
看上去就像"又掉了"。`tools/dpapiprobe` 分两个进程验证，中间还杀掉 wineserver：

```
第一个进程：封装成功，180 字节
（杀掉 wineserver）
第二个进程：解封成功: 内容一致 ✓
```

所以登录一次就够了。

### 安装流程里删掉了一件本来就做不成的事

`import-office.sh` 原来第 7 步会把原机的 `OneAuth` / `IdentityCache` / `TokenBroker`
一并复制过来。**那在这里永远解不开**——它们在写入它们的那台机器上用 DPAPI 密封，
这里没有对应的密钥。Office 拿到读不了的缓存不会"重新登录"，而是报"无法访问你的账户"
且**不给登录表单**，比空 profile 更糟。这一步改成明确跳过并说明原因。

### 一个被实测证伪的顺手改动

既然跨机缓存有害，我顺手把 prefix 里现存的 6 处也移走了（移动不删除）。结果**更糟**：
没有账号记录之后，Office 不再走 WebToken 那条路，改用 `OnlineIdAuthenticator` 要 MSA
的 RPS ticket——而这正是本文前面记过的、Wine 产不出来的东西。追踪里
`webauth:statics_FindAccountProvider*` 归零、`standing aside` 归零，全变成
`onlineid:ticket_*`，登录窗再也不出现。已全部放回，`OneAuthWebView2Browser` 随即恢复。

所以 `verify-signin.sh --move-foreign-caches` 保留为**对症手段**，只在 Office 真的报
"无法访问你的账户"时用；默认那一项只作提示，不算缺陷。教训还是老一条：改之前先想清楚
现象是不是你要治的那个。

### 现在的状态

`scripts/verify-signin.sh` 把整条链路逐环检查，并接进了 `import-office.sh`：

```
[ 好 ] WebView2 / 绕过 broker 两个门    已开
[ 好 ] WebView2 运行时                  153.0.4234.32
[ 好 ] broker 让位                      无令牌，broker 不认领提供者
[ 好 ] DPAPI 跨进程                     登录结果可持久
[ -- ] 身份缓存                         4 处早于本 prefix；保持原样
```

启动 Office 时会自己弹出 `OneAuthWebView2Browser` 登录页。**账号密码要你自己输**——
我不会代输，也没有任何地方伪造登录状态。

## 更正：桌面集成不该由脚本做，Wine 自己就会

前面那一节用一个自写的 `.desktop` 生成器加一个 `office-launch.sh` 包装脚本解决了菜单、
图标、任务栏归组和界面语言。能用，但**方向错了**：启动路径上挂着我的脚本，而这四件事
本来就是 Wine 的职责。查下去发现每一件它都已经实现了，只是被两个缺陷挡住。

### 一、菜单项：`cx_mode` 硬编码成 1，条目全被丢掉

`winemenubuilder` 有完整的 XDG 写出实现——名称、`Icon`、工作目录，连窗口管理器归组用的
`StartupWMClass` 都会写（它从目标 exe 文件名取小写，正是 `winword.exe`）。但
`programs/winemenubuilder/cxmenu.c` 里：

```c
int cx_mode = 1;
```

CrossOver 不自己写条目，它把每一项交给外部发布器 `wineshelllink`。那个程序属于
CrossOver，不属于 Wine。**这里没有它**，于是 `__wine_unix_spawnvp` 生成的每一项都喷进
虚空，静默失败。这就是"复制来的 prefix 只有文件关联、没有应用程序"的真正原因，和快捷
方式有多少无关。

改成默认 0，`WINE_CX_MENUS=1` 可以切回去。

### 二、快捷方式：这个 prefix 里一个都没有

Wine 的菜单支持**完全由 `.lnk` 驱动**（`winemenubuilder` 每次处理一个链接）。而这个
prefix 是复制已装好的 Office 得来的，从没跑过安装程序，所以 `ProgramData\...\Start Menu`
下连一个 Office 快捷方式都没有——MathType 有，因为它的安装器真的跑过。

所以正确做法是**建快捷方式，而不是建 `.desktop`**。`tools/mkshortcut` 用
`IShellLinkW` + `IPersistFile::Save` 创建，和安装程序做的是同一件事；保存动作本身就会
让 shell32 去调 winemenubuilder。结果是 Wine 自己写出：

```
[Desktop Entry]
Name=Word
Exec=env "WINEPREFIX=..." wine "C:\ProgramData\...\Start Menu\Programs\Word.lnk"
Path=/home/.../Office16
Icon=7A2C_WINWORD.0
StartupWMClass=winword.exe
```

八个程序齐全，图标和窗口类都是 Wine 推导的。

### 三、界面语言：注册表里的开关本来就有，但坏了

`dlls/ntdll/locale.c` 有一段 CrossOver 的 locale 覆盖，读 `HKCU\Software\Wine` 下的
`LC_ALL` / `LC_CTYPE` / `LC_MESSAGES`。两个缺陷让它一半失效：

- `LC_CTYPE` 和 `LC_MESSAGES` 解析的是 `bufferW`，也就是 `KEY_VALUE_PARTIAL_INFORMATION`
  的结构头，而不是它后面的字符串（`LC_ALL` 那一支用的是 `info->Data`，是对的）；
- 两者都赋给 `system_lcid`。于是 `LC_MESSAGES`——唯一一个含义就是"界面语言"的类别——
  永远到不了下面那句 `NtSetDefaultUILanguage`。

实测（宿主 `LC_ALL=en_US.UTF-8`）：

| prefix 里设的值 | Wine 的报错语言 |
| --- | --- |
| `LC_MESSAGES=zh_CN.UTF-8` | 英文（无效） |
| `LC_ALL=zh_CN.UTF-8` | 中文（但连带改了整个 locale） |

两处都修好之后，只设 `LC_MESSAGES` 就能让界面变中文而不动其它类别。

### 结果：启动路径上没有脚本了

宿主保持 `LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8`，不设任何变量，直接执行 Wine 自己写的
那行 `Exec`：

```
Word 启动，WM_CLASS = ("winword.exe" "winword.exe")   ← 与 StartupWMClass 对上
打印属性页：布局 / 方向 / 纵向(P) / 横向(L) / 份数(C) / 逐份打印(O) / 确定 / 取消
```

双击文档走 Wine 自己的 `wine-extension-*.desktop`（`wine start /ProgIDOpen`，路径转换由
`start.exe` 做），关联默认值在删掉我那批条目后自动回到了它们。

`scripts/office-launch.sh` 和 `scripts/install-desktop-integration.sh` 已删除；
`scripts/install-start-menu.sh` 只做安装期该做的两件事：建快捷方式、写 prefix 的语言，
剩下的交给 Wine。

### 菜单项点了没反应：它启动的是另一个 Wine

上一节做完之后，从开始菜单点 Word **没有任何反应**，再点一次会弹安全模式提示，选"否"
仍然没反应。

原因是 winemenubuilder 写出的 `Exec=` 里是一个裸 `wine`，点击时从 PATH 解析。这台机器上：

```
/usr/bin/wine -> /opt/wine-stable/bin/wine     （winehq-stable 包）
```

也就是说菜单项拿**发行版的 Wine** 去开我们的 prefix。实测它确实会启动 WINWORD.EXE
（日志里有 `msowercrash.dll`、`get_dummy_preferred_ui_language 0x409` 这些 Office 自己的
痕迹），但缺了这棵树上的全部补丁，**退出码 3，一个窗口都没画出来**——于是 Word 认为
上次运行失败，下次启动就问要不要安全模式。整个过程没有任何地方说明"跑的是哪个 Wine"。

改法是让条目记住写它的那个 Wine。`WINELOADER` 正是当前进程所用的加载器路径，
winemenubuilder 把它转成 Unix 路径写进 `Exec=`，菜单项和文件关联两处共用一个 helper，
变量不存在时仍退回裸 `wine`。现在条目长这样：

```
Exec=env "WINEPREFIX=..." "/opt/wine-altars/lib/wine/x86_64-unix/wine" "C:\...\Word.lnk"
```

文件关联那批不会自动重写——`winemenubuilder -a` 只在关联有变化时才动手，判据是
`HKCU\Software\Wine\FileOpenAssociations`。清掉那个键再跑 `-a` 才会全量重建。
**注意它会连带清掉属于其它 prefix 的关联**（这台机器上少了 18 个 QQ音乐 / ClickOnce 的），
重建后要把那些补回去。

### 让系统的 `wine` 就是这一个

`scripts/install-system-wine.sh` 把构建结果复制到 `/opt/wine-altars`，再用
`/usr/local/bin`（在 PATH 里排在 `/usr/bin` 之前）指过去。**不动 winehq-stable 的任何
文件**，撤回就是删掉那些链接（`--uninstall`）。

用复制而不是软链到构建目录：把整个系统指向一个 git 工作树，一次重建或一次误删就会
带走机器上所有 Wine 程序。代价是重建之后要重跑本脚本刷新 `/opt`。

这是一个**系统级改动**：机器上所有 Wine 程序都会改用这个构建，各前缀在下次启动时按新版本更新。

**10 月 1 日换成 altars-up（`8bf54fc4b20`，Wine master 加本项目的提交，没有 CrossOver 的改动）。** 原来的
CrossOver 血统 11.0 整个留在 `/opt/wine-altars.prev`，`--rollback` 换回来。这一轮顺带改了：

- **bin 里的每个工具都链过去。** 原来 `/usr/local/bin` 只有 wine、wineserver 和几个开发工具，`winecfg`、`wineboot`、
  `regedit`、`msiexec`、`winepath` 仍落到 `/usr/bin` 的 wine-stable——同一个前缀被两个 Wine 轮流打开，协议号一不同
  就起不来。现在 24 个都指向 `/opt`；脚本装前还会确认没有哪个前缀的 wineserver 还在用旧的那份。
- **菜单项改指 `~/.wine-c2r-up`。** Word、Excel、PowerPoint、Outlook、Office 语言首选项和 18 个文件关联原来指着旧前缀
  `~/.wine-c2r-test`（旧前缀最后一次刷新时覆盖了同名文件）。删掉新前缀里那 18 条 `FileOpenAssociations` 记录、
  再跑 `winemenubuilder -a`，菜单项逐个 `winemenubuilder <快捷方式>` 重建。旧前缀现在没有任何入口。
- **脚本里跑 winemenubuilder 要带上桌面会话的变量。** `env -i` 的环境里没有 `XDG_CURRENT_DESKTOP`，它只打一行
  `Skipping .desktop file creation, no XDG-compliant desktop environment detected` 就退出码 1，什么都不写。
  这台机器是 `XDG_CURRENT_DESKTOP=ubuntu:GNOME`。也因此此前所有测试运行都没有动过菜单。
- **同名的关联文件归最后刷新的那个前缀。** `.application`、`.appref-ms`、`.xaml`、`.xbap` 两个前缀都有：新前缀的
  `-a` 先把它们改指了自己，已还给原来的 `~/.wine`；`~/.wine` 更新后它的 `.crt`、`.reg` 有变化，刷新时改指了
  `~/.wine`（用户下次开 QQ音乐时也会这样）。双击 `.reg` 导入的是它所指前缀的注册表。
- **`~/.wine`（QQ音乐、LAV Filters）已用新 Wine 更新**：18 秒，无错误；用户目录本来就是 `user`，不受用户名
  改动影响；更新前的注册表留在 `~/.wine/registry-before-wine-11.18/`。
- 开发脚本的默认前缀改成 `~/.wine-c2r-up`；`word-iter.sh` 改为把 `wine-src-up` 的 DLL 部署进 `dist-up` 而不是 `/opt`。
- 开发与日常共用 `~/.wine-c2r-up`：`dist-up` 与 `/opt` 同一提交时可以混用；改了服务器协议之后，要么先停掉那个前缀的
  会话，要么重跑本脚本让 `/opt` 跟上，否则从菜单点开的 Office 连不上开发版留下的 wineserver。

验收（干净登录环境、PATH 只有系统路径、不设任何变量）：

```
PATH 上的 wine: /usr/local/bin/wine -> /opt/wine-altars/bin/wine
执行条目里的 Exec  ->  OpusApp 窗口 "Word" 出现，WM_CLASS = winword.exe
```

### "点了没反应"的另一半：Office 是单实例，它把点击交给了看不见的那个

换成绝对加载器路径之后，从开始菜单点 Word 仍然没反应，再点一次出安全模式提示。
这一次不是条目的问题——用一个包装器把桌面启动器真正传进来的参数抓下来，一切都是对的：

```
argv[0]=[env]
argv[1]=[WINEPREFIX=/home/user/.wine-altars-office]
argv[2]=[/opt/wine-altars/lib/wine/x86_64-unix/wine]
argv[3]=[C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Word.lnk]
DISPLAY=[:0]
```

反斜杠正确还原成单个，加载器是我们的，语言是 `0x804`（中文）。Word 进程也确实起来了，
然后**退出码 3，一个窗口都没有**。

先猜"残留的 wineserver 绑在别的显示上"，做实验**否掉了**：故意在 `:77` 留一个
wineserver，再从 `:0` 启动，窗口照样落在 `:0`（20 个）。Wine 是按进程各自的 `DISPLAY`
走的。

真正的原因是 **Office 的单实例移交**。先前的测试在 `:77` 上留下了一个还活着的
WINWORD.EXE；从 `:0` 点菜单时，新进程把请求交给那个已有实例、自己退出，而那个实例在
你看不见的显示上。构造实验验证：

| 状态 | 从 `:0` 点菜单的结果 |
| --- | --- |
| `:77` 上已有 Word（22 个窗口） | `:0` 上 **0 个**窗口，进程数仍是 1，退出码 3 |
| 全部清干净 | `:0` 上 **5 秒**出现 20 个窗口，连点两次都一样 |

第二次点出安全模式提示也随之而来：Word 看到连续几次"启动后没能正常运行"，就问要不要
安全模式——而选"否"之后新进程还是移交给那个旧实例，于是依然没反应。

教训是给调试用的：**在别的显示上跑测试，跑完必须把进程清干净**，否则下一次真实点击会
静默地交给那个残留实例。清理时 `pkill` 要用 `-x` 按进程名精确匹配（`-f` 会匹配到调用者
自己的命令行），而且进程名要列全：`wineserver`、`wineboot.exe`、`winedevice.exe`、
`services.exe`、`WINWORD.EXE` 等，少一个就还会有残留把 prefix 占住。

### 注销之后账户还在：它记在 OneAuth 缓存里，不在注册表

用户注销旧账户后，Word 再开还是显示那个账户并要求登录。逐步量下来：

**一、注销确实做了事，但做了一半。** 点「注销」→「注销并关闭」之后：

| | 之前 | 之后 |
| --- | --- | --- |
| `Identity\Profiles\<旧账户>_LiveId` | 在 | **已删除** ✓ |
| `ConnectedAccountCID` / `ConnectedOneAuthAccountId` | 指向旧账户 | **仍指向已被删除的那个** ✗ |
| broker 调用 | — | **一次都没有** |

`+reg` 追踪证实：整个注销过程 Office **对这两个值一次读写都没有**。它不打算清，
于是留下一个指向已删除 profile 的悬空指针。

顺带否掉一个看着很像的猜想：`IWebAccount2::SignOutAsync` 在 shim 里是 `E_NOTIMPL`，
看上去正是元凶——但追踪显示**注销根本不调它**（SignOut 相关行 0）。点「注销」时出现的
`onlineid:ticket_*` 是发给 `events.data.microsoft.com/OneCollector` 的**遥测**，不是注销。

**二、真正的症结是 OneAuth 缓存。** 从「文件 → 帐户 → 切换帐户」进去，Office 直接报：

```
帐户错误
很抱歉，当前无法访问您的帐户。要解决此问题，请重新登录。
你的帐户或设备出现问题。让我们来修复它。
```

这正是本文档很早就记过的那条症状。把 `OneAuth` / `IdentityCache` 移开（并清掉那两个
悬空指针）之后错误消失。**账户是记在 OneAuth 缓存里的，不在注册表**——把缓存放回去、
指针保持清除，登录页照样显示旧账户名，这就是证据。

**三、清干净之后，哪个入口能用。** 这一条对发布很要紧，因为**全新 prefix 本来就没有
这些缓存**：

| 入口 | 干净状态下的结果 |
| --- | --- |
| 首次运行对话框「登录或创建帐户」 | **可用**（真实鼠标点击，5 秒打开登录页） |
| 文件 → 帐户 → 登录 | **可用**，同样 5 秒，页面是空白的「电子邮件或电话」 |

**这里要更正我自己的一个错误结论。** 起初我判定首次运行那个按钮"无反应、是发布拦路虎"，
依据是 `uiclick` 点它之后 webauth / onlineid 两个通道一条调用都没有。那是**工具造成的
假象**：`uiclick` 用的是 `accDoDefaultAction`，而 Office 这个自绘的 NetUI 按钮对它
**回 S_OK 却什么都不做**。改用 `xdotool` 在同一个 `accLocation` 上发真实鼠标点击，
5 秒就打开了 `OneAuthWebView2Browser`。

顺带把同一段追踪里另一个吓人的东西也归位：那 835 次

```
err:ole:marshal_object Failed to create an IRpcStubBuffer from IPSFactory
for {00020404-...} with error 0x80004002
```

（`{00020404}` 是 `IEnumVARIANT`）看着像 COM 编组坏了，查注册表、查 oleaut32 的
`DllGetClassObject`、查生成的存根表，全是好的。上下文才说明问题：

```
fixme:oleacc:LresultFromObject unsupported wParam = ffffffff
fixme:ole:RemUnknown_QueryInterface No interface for iid {000209fa-...}
err:ole:marshal_object Failed to create an IRpcStubBuffer ...
```

`LresultFromObject` 是响应 `WM_GETOBJECT` 的无障碍接口——**那是我自己的 uidump/uiclick
在问 Office 要可访问对象**，跟登录路径毫无关系。

`uiclick` 因此加了 `-click`：跳过默认动作，直接点对象自报的位置。一个"报告成功却什么
都没做"的默认动作，比明确失败更坏——它会把你推去应用里找根本不存在的 bug。

### 登录框认错账户：线索在 Outlook 的邮件配置里，不在任何身份缓存

登录并激活之后，每次启动仍弹出另一个账户的登录框。查这个花了很久，因为**所有看起来
该负责的地方都是干净的**：

| 查过并排除的 | 结果 |
| --- | --- |
| `OneAuth/accounts/` | 只有一个账户，`email` / `login_name` / `account_hints` 全是正确的那个 |
| `OneAuth/blobs/` 里那个以旧邮箱命名的文件 | 删掉后**自己长回来**——它是结果不是原因 |
| `IdentityCache` | 整个移开，无变化 |
| WebView2 数据目录（67MB） | 整个移开、全新重建，无变化 |
| wininet 的 Cookie | 清空，无变化 |
| `Identity` 整棵子树 / `ServicesManagerCache` | 只有正确账户那一个 ID |
| `LanguageResources\LocalCache\<旧邮箱>` | 删掉，无变化 |

决定性的一步是去看 **Office 请求的 URL**。WebView2 的 `History` 里存着它：

```
login.microsoftonline.com/...?...login_hint=<旧账户>%40outlook.com
login.live.com/...?...login_hint=<旧账户>%40outlook.com
```

**是 Office 自己把这个地址当 `login_hint` 传出去的**——所以它一定存在本地。但全 prefix
搜明文只命中 WebView2 的 History 和 Favicons，都是导航留下的痕迹。

漏掉的是**注册表里的二进制值**：`user.reg` 把 REG_BINARY 写成十六进制字节，搜
账户名的明文自然搜不到，要搜它 UTF-16LE 的十六进制形式（每个字符两个字节，如 `x` 是
`78,00`）。一搜就中：

```
HKCU\Software\Microsoft\Office\16.0\Outlook\Profiles\Outlook\caa1c55647359940981dcf71a08288f2
  值 001f3d16
```

那是一个 **Outlook 的 MAPI 邮件配置条目**，配着旧账户——随 `import-office.sh` 的
`Outlook\Profiles` 一起从原机搬过来的。Word 读 Outlook 的默认配置来决定用户身份，于是
把它当成了登录提示。删掉那一个条目之后：

```
登录/对话框窗口: 0 个
<显示名> (<当前账户>) 已登录
```

教训有两条，都值得记：

- **搜注册表要连二进制形式一起搜。** 明文搜索在 `user.reg` 上会漏掉所有 REG_BINARY，
  而账户、配置这类东西恰恰常存在二进制值里。
- **先看应用发出去的请求，再去猜它从哪读的。** 这一轮前面清了五处存储、每次都没效果；
  真正定位是靠 URL 里的 `login_hint`——它直接证明了"值在本地"，把范围从"哪里存着旧
  账户"缩小成"哪个二进制值里有这个字符串"。

**还有一条是我自己制造的故障，记下来免得重犯。** 上面那串排除实验里，我把
`IdentityCache` 移开之后没有放回去，以为 Office 会重建——它没有。结果下一次启动
Office 报「你的帐户或设备出现问题」要求修复：账户记录在 `OneAuth` 里好好的，而令牌
材料（RT / AT / ID / Accounts）在 `IdentityCache` 里，被我拿走了。**这两个目录是同一
次登录写出的一套东西**，时间戳都是同一秒；只留一半，Office 认得出账户却取不到令牌。
把原样备份放回去即刻恢复。

排除实验每移走一样，都要在下一步开始前放回原处——尤其是这种成对的状态。备份都在
`$WINEPREFIX/identity-repair-*/`。

### 一次闪退，以及菜单启动带来的一个意外好处

从开始菜单启动的 Word 跑了 26.8 秒就没了。**证据全在 journal 里**——这是把
`.desktop` 交给 Wine 自己生成之后的副产品：桌面启动的程序在 systemd scope 下运行，
Wine 写到 stderr 的一切都进了用户 journal，不需要事先套任何包装脚本。

```
Sep 19 06:30:59  Word.desktop[3717731]: ... 启动
Sep 19 06:31:19  Word.desktop[3717731]: 033c:fixme:winhttp:request_query_option unimplemented option 77
Sep 19 06:31:19  Word.desktop[3717731]: 0340:fixme:winhttp:request_query_option unimplemented option 77
Sep 19 06:31:19  Word.desktop[3717731]: 0200:err:seh:NtRaiseException Unhandled exception code 1e044000
Sep 19 06:31:24  systemd: app-gnome-wine\x2dPrograms\x2dWord-3717613.scope: Consumed 20.177s CPU ... 857.6M peak
```

`0x1e044000` 是 Office 自己的 fail-fast 码（同族的 `0x1e3c3840` 本文前面记过）。
紧邻的两条 FIXME 是 `WINHTTP_OPTION_AUTOLOGON_POLICY`（77）。

查下来那是 Wine 的一个实打实的缺陷，而且形状很清楚：这个选项**设得进、读不出**。
`request_set_option` 把它存到 `object_header.logon_policy`，而 `request_query_option`
没有对应的 case，落到 `default:` 返回 `ERROR_INVALID_PARAMETER`。值就在那里等着被读。
补上之后 `+winhttp` 下已经查不到 `unimplemented option 77`。

**但因果没有证实。** 闪退只出现过一次，我没能复现；"查询失败"与"fail-fast"只是相邻，
不是被证明的因果。这个选项本身错在它自己的形状上，值得修——但不能因此宣称闪退已解决。
再遇到闪退，直接从 journal 捞 `Word.desktop` 的输出，比任何猜测都快。

## 从 C2R 装一遍：安装器自己能不能在 Wine 上跑完

到这里为止，这个 prefix 里的 Office 是从一台真实 Windows 机器导入的。那条路能走通，
但对别人没用——要发布给公众，得能从微软官方的安装器装出来。所以开一个全新 prefix，
拿 Office 部署工具（ODT）的 `setup.exe` 直接 `/configure` 一遍，看它卡在哪。

### 先拿到安装器

`download.microsoft.com` 在这台机器上 TLS 握手失败：它的证书链要 `Microsoft TLS RSA
Root G2`，本机的发行版根证书包里没有。**没有为此装任何根证书**——换 CDN 即可：

```
https://officecdn.microsoft.com/pr/wsus/setup.exe      6.9M
```

配置只装 Word（`ExcludeApp` 掉其余全部），`Display Level="None"`，`Channel=Current`，
`zh-cn`，64 位，日志写到 `C:\c2rlog`。

### 第一次：wintrust 在清理时写进了别人的内存

装到一半整个卡死，`wine` 打出一个页错误：

```
wine: Unhandled page fault on write access to 00006FFFF9BDCFC0 at address 00006FFFF92EB8A4
wine:   00006FFFF92EE235  L"wintrust.dll" +0xe235
wine:   00006FFFF9BE499E  L"crypt32.dll" +0x4499e
```

把 VMA `0x18000b8a4` 反汇编出来是 `movq $0xffffffffffffffff,0x10(%rax)`，在
`SoftpubCleanup` 里，就是这一句：

```c
CloseHandle(data->pWintrustData->pFile->hFile);
data->pWintrustData->pFile->hFile = INVALID_HANDLE_VALUE;   /* 这里 */
```

`WinVerifyTrust` 的 `WTD_STATEACTION_CLOSE` 自带一个 `WINTRUST_DATA`，而
`WINTRUST_DefaultClose` 清理时用的还是当初 VERIFY 那次的指针。调用方在 verify 返回
之后完全有权把那块结构释放或复用掉，于是这一写就落到了现在占着那块内存的东西上。
改成用 close 这次传进来的结构——**调用方此刻担保的就是它**——之后同样的安装零崩溃。

### 第二次：不崩了，但一分钟后回滚

`setup.exe` 退出码 108，prefix 只长了 184MB。注册表说得很清楚：

```
HKLM\SOFTWARE\Microsoft\Office\ClickToRun
  LastScenario        REMOVEINSTALLATION
  LastScenarioResult  Failure
  ScenarioLastError   1
```

C2R 客户端本身是装成功的（`ApiClient.dll`、`AppV*.dll` 齐全），日志也在
（`C:\windows\WINHOST-*.log`，UTF-16LE）。里面有三类扎眼的东西，**其中两类是幌子**：

- `Transport::Connect ... 0x80040154 "Unable to create DeliveryOptimization instance."`
  ——Delivery Optimization 是 Windows 的下载 COM 服务，Wine 没有。但 C2R 有
  `FailOverTransport`，它退回去用别的传输，下载照样完成了。
- `CabManager::ValidateCatalogFile` 错误 188。日志自己下一行就写着
  `"but catalog errors are non-fatal, continuing"`。

真正致命的是第三条，在 repoman 把 10205 个下载任务全部算完之后：

```
C2R::RepomanPipeline::OpenStreamSession  ErrorCode 30029  InvalidArgument
  Failed to get path root %ProgramFiles%\Microsoft Office\Updates\Download\
  PackageFiles\116F7616-..., driveNumber=-1
```

### `driveNumber=-1` 是个能直接读懂的数字

二进制里的格式串是 `Failed to get path root %s, driveNumber=%d`。带 driveNumber 的
「取路径根」只可能是 `PathGetDriveNumberW`——它对 A–Z 返回 0–25，失败返回 -1。
喂给它的字符串以 `%` 开头，当然不是盘符。

也就是说 C2R 把**没展开的** `%ProgramFiles%\...` 直接当路径用了。注册表里确实如此：

```
Scenario\INSTALL  PipelineInstallPath   = %ProgramFiles%\Microsoft Office
                  PipelineDownloadPath  = %ProgramFiles%\Microsoft Office\Updates\...
```

而从真实 Windows 导入的那个 prefix 里，同样两个值是 `C:\Program Files\Microsoft
Office`。**所以这两个值本该在写入时就展开好**，不是 C2R 的存储约定。

### 谁写的，以及为什么没展开

`PipelineInstallPath` 这个字符串只出现在一个二进制里：`OfficeClickToRun.exe`，
也就是 **ClickToRun 服务**。

开 relay（只跟 `ExpandEnvironmentStringsW`、`RegGetValue*`、`RegSetValue*`）跑一遍，
看到两件事：

- C2R 把**每一个**要写进注册表的值都过一遍 `ExpandEnvironmentStringsW`——
  `"zh-cn"`、`"CDN"`、`"TASKSTATE_EXECUTING"` 都过。所以没展开只能是展开失败。
- 32 位的 `setup.exe` 用的是 `%ProgramW6432%`（返回 17 个字符 = `C:\Program Files`），
  它很清楚 32 位进程里 `%ProgramFiles%` 会指到 `(x86)` 去。

关键的一次对照出在 relay 脚本本身的一个差别上：某一轮我在安装前 `wineserver -k` 了一下，
那一轮服务写进去的是 `RegSetValueExW(..., "PipelineInstallPath", ..., cbData=0x44)`
——68 字节 = 33 个字符加结尾 NUL = `C:\Program Files\Microsoft Office`，**展开好的**。

### 根因：服务拿到的环境，是 prefix 还没建好时的那一份

装一个只做 `cmd /c set > 文件` 的服务，两种条件下各跑一次：

| 条件 | 服务进程的环境变量 | `ProgramFiles` |
|---|---|---|
| 新建 prefix 后直接起服务 | 18 个 | 没有 |
| 新建 prefix 后先重启 wineserver | 43 个 | `C:\Program Files` |

18 个那一份里，**一个来自注册表的变量都没有**——没有 `PATH`、没有 `OS`、没有
`windir`、没有 `ProgramData`、没有 `USERPROFILE`。

代码里的时序对得上。`programs/wineboot/wineboot.c` 的 `main`：

```
start_services_process();                  /* services.exe 在这里起来 */
if (init || update) update_wineprefix( update );   /* wine.inf 在这之后才跑 */
```

新建 prefix 时，services.exe 启动的那一刻，`Session Manager\Environment` 和
`CurrentVersion\ProgramFilesDir` 都还不存在。而 `programs/services/services.c` 把
`CreateEnvironmentBlock` 的结果**存进一个静态变量**，此后这个 services.exe 启动的
每一个服务都拿这一份。只有重启 wineserver 才会换掉它。

于是链条完整了：新建 prefix → 立刻装 Office → ClickToRun 服务的环境里没有
`ProgramFiles` → 它把 `%ProgramFiles%\Microsoft Office` 原样写进注册表 →
`PathGetDriveNumber` 返回 -1 → 30029 → 10205 个任务全部作废，整个安装回滚。

**而"新建 prefix 之后紧接着装东西"正是所有安装脚本的写法。**

### 修法

把环境块改成每次启动服务时现建，不再缓存。这也是 Windows 的行为——改了系统环境变量，
服务重启就能拿到新值，不需要重启服务管理器；服务启动本来就稀疏，代价可以忽略。

改完之后，同一个对照实验里「新建 prefix 后直接起服务」拿到 43 个变量，`ProgramFiles`、
`ProgramW6432`、`ProgramData`、`PATH`、`windir` 全在——**没有重启 wineserver**。

### 装得动之后：为什么还这么慢

安装能跑通之后，下载速度成了下一个问题。这一段的结论分两半，一半是环境，一半是 Wine。

**环境那一半**：本机所在的网络，路由器跑 passwall2。查下来分流是
对的——`microsoft.com` 在 DNS 规则和路由规则里都命中 `direct`，`route.final` 也是 `direct`，
没有被代理绕出国。但 C2R 的日志里只有两个主机：

| 主机 | 用途 | 落点 | RTT |
|---|---|---|---|
| `officecdn.microsoft.com` | 元数据 | `...wyy.ctlcdn.cn` → 106.41.206.x | 3.0 ms |
| `f.c2r.ts.cdn.office.net` | **payload** | `office.microsoft.map.fastly.net` | 205.8 ms |

真正下载 payload 的是第二个，它**不匹配**直连关键字表里的 `microsoft.com`，CNAME 一路落到
境外。而那个电信节点接受 `Host: f.c2r.ts.cdn.office.net` 并正常返回 206，所以在路由器上加了

```
uci add_list dhcp.@dnsmasq[0].address='/c2r.ts.cdn.office.net/106.41.206.187'   # 以及 .188 / .98
```

**一个走错的路**：中途试过用 `address=/c2r.ts.cdn.office.net/::` 压掉 AAAA，那是错的——`::`
会被当成本机回环，curl 连到本机服务拿到 404。已撤销。AAAA 仍回 Fastly 的 v6，而本机没有
IPv6 默认路由，这条还没解决。

**Wine 那一半才是重点**。换到 3 ms 的国内节点后仍然只有 100–200 KB/s，说明瓶颈不是距离。
安装器全程只开**一条** TCP 连接——因为它本该用的两条快路都断了：

```
Data.FailOver_1: "DOStreaming"     0x80040154  Unable to create DeliveryOptimization instance
Data.FailOver_2: "BITS"            0x80200003  Unable to resume job
                                               "Ran out of sources to retry from."
```

Delivery Optimization 是 Windows 的下载服务，Wine 没有，这个短期内也不会有。但 BITS
有——`dlls/qmgr` 是完整的一份实现。查下去，缺的只有一个方法：

```c
static HRESULT WINAPI BackgroundCopyJob_AddFileWithRanges(...)
{
    FIXME("...: stub\n");
    return S_OK;          /* 返回成功，但什么都没加 */
}
```

C2R 用 `IBackgroundCopyJob3::AddFileWithRanges` 加文件（它要按字节段取一个几 GB 的 stream
文件），Wine 装作成功却没往 job 里放任何东西。紧接着 `Resume()`：

```c
else if (job->jobProgress.FilesTransferred == job->jobProgress.FilesTotal)
    hr = BG_E_EMPTY;      /* 0 == 0 */
```

`BG_E_EMPTY` 就是 `0x80200003`——日志里那个码。**一个"成功"的空实现，在两层之外变成一个
看不出来路的错误码。**

`tools/bitsprobe` 按 C2R 的调用序列复现了它，前后对照：

```
修复前   AddFileWithRanges 0x00000000   Resume 0x80200003
修复后   AddFileWithRanges 0x00000000   Resume 0x00000000   状态 TRANSFERRED
```

修复是让它真的把文件加进 job 并带上范围，`transfer_file_http` 对每个范围发一次带
`Range:` 头的请求、按偏移落盘。顺带把 `GetFileRanges` 从 `E_NOTIMPL` 实现掉，并在
`hresult_from_http_response` 里点名 206——带范围请求的成功码本来就是它，不该落到 FIXME。

验证用真实 CDN：请求偏移 1000 的 4096 字节和偏移 500000 的 8192 字节，总共传了
**12288 字节**（不是 366 MB 的整个文件），本地文件 **508192 字节**（= 500000 + 8192），
`GetFileRanges` 回来的两段与请求一致。

### BITS 这条路我走错了，四个改动最后全部撤回

这一段记录一次**完整的失败**，因为它的教训比成功的那几条更有用。

C2R 的传输失败序列是 `DOStreaming → BITS → 自己的单流 HTTP`。Delivery Optimization
Wine 没有（也不该在这里补），但 BITS 有——`dlls/qmgr` 是完整实现。查下去，缺的只有一个方法：

```c
static HRESULT WINAPI BackgroundCopyJob_AddFileWithRanges(...)
{
    FIXME("...: stub\n");
    return S_OK;          /* 返回成功，但什么都没加 */
}
```

C2R 用 `IBackgroundCopyJob3::AddFileWithRanges` 加文件，Wine 装作成功却没往 job 里放
东西，紧接着 `Resume()`：

```c
else if (job->jobProgress.FilesTransferred == job->jobProgress.FilesTotal)
    hr = BG_E_EMPTY;      /* 0 == 0 */
```

`BG_E_EMPTY` 就是日志里那个 `0x80200003`。把它实现掉之后，`tools/bitsprobe` 验证完全正确：
请求偏移 1000 的 4096 字节和偏移 500000 的 8192 字节，实际只传 **12288 字节**，本地文件
**508192 字节**，`GetFileRanges` 与请求一致。

**然后 Office 就装不上了。**

同一个干净 prefix、其它条件全同，只差这一个提交：

| `AddFileWithRanges` | 结果 |
|---|---|
| 实现掉 | 42 分钟没装上 C2R 客户端；日志停在 `StartDownload`（`"Time to start job 185ms"`，文件 `v64_*.cab`），`JobTransferredOrCompleted` **从未出现**，没有临时文件、没有 socket——job 躺在队列里没被取走 |
| **保持 stub** | **30 秒装好客户端**，`Office\Updates` 建出来，继续下载 |

排除过的：不是通知（早已撤销，没有它照样挂）；不是 CDN 的 DNS 覆盖（撤掉无变化）；
不是崩溃（无异常，setup.exe 在 5 秒 `GetTickCount64`/`Sleep` 重试循环里）。

**一个"成功的空实现"在这里反而是对的**：它让 C2R 立刻失败并正确退路，安装得以完成。
把它实现掉，C2R 就改走 BITS，而 Wine 的 BITS 在这个负载下会停摆。

所以这一轮 qmgr 的四个提交（`AddFileWithRanges`、`GetFileRanges`、206、以及三版通知）
**全部 `git revert`**，qmgr 回到基线，一行未改。留下的是 `tools/bitsprobe` 和这段记录。

### 下次接着查 BITS，该从哪儿下手

- `dlls/qmgr/qmgr.c` 的队列是**单线程**：`processJob` 在它上面同步执行，
  `wait_for_completion` 用 `INFINITE`。任何一次传输卡住，后面所有 job 永远排不上。
- `SetNoProgressTimeout` qmgr 存下来了，**从不使用**。
- 真正没搞清的是：探针 `Resume` 的 job 会被取走，C2R `Resume` 的不会。差别可能在
  公寓模型、在跨进程代理、也可能在 C2R 同时持有多个 job。

**还有一条关于测试的教训，比这个 bug 本身更值钱。** `bitsprobe` 每一版都通过：
12288 字节、退出码 0。它恰恰是唯一复现不出问题的调用方——**它在 MTA 里轮询 `GetState`，
而 C2R 在 STA 里阻塞等待，并且一次跑多个 job**。探针能验证"范围下载是对的"，
验证不了"让这条路生效之后整体还能不能跑"。**功能正确 ≠ 集成安全**，这两件事要分开测，
而唯一能测第二件的是真实负载。

### 第四件：`0xc06d007f`，以及两个诊断脚本骗了我一次

关掉 Mono 之后，`setup.exe` **自己**崩了，退出码 84，安装日志只有五行：

```
wine: Unhandled exception 0xc06d007f in thread 268 at address 7B5C92FB
WineDbg attached to pid 01bc
EXIT=84
```

`0xc06d007f` 不是随便一个码。`VcppException(sev, err) = 0xC06D0000 | err`，
`err = 0x7F = 127 = ERROR_PROC_NOT_FOUND`——**VC++ 延迟加载助手找不到某个导出函数**。
（`0xC06D007E` 是找不到模块。）延迟加载绑不上不会返回错误，它直接抛异常，没人接就死。

**先绕了一圈**：我拿 `scripts/pe-delay-imports.py` 列出 setup.exe 的延迟导入，
再用 `scripts/check-wine-exports.py` 检查，得到"共 0 项缺失"。两个问题：

- `check-wine-exports.py` 的第一个参数是 **Wine 的 DLL 目录**，清单从 stdin 读。
  我传了个 .exe，stdin 是空的——于是它老老实实报了 0，看起来和"体检通过"一模一样。
- `pe-delay-imports.py` 每个 DLL 只打前 6 个函数，后面写 `...`。ole32 那一行正好
  被截断，**`CoCancelCall` 排在第七个**。

两个脚本都改了：前者参数不对或 stdin 是终端就报错退出，遇到被截断的行会警告；
后者默认打全，`-s` 才给摘要。**一个静悄悄返回 0 的诊断工具比没有工具更糟。**

**直接的办法**：给 `GetProcAddress` 开 relay，看崩溃线程最后问了什么。

```
0278:Call KERNEL32.GetProcAddress(79ff0000,"CoCancelCall") ret=00809943
0278:Ret  KERNEL32.GetProcAddress() retval=00000000
wine: Unhandled exception 0xc06d007f in thread 278 ...
```

线程号对得上。Wine 的 ole32 导出了 `CoEnableCallCancellation` 和
`CoDisableCallCancellation`，**唯独漏了这一组里的第三个 `CoCancelCall`**；combase 里
它只是 `@ stub`，没有任何 DLL 以调用方 import 的那个名字导出它。

补上之后 `ole32!CoCancelCall` 能解析到，返回 `RPC_E_CALL_COMPLETE`（0x80010117）。
这个返回值是诚实的：Wine 不跟踪未完成的调用，**"那个调用已经结束了"就是实情**，
而不是敷衍——Windows 在被取消的调用已经返回后也是这么答的，调用方认得这个码。

### 顺带修掉一个把崩溃变成卡死的设置

上面那次崩溃之所以让安装**卡住**而不是干脆失败，是因为 `winedbg --auto` 弹了一个
"Program Error" 对话框在等人点。实测：

```
无 DISPLAY    崩溃后 2 秒退出
有 DISPLAY    挂满 60 秒（超时才结束），xdotool 能看到窗口 "Program Error"
```

`/proc/<pid>/wchan` 是 `anon_pipe_read`。这是 Wine 的既定行为，不是 bug，但对无人值守
安装是致命的——任何一个子进程崩溃都会把父进程一起堵死。无人值守时应该关掉：

```sh
wine reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f
```


## 从 C2R 装一遍：结果

修完 wintrust、services.exe 的环境块和 `ole32!CoCancelCall` 之后，ODT 的 `setup.exe`
在一个全新 prefix 里跑到了这个地步：

```
C:\Program Files\Microsoft Office\
  root\Office16\WINWORD.EXE          1,557,304 字节
  root\Office16\                     282 个文件，1.7 GB
  root\vfs  root\vreg                都在，结构与真机导入的一致
  root\                              3.3 GB
  AppXManifest.xml  PackageManifests  Updates
```

**而且它能启动**：直接跑 `WINWORD.EXE`，窗口起来了，标题是「Microsoft 365 和 Office」
（首次运行的登录/激活页）。

`setup.exe` 本身的退出码是 106，因为最后一步没做完：

```
Office.ClickToRun.C2RClient.OfficeVirtRegistration
  Result.Code 30088  UnexpectedError
  Data.PrevVirtBeforeRegistration    "None"
  Data.CurrentVirtBeforeRegistration "AppVReady"
  Data.ErrorMessage "Orchestration::UpdateAppV: TryRefreshMergedManifest failed"
```

注册表把这一步的性质说得最清楚：

```
C2R 装的 prefix:   PrevVirtualizationMechanism=None       VirtualizationMechanism=AppVReady
真机导入的 prefix: PrevVirtualizationMechanism=AppVReady  VirtualizationMechanism=AppVRegistered
```

`AppVReady → AppVRegistered` 这一跃没有完成。而这正是 `scripts/apply-office-registry.sh`
和 `import-office.sh` 在导入路径上**手工做掉的那件事**——把 `vreg` 导进 HKLM、把 `vfs`
摊到它本该被投射到的位置。所以 C2R 装出来的树是完整的，缺的只是注册。

### 差的那一步，根因查到了

把 `WINEDEBUG` 写进 `HKLM\System\CurrentControlSet\Control\Session Manager\Environment`
（这样 `CreateEnvironmentBlock` 会把它发给服务进程——本文前面那条发现的第三次应用），
带 `+msxml` 再跑一遍，抓到两条：

```
584 × warn:msxml:doparse Namespace prefix appv is not defined
  1 × warn:msxml:node_remove_child childNode 00007CD54A2FA7F0 is not a child of 00007CD54A2FA6D0
```

第二条就是 `removeChild` 返回 `E_INVALIDARG` 的那一处，在 `dlls/msxml3/node.c`：

```c
if(child_node->node->parent != This->node)
{
    WARN("childNode %p is not a child of %p\n", child, This);
    return E_INVALIDARG;
}
```

第一条解释了为什么会这样。`AppXManifest.xml` 的根元素**是**声明了
`xmlns:appv="http://schemas.microsoft.com/appv/2010/manifest"` 的，全文 616 处 `appv:`。
但 C2R 合并清单时处理的是**片段**，片段里没有根上的那行声明——于是 libxml2 报"前缀未定义"，
**丢掉前缀**继续；而 MSXML 遇到未声明前缀会把 `appv:Foo` 整个当作节点名保留下来。
两边建出的树不一样，C2R 手里的节点在 Wine 的树里就不是它以为的那个父节点的孩子。

下一步该做的是把这个差异做实：拿一个带未声明前缀的片段，比对 MSXML 与 Wine 的
`nodeName` / `prefix` / `namespaceURI` / `parentNode`，再决定是在解析时保住前缀，
还是在 `removeChild` 上放宽父子判定。

### 把 `removeChild` 那一处再挖了一层，然后停手

`tools/xmlprobe` 按 C2R 的用法复现了几种情形，Wine 下的结果：

| 情形 | `nodeName` | `prefix` |
|---|---|---|
| 前缀已声明 | `appv:TargetOSes` | `appv` |
| **前缀未声明** | **`TargetOSes`** | **`<null>`** |

**Wine 会把未声明的前缀整个丢掉**（libxml2 报 `XML_NS_ERR_UNDEFINED_NAMESPACE` 后按
无前缀恢复）。这解释了那 584 条警告的后果，但**不是**这次失败的直接原因——往下挖，
时序才是关键。

`+msxml` 的完整调用序列（重复了 16 次，形状完全一致）：

```
elem.selectSingleNode("//appv:Extensions")   -> found 1 matches   拿到节点 N
  ... 中途 someParent.replaceChild(新, N)     -> N 被摘出去
elem.selectSingleNode("//appv:Extensions")   -> found 0 matches
  ...
elem.removeChild(N)                          -> E_INVALIDARG
```

两点要注意：

- **`//` 是从文档根开始搜的**，不管在哪个节点上调用。所以 `elem.selectSingleNode("//…")`
  返回的节点**本来就未必是 `elem` 的孩子**。
- Wine 的 `create_node` 每次都新建一个包装对象，所以日志里不同的 `domelem` 指针
  可能包着**同一个 libxml2 节点**。不能按指针判断"是不是同一个元素"。

C2R 拿的是第一次查询的结果，而它在中途已经被 `replaceChild` 摘走了。**在 Windows 上
这一步为什么不失败，我没有证据。** 可能是 MSXML 的 `replaceChild` 语义不同、可能是
两次查询返回的节点不同、也可能是 C2R 在 Windows 上根本走的另一条分支。

**要往下走，需要一台真 MSXML 来对照**：同一份清单、同一串 `SelectionNamespaces`、
同一组调用，比对 `selectSingleNode` 返回哪个节点、`replaceChild` 之后 `parentNode`
是什么、`removeChild` 返回什么。`tools/xmlprobe` 就是为此写的，拿到 Windows 上直接跑。

在有对照之前不动 `dlls/msxml3`。本轮已经因为"探针通过就宣称修好"在 BITS 上栽过一次，
同样的错误不再犯第二遍。

### 而且这一步未必非修不可

Word 已经能从 C2R 装出来的树里启动。App-V 注册做的事情——把 `vreg` 导进 HKLM、
把 `vfs` 摊到该去的位置——正是 `scripts/apply-office-registry.sh` 和 `import-office.sh`
在导入路径上手工做掉的。C2R 装出的 `root\vfs`、`root\vreg` 结构与真机导入的一致，
所以补完这一步不一定要经过 MSXML。

### 想绕过 App-V 注册，绕不过去

既然 `scripts/export-office-registry.py` 本来就会读 `root/vreg/*.vreg.dat`（标准 regf
蜂巢）并做 App-V 路径令牌替换，那就给它加个 `--vreg-only`，直接对 C2R 装出的 prefix 用——
Wine prefix 没有真机的 SOFTWARE 蜂巢和 NTUSER.DAT，原来的流程走不下去。

跑出来是 **0 个键**。原因一看文件大小就明白：

| | vreg 文件 | 总大小 |
|---|---|---|
| C2R 装的 | 15 个，**每个都是 8192 字节** | 124 KB |
| 真机导入的 | 23 个，64 KB–1 MB 不等 | 11 MB |

8192 字节是**空 regf 蜂巢的最小尺寸**。C2R 把壳创建出来了，内容是
APPLYCONFIGURATION 阶段写进去的——而那一步正是失败的那一步。

所以链条是闭合的，没有旁路：

```
清单合并 removeChild 失败 → APPLYCONFIGURATION 失败 → vreg 是空壳
                                                   → 什么都没注册
```

数字上的差距：

| | C2R 装的 | 真机导入的 |
|---|---|---|
| `Word.Application` ProgID | **0** | 7 |
| HKLM Office 键 | **23** | 42085 |
| Classes 键 | 15353 | 38194 |
| `system.reg` | 3.9 MB | 21 MB |

Word 能启动，是因为它从自己的目录里跑；但没有 ProgID、没有文件关联、没有 COM 自动化。
**要让 C2R 装出可用的 Office，必须修掉那个 `removeChild`** —— 而修它需要一台真 MSXML
来对照，`tools/xmlprobe` 已经为此写好，拿到 Windows 上直接跑就是。

## App-V 注册跨过去了：三个 msxml 缺陷，一层压一层

上一节停在「要一台真 MSXML 才能往下走」。后来发现**真机产物本身就是对照物** ——
导入的 prefix 里有一份 Windows 的 C2R 亲手写出的 `AppXManifest.xml`。照着它一层层剥，
三个缺陷全部定位并修好，App-V 注册从此完成。

### 一、未声明的前缀被丢掉

C2R 合并清单时用 `loadXML` 加载片段，片段长这样（trace 原样打印的）：

```xml
<appv:Extensions>
	<appv:Extension Category="AppV.FileTypeAssociation">…
```

**没有 `xmlns:appv` 声明。** libxml2 的 SAX1 路径建了一个带前缀、`href` 为 NULL 的
`xmlNs` 挂到 nsDef 上，然后因为这个守卫从不把它设成元素的命名空间：

```c
if ((ns != NULL) && (ns->href != NULL) && ...) xmlSetNs(ret, ns);
```

守卫上面的注释说的是**默认命名空间**未绑定时该留 NULL；具名前缀不该按这个处理——
丢掉前缀等于把元素改了名。

**真机对照**（同一份清单，Word-only 对四件套，看形状不看数量）：

| | 真机 Windows | Wine 修前 | Wine 修后 |
|---|---|---|---|
| `<appv:Extension>` | 3704 | **0** | **630** |
| `<Extension>`（丢了前缀的） | 0 | **630** | **0** |
| 合并后清单 | 5.1 MB | 596 KB | 754 KB |

### 二、插入文档时不绑定命名空间

前缀保住了还不够。真机清单里那个插入的元素是这样的：

```xml
<appv:Extensions xmlns:appv="http://schemas.microsoft.com/appv/2010/manifest">
```

**它带着自己的声明。** 而喂进去的片段没有，trace 里也没有任何 `setAttribute` ——
只能是 MSXML 在节点插入文档时把前缀绑到了目标文档的声明上，绑定了序列化时才会再写一次。

Wine 不做这步，于是链条是：

```
插入后节点没有 URI → 第二次 selectSingleNode("//appv:Extensions") 找不到
                  → C2R 回退去删那个已被 replaceChild 换走的旧节点
                  → removeChild E_INVALIDARG → 30088
```

补上之后 `tools/xmlprobe` 实测：同样的片段放进声明了该前缀的文档，再查回来，
**S_FALSE 变成 S_OK**，XSLPattern 与 XPath 都是。

### 三、XSD 的 `\uXXXX` 转义

清单合并通过后，错误前移到 App-V 自己的包配置：

```
OISVAPI::ConfigurePackage Failed. publishScope = 1
  [AppV Error: Code(0x3e504e25-0x80004005) … File(xmlutils.cpp : 624)]
错误 30175
```

`+msxml` 抓到真因：

```
err:msxml:Schema_parse error code 1756: Element '{…XMLSchema}pattern':
  The value '[^﷐-﷯￹-￿\p{IsPrivateUse}]+' … is not a valid regular expression.
fixme:msxml:cache_entry_from_xsd_doc failed to parse doc
```

**XML Schema 正则没有数字转义**——文法里只有 `\n`、`\r`、`\t`、标点转义、字符类简写和
`\p{…}`。MSXML 额外接受 `\uXXXX`，照着它写的 schema 就这么用。libxml2 按规范拒绝该面，
**整个 schema 就加不进去**，App-V 拿不到校验用的 schema，配置包失败。

改法是在交给 schema 解析器的那份副本上把 `\uXXXX` 换成它表示的字符。`U+0000` 换成
`U+0001`——C 字符串装不下 NUL，而对一个匹配 XML 内容的模式来说两者无从区分，XML 本来
就不能含这两个字符中的任何一个。

**只改这一处，同一个安装续跑的结果**：

```
VirtualizationMechanism   AppVReady  ->  AppVRegistered
HKLM Office 键            23         ->  2902
setup.exe                 exit 106   ->  exit 5
```

`AppVRegistered` 正是真机导入的那个 prefix 里的值。

### 现在卡在哪

几乎所有任务都是 `TASKSTATE_COMPLETED`：`STREAM`、`APPLYCONFIGURATION`、
`INTEGRATE_INSTALL`、`STAGEREGISTRY`、`FONTS`、`MIGRATE`、`UNINSTALLCENTENNIAL`。
外层 `SCENARIO`/`BRANCH`/`GROUP` 还停在 `EXECUTING`，因为 `OfficeClickToRun.exe` 崩了：

```
Unhandled page fault on read access to 0  at OfficeClickToRun.exe+0x75707c
rax=0x80004002 (E_NOINTERFACE)  rcx=0
```

反汇编那一段：

```asm
lea  rdx,[rip+0x30a288]   # IID {0847E909-53CD-4E4F-832E-57D180F6E447}
mov  rax,[rax]            # vtbl[0] = QueryInterface
call [rip+0x8774f]
mov  rcx,[rbp+0x28]       # out 参数，失败时仍是 NULL
mov  rax,[rcx]            # ← 崩在这里，不检查返回值
```

那个 IID **只出现在 `OfficeClickToRun.exe` 自己里**——是 C2R 的私有接口，在它自己的
对象上查的，不是 Wine 缺的东西。说明更早某一步给了它一个类型不对的对象。

可用性：Word 能启动，主窗口标题 `Microsoft Word 16.0`；但它报
「此功能看似已中断，并需要修复」——注册仍不完整（HKLM Office 键 2902，真机 42085）。

顺带抓到但尚未定性的两条：

- `marshal_object Failed to create an IRpcStubBuffer from IPSFactory for
  {659cdeac-…}`（`IBackgroundCopyCallback2`）。代理 DLL 里**有**这个 IID，
  `CStdStubBuffer_Construct` 也成功了，失败在其后对服务端对象的 QI——
  `SetNotifyInterface` 先问 Callback2、不成再退回 Callback，这条 err 可能只是噪音。
- `apartment_add_dll couldn't load in-process dll msoxmlmf.dll`（64 次）——
  那个文件在 prefix 里根本不存在。

## 那个 IID 是 `IPackageManager6`，前面判断错了

上一节说「那个 IID 只出现在 `OfficeClickToRun.exe` 自己里，是 C2R 的私有接口」——不对。
当时只在 prefix 的一部分目录里搜了，而且搜错了前缀（搜的是 `~/.wine-office`，
C2R 装的是 `~/.wine-c2r-test`）。重搜一遍，`{0847E909-53CD-4E4F-832E-57D180F6E447}`
在三个文件里各出现一次：`OfficeClickToRun.exe`，以及两份
`Microsoft.WindowsAppRuntime.dll`。在后者的 `.rdata` 里它夹在一张 IID 表中间，
前后是 `IPackageManager3`、`IPackageManager2`、`IPackageManager5`。

还有一个更快的判据我一开始没用上：GUID 第三段的首字符是版本位。参数化的
`IAsyncOperationWithProgress<T,P>` PIID 是 SHA-1 派生的，版本位必然是 `5`；
这个 IID 是 `4e4f`，版本位 `4`，即手工分配的固定接口 IID。我却因为它在
C++/WinRT 二进制里紧邻 `IAsyncOperationWithProgress` 的模板符号，就先入为主
判成了 PIID，白绕一圈。

对上 `microsoft/windows-rs` 生成的绑定，答案是
**`Windows.Management.Deployment.IPackageManager6`**，再用 `tpn/winsdk-10` 里
10.0.16299.0 的 `winrt/windows.management.deployment.idl` 原文核对：contract 5.0，
uuid 一致，六个方法，第一个正是 `ProvisionPackageForAllUsersAsync`——
vtable 槽位 6，`mov rax,[rax+0x30]` 取的就是它。

### 完整的崩溃链

LASTRUN 要把 Office 的 MSIX 附加组件集成进来。它做的事，按日志顺序：

```
FindPackagesByPackageFamilyName(Microsoft.WritingAssistant_8wekyb3d8bbwe)   E_NOTIMPL
FindPackagesByPackageFamilyName(Microsoft.Outlook.UriHandler_8wekyb3d8bbwe) E_NOTIMPL
Uri.CreateUri(...\Integration\Addons\OPushUtil.msix)                        OK
RoGetActivationFactory(Windows.Management.Deployment.StagePackageOptions)   0x80040154
package_manager_QueryInterface {0847e909-...}                               E_NOINTERFACE
wine: Unhandled page fault on read access to 0000000000000000 at 14075707C
```

新 API（`StagePackageOptions`）Wine 没有，C2R 于是回退到
`IPackageManager6::ProvisionPackageForAllUsersAsync`；Wine 的 `PackageManager`
只实现了 `IPackageManager` 和 `IPackageManager2`，QI 失败，而 C2R 不检查返回值。

### 问题不在 C2R 不检查返回值

它在 Windows 上不会挨这一下：QI 成功，调用返回一个操作对象，对象自己说部署成没成。
Wine 这边的桩打破了这个契约——返回 `E_NOTIMPL`，`[out]` 参数原封不动——
于是调用方按契约去解引用一个从没被写过的指针。

所以修法是**把问题答了，而不是拒答**。Wine 里没有 AppX 包存储：一个包都没注册，
一个也部署不了。这两件事都有诚实的 WinRT 表达方式：

- `FindPackages*` 返回**空的 `IIterable<Package>`**。零个包匹配，因为确实零个包存在。
- `FindPackageBy*FullName` 返回 `S_OK` + `*package = NULL`，即"没找到"。
- 每个 `*Async` 返回一个**已经走到 `AsyncStatus_Error` 的操作**，
  `IAsyncInfo::ErrorCode` 是 `ERROR_NOT_SUPPORTED`，`GetResults()` 给出一个
  `DeploymentResult`，里面是同一个错误码和一句说明没有部署任何包的 ErrorText。
  微软自己的部署示例就是 `if (op.Status == AsyncStatus.Error) { op.GetResults().ErrorText }`，
  失败的部署走的正是这条路。调用方无论从哪个角度看，看到的都是"没成"。
- 其余仍是桩的方法，至少把 `[out]` 置 NULL，下一个不看 HRESULT 的调用方拿到的
  是 NULL，而不是它自己栈上的残留值。

`IPackageManager6` 按 SDK idl 补齐，连带补上签名需要的
`AddPackageByAppInstallerOptions` 枚举和 `PackageVolume` 运行时类。

### 结果

`tools/appxprobe` 走一遍 C2R 走的同一条路：`QI IPackageManager6` 0x0，
空集合可正常遍历（First / HasCurrent → 0 个），异步操作 status=Error、
code=0x80070032，完成回调恰好触发一次，`DeploymentResult.ExtendedErrorCode`
与之一致。

真实安装里也对上了——`WINEDEBUG=+appx` 的日志显示 C2R 逐字段读了我填的东西：

```
package_manager_QueryInterface {0847e909-...}              → S_OK
package_manager6_ProvisionPackageForAllUsersAsync L"aimgr_8wekyb3d8bbwe"
async_QueryInterface {00000036-...}   (IAsyncInfo)
async_info_get_Status
async_GetResults
deployment_result_get_ExtendedErrorCode
deployment_result_get_ErrorText
package_manager_FindPackagesByPackageFamilyName L"aimgr_8wekyb3d8bbwe"   ← 接着处理下一个
```

处理了三个包（`aimgr`、`Microsoft.Office.ActionsServer`、
`Microsoft.OfficePushNotificationUtility`），一次没崩。

**`setup.exe /configure` 第一次以 `EXIT=0` 退出**，整个安装零 page fault。
此前是 exit 5 / 106，且 LASTRUN 必崩。

## 下一个卡点：App-V 虚拟注册表是空的

装完了，Word 也能起来（主窗口 `Microsoft Word 16.0`），但仍报
「很抱歉，此功能看似已中断，并需要修复」。

这次能指到具体位置。C2R 自己的损坏检查就报了：

```
Office.ClickToRun.CorruptionCheck  Success: false  Result.Code: 15
Result.Type: "CorruptFileCount"
Data.MismatchSizeFiles: "root\vreg\dcfmui.msi.16.zh-cn.vreg.dat|
   root\vreg\office32mui.msi.16.zh-cn.vreg.dat|root\vreg\officemui.msi.16.zh-cn.vreg.dat|
   root\vreg\osmmui.msi.16.zh-cn.vreg.dat|root\vreg\osmuxmui.msi.16.zh-cn.vreg.dat"
```

`root/vreg/` 下 15 个 `.vreg.dat`，**每一个都正好 8192 字节**——用 hivex 打开是
合法的 regf，但里面只有 `ROOT\REGISTRY` 两级空键。`Result.Code: 15` 就是这 15 个。

这批 hive 装的是 Office 的 COM 注册。真机上：

| | C2R 装的 prefix | 真机 |
|---|---|---|
| `HKLM\...\ClickToRun\REGISTRY` 键数 | 6 | 29,791 |
| `ClickToRun\AppVMachineRegistryStore` | 2,863 | 14,922 |
| `HKLM\Software\Microsoft\Office` | 2,913 | 42,085 |
| `root/vreg` 合计 | 122,880 字节（15×8192） | 数 MB |
| `Word.Application` ProgID | 无 | 有 |

真机上 `Word.Application` 在两处：vreg hive 里的
`HKLM\Software\Classes\Word.Application`，以及暂存到真实注册表的
`ClickToRun\REGISTRY\MACHINE\Software\Classes\Word.Application`。两处这里都没有。
Word 起得来是因为文件都在（VFS 有 3.3 GB），但凡要走 COM 注册的功能都落空。

排除掉的几条：

- **不是 C2RManifest 生成的。** 全部 `C2RManifest.*.xml` 加起来只有 1,988 条
  `<Registry>`，且完全没有 Word 的 CLSID `{000209FF-…}`。真机单是 word 那个 hive
  导出就 938 KB。
- **不是 Wine 的 hive API 桩。** `NtRestoreKey`、`RegRestoreKeyW`、`RegLoadAppKeyW`
  确实都是"返回成功但什么都不做"的桩（`RegLoadAppKeyW` 还把 `*result` 填成
  `0xdeadbeef`），本来是很像的嫌疑——但四份安装日志里这些 FIXME **一次都没出现**，
  它们没被调用。Office 是用自己的 regf writer 直接写文件的。
- **不是 CabManager 那两条报错。** 日志自己说了
  「catalog errors are non-fatal, continuing」。
- **重新流式下载不会带回内容。** 删掉 word 的两个 `.vreg.dat` 再跑一遍
  `/configure`，它们回来了，仍是 8192；而且 15 个文件的 mtime 是**同一瞬间**
  （22:15:59），包括我没删的那些——是某一步一次性把它们全写成了空的，
  不是流式下载逐个落盘。
- **`scenario=Repair RepairType=QuickRepair` 空跑。** exit 0，vreg 一字节没变。

C2R 二进制里相关的字符串指向这一步：`About to publish vReg`、
`Unable to find merged VReg file, recreating virtual registry...`、
`C2R::OfficeContainer::CopyVRegMergedFileToVRegMergedDirectory`。
下一步是拿 `WINEDEBUG=+file` 抓这一瞬间到底在读什么、写什么。

作为对照：`~/.wine-altars-office`（从真机导入的那个 prefix）这三项分别是
11 MB / 26,086 / 有——但那是拷来的，不是 C2R 装出来的，不能拿来充数。

### C2R 自己的数据库说得很清楚

`C:\ProgramData\Microsoft\ClickToRun\ProductReleases\<PRID>\operations.db` 是一个
**普通的 SQLite 库**（旁边有 `-journal`/`-wal`，`sqlite3` 直接能开），21 MB，
里面是整个包的关系模型：`Files` 27,255 行、`Paths` 26,501、`Hashes` 82,440
（带 `Size`）、`FileTasks` 20,410（带 `State`/`Disposition`）。
不用再从 100 MB 的 trace 里猜，直接查就行。

查 vreg 文件应该多大：

```sql
select d.Name||'\'||p.Name, h.Size from Paths p
  join Directories d on d.directory_id=p.directory_id
  join Files f on f.target_path=p.path_id
  join Hashes h on h.hash_id=f.hash_id
 where p.Name like '%vreg%' order by h.Size desc;
```

```
  2097152  root\vreg\office.x-none.msi.16.x-none.vreg.dat
   786432  root\vreg\word.x-none.msi.16.x-none.vreg.dat
   131072  root\vreg\wordmui.msi.16.zh-cn.vreg.dat
    ...
```

磁盘上这 15 个全是 8192。

再查 `FileTasks`，一眼看出 C2R 对它们做了什么：

```
   op=1  State=2  Disp=2   n=10205      ← 首次安装：全部正常安装，含 vreg（786432 字节那份）
   op=2  State=1  Disp=1   n=10190      ← 本次操作：待处理
   op=2  State=2  Disp=8   n=15         ← 本次操作：恰好这 15 个 vreg，另一种处置方式
```

`Disposition=8` 这一类**只有这 15 个文件**。也就是说 C2R 并不从包里复制它们，
而是**就地重新生成**——从它暂存在真实注册表里的那棵虚拟注册表
（`HKLM\Software\Microsoft\Office\ClickToRun\REGISTRY`）。
真机上那棵树 29,791 个键，这里 6 个，所以生成出来是空的。

两件事因此确定了：

1. **首次安装时这些 hive 是有内容的**（op=1 的任务 State=2 完成，Size=786432），
   后来被某一步一次性写成空的。
2. **真正断掉的是"把 hive 灌进 `ClickToRun\REGISTRY`"这一步**，发生在它们还有内容的时候。

还要说明一点：前面写「Wine 的 hive API 一次都没被调用」是根据日志里没有
`NtRestoreKey` / `RegLoadAppKeyW` / `RegRestoreKeyW` 的 FIXME。这三个确实是
"返回成功但什么都不做"的桩（`RegLoadAppKeyW` 还把 `*result` 填成 `0xdeadbeef`），
但 **`NtLoadKey` 和 `NtSaveKey` 不是桩、只有 TRACE 没有 FIXME**，默认日志里本来
就看不见——它们有没有被调用，前面那个证据什么也没证明。

而 Wine 的 `load_keys()` 只认自己那行 `WINE REGISTRY Version 2` 开头；喂给它一个
真正的 `regf` 二进制 hive，它返回 `STATUS_NOT_REGISTRY_FILE` 就完了，不打印任何东西。
所以"C2R 调了 `NtLoadKey`、Wine 静默拒绝"完全说得通，得用 `WINEDEBUG=+reg` 实测才能分清。

集成侧的缺口也量出来了。App-V 清单
（`ClickToRun\MachineData\Catalog\Packages\{…}\{…}\Manifest.xml`，1.6 MB）
声明了 1,046 个 `<Extension>`（906 个 `AppV.COM`、129 个 `AppV.FileTypeAssociation`），
其中 191 个唯一 ProgId，包含全部 `Word.*`。实际写进
`AppVMachineRegistryStore\Integration\Ownership\Software\Classes` 的只有 126 个，
**`Word.*` 27 个一个都没有**（真机 27 个齐全）；扩展名也只有 `.docm` `.docx`，
真机还有 `.doc` `.dochtml` `.docmhtml` `.docxml`。

（注意别拿真机的 761 条总数直接比：真机装的是整套 Office，这个 prefix 只装了 Word，
其余全在 `ExcludedApps` 里。要比就按 `Word.*` 比。）

## 虚拟注册表：是 `RtlValidRelativeSecurityDescriptor` 把答案反过来了

追下去发现，`root\vreg` 那 15 个 hive 不是流式下载写坏的——**流式下载每次都把正确内容写对了**。
`WINEDEBUG=+file` 的跟踪里，同一批暂存文件被写了两遍：

```
02c8: WriteFile <staging>\dcfmui...vreg.dat  16384  → SUCCESS(16384)   ← 正确内容，与包数据库一致
...
0374: 加载 OffReg.dll
0374: ReadFile  <staging>\dcf...vreg.dat  524288 → SUCCESS(524288)     ← 读进来的也是对的
0374: RtlValidRelativeSecurityDescriptor(..., 396, 0)  ×2
0374: DeleteFileW <staging>\dcf...vreg.dat
0374: CreateFileW <staging>\dcf...vreg.dat                             ← 删掉重建
0374: WriteFile 4096 ×2                                                ← 写回 8192 字节空 hive
0460: CreateHardLinkW root\vreg\dcf...dat ← <staging>\dcf...dat
```

`offreg.dll` 是微软的 **Offline Registry Library**（`OROpenHive`/`ORSaveHive`/…），
C2R 用它处理 App-V 的虚拟注册表。写个几十行的探针（`tools/offregprobe`）直接调它，
10 秒就复现：

```
OROpenHive  : 1017  <-- FAILED        (ERROR_BADDB，"注册表数据库损坏")
```

一个完全正常的 786,432 字节 Office hive，Wine 下**打不开**。

`WINEDEBUG=+relay` 跟踪探针，失败点一目了然：

```
ntdll.RtlValidRelativeSecurityDescriptor(7ffffe9f1100, 0x18c, 0)
fixme:ntdll:RtlValidRelativeSecurityDescriptor ...: semi-stub
ntdll.RtlValidRelativeSecurityDescriptor() retval=00000000     ← FALSE
（紧接着一路 free，OROpenHive 返回 1017）
```

Wine 的实现：

```c
BOOLEAN WINAPI RtlValidSecurityDescriptor( PSECURITY_DESCRIPTOR descriptor )
{
    SECURITY_DESCRIPTOR *sd = descriptor;
    return sd && sd->Revision == SECURITY_DESCRIPTOR_REVISION;   /* BOOLEAN */
}

BOOLEAN WINAPI RtlValidRelativeSecurityDescriptor( ... )
{
    FIXME("...: semi-stub\n");
    return RtlValidSecurityDescriptor(descriptor) == STATUS_SUCCESS;   /* 拿 BOOLEAN 比 0 */
}
```

`STATUS_SUCCESS` 是 0。有效的描述符返回 TRUE，`TRUE == 0` 为假 → 判为无效；
无效的返回 FALSE，`FALSE == 0` 为真 → 判为有效。**这个函数的每一个答案都是反的。**

改成真正的实现：缓冲区要装得下 `SECURITY_DESCRIPTOR_RELATIVE`、`Revision` 正确、
带 `SE_SELF_RELATIVE`，它声称的每个组件都得是合法 SID 或 ACL 且完全落在给定长度内，
`info` 指名的组件必须存在。

### 效果

探针：

| | 修复前 | 修复后 |
|---|---|---|
| `OROpenHive` | 1017 ERROR_BADDB | 0 |
| `REGISTRY\MACHINE\Software\Classes` | 打不开 | 86 个子键（`.doc` `.docm` `.dochtml` …） |
| `ORSaveHive` | 8192 字节 | 708,608 字节 |

真实安装（`setup.exe /configure`，EXIT=0，零崩溃）：

| | 之前 | 现在 | 真机（全套 Office） |
|---|---|---|---|
| `root/vreg` 合计 | 122,880（15×8192） | **6,422,528** | 数 MB |
| `word.x-none...vreg.dat` | 8192 | **786,432**（与包数据库一致） | 786,432 |
| `ClickToRun\REGISTRY` 键 | 6 | **14,346** | 29,791 |
| `HKLM\Software\Microsoft\Office` | 2,913 | **17,255** | 42,085 |

15 个 hive 现在**每一个**都和 `operations.db` 里记录的大小分毫不差。

### Word 现在到哪一步

「很抱歉，此功能看似已中断，并需要修复」**不再出现**。Word 起来后是完整的：
开始屏幕（新建模板、最近/收藏夹/与我共享）、完整功能区（文件/开始/插入/设计/布局/
引用/邮件/审阅/视图/帮助）、字体段落样式组和实时样式预览、带页边距和光标的文档页，
标题栏 `文档1 - Word (未经授权产品)`（未登录，符合预期）。**约 5 秒**到达可编辑窗口。

但它撑不住：大约 20 秒内进程退出，有时先弹「很抱歉，Word 遇到错误，使其无法正常工作」。
Word 自己也诊断到了——第三次启动时它问「最后两次启动时开始屏幕意外关闭。是否想要将其关闭？」。

已排除的：

- `winebus.sys+0x20f0` 那 17 次 `c0000005`（空指针读，偏移 0x628）是**既有噪音**：
  空跑 `cmd /c ver` 一样有 17 次。
- 抛出的 C++ 异常都是有类型、可捕获的：`AppvIsvSubsystems64` 抛
  `windows_exception_impl<0>`，`Mso30win32client` 抛
  `Roaming::RoamingCacheException`（未登录时漫游缓存失败），两者都不像致命的。
- `msoxmlmf.dll` 加载失败是**注册路径写错**，不是文件缺失：包里有
  `root\vfs\ProgramFilesCommonX64\Microsoft Shared\Office16\MSOXMLMF.DLL`（82,312 字节，
  磁盘上也在），而注册表把 COM 类指向了 `Common Files\Microsoft Shared\ClickToRun\msoxmlmf.dll`。

下一步是定位那个真正致命的失败。

### 那个弹窗是授权对话框，不是新缺陷

Office 自己的诊断日志给出了因果链。它在
`%LOCALAPPDATA%\Temp\Diagnostics\WINWORD\Primary*.log`，是**可读的 TSV**
（16 MB 预分配，有效内容几百 KB），每行一个 `SendEvent {...}`：

```
03:17:51.314  Office.Word.FileNew.CreateNewFile                     ← 新文档已建好
03:17:51.796  Office.Telemetry.LoadXmlRules
03:17:51.800  Office.Licensing.FullValidation   Success=false
              Result.Code=-1073418219 (0xC004F015)
              Data.Licenses=""  Data.LicenseStatuses=""
              Data.ValidAcids="{3AD61E22-E4FE-497F-BDB1-3E51BD872173}"
03:17:51.809  Office.Performance.Boot                               ← 启动完成
03:17:52.190  Office.UX.NUIDialog.DialogBootTime  DialogId=4187 IsModal=true
```

`0xC004F015` 是"未安装许可证"。`Licenses` 和 `LicenseStatuses` 都是空字符串——
这个 prefix 从来没有登录过，所以**一个许可证都没有**。这不是 Wine 的缺陷，
是真实状态。

而 `scripts/office-sign-in.sh` 的注释里，这个对话框早就写清楚了：

> the "Sign in" button lives behind the licensing dialog, which quits Word if
> you dismiss it

也就是说，C2R 装出来的这个 prefix 现在**和之前那个从真机导入的 prefix 处在同一状态**：
安装本身是完整的，缺的是登录换取许可证这一步，而那条路本仓库已经有工具
（`scripts/office-sign-in.sh`、`scripts/mint-wam-tokens.sh`、`scripts/verify-signin.sh`）。

顺带记下诊断日志这个入口：`Diagnostics\WINWORD\Primary*.log` 里直接有
`Office.Licensing.FullValidation` 的 HRESULT、`Office.Performance.Boot` 的各段耗时、
`Office.UX.NUIDialog.DialogBootTime` 的 DialogId——比从 `WINEDEBUG` 里猜快得多。

## 登录入口到不了，是因为 `SLInstallLicense` 把产品定义扔了

把两个功能门打开、WebView2 运行时装好之后，Word 仍然弹「很抱歉，Word 遇到错误…
是否希望我们立即修复?」，而且**根本没有尝试加载登录界面**——遥测里从
`Office.Licensing.FullValidation`（失败，`0xC004F015`）直接跳到
`Office.UX.NUIDialog.DialogBootTime`（`DialogId=4187`，`IsModal=true`），
中间没有任何 SDX 事件。

`0xC004F015` 是 `SL_E_PRODUCT_SKU_NOT_INSTALLED`，字面意思是"这个产品没装"。
而 `tools/sppcprobe/README.md` 里 2026-09-07 记的原生对照是：

> 参考机整个 Office App 的评估返回 S_OK，但结果包含 32 条未授权记录和一条通知记录

同一个调用，Windows 答"这是你的 33 条 SKU，全部未授权"，Wine 答"这个应用根本没装"。
Office 把后者读成"安装损坏"，于是给修复对话框而不是登录界面——登录入口永远到不了。

### 产品定义是安装期交过来的

`WINEDEBUG=+slc` 跟踪一次安装：

```
SLGetSLIDList (...) stub, returning an empty list
SLInstallLicense (0000000000000001,11181,...) stub, accepting
SLInstallLicense (0000000000000001,11185,...) stub, accepting
... 共 54 次
```

追踪里 30 个不同的字节数，**30 个全部能在 `root\Licenses16\` 里找到同样大小的文件**。
也就是说 C2R 就是在用这个 API 把 `*.xrm-ms` 逐份交给许可子系统，
而 Wine 的 "accepting" 是收下就扔。

这类许可是**产品定义**：PPD 标题写明它描述哪个 SKU（`Office<GUID> PPD License`），
`sl:appId` 是应用，`tm:editionId` 是版本名，另一些文件带 `ApplicationBitmap`
说明该 SKU 覆盖哪些应用。里面没有密钥、没有权利、没有账户、没有设备绑定，
登记它不改变任何状态——目录照样把每个 SKU 报成 `UNLICENSED` /
`SL_E_PKEY_NOT_INSTALLED`，这就是事实。

`dlls/sppc/license.c` 解析 blob（UTF-8 或两种字节序的 UTF-16），拒绝 DTD 与实体声明，
按**本地名**匹配元素以免去猜命名空间前缀。位图和 PPD 分散在不同文件、到达顺序不定，
所以位图先存在 `Software\Wine\SPPC\Bitmaps` 下，等 SKU 自己注册时再合并——
这样 `sppc_catalog_status` 永远不会看见一个没有 `Name` 的半成品 SKU 键。

`tools/slinstallprobe` 手工装一份再按 Word 的方式评估，10 秒出结果：

| | 之前 | 之后 |
|---|---|---|
| `SLConsumeRight`（Office 应用） | `0xC004F015` 产品未安装 | `0xC004F013` 权利未授予 |
| `SLGetLicensingStatusInformation` | 无记录 | S_OK，**17 条全部 UNLICENSED** |
| licensed / grace / notification | — | **0 / 0 / 0** |

### 还发现装错了产品

参考机 winref 的 SPPC 目录 34 个键全是 `Office16O365HomePremR_*`，
`ProductReleaseIds` 也以 `O365HomePremRetail` 为主——订阅是 **Microsoft 365
家庭版/个人版**。而这个 prefix 之前装的是 `O365ProPlusRetail`（企业应用版），
两者不是一个产品，登录了也拿不到许可证。

改用 `<Remove>` O365ProPlusRetail + `<Add>` O365HomePremRetail 重装，
并同时装上 Excel、PowerPoint、Outlook。装完 `root` 4.4 GB，四个主程序齐备
（Excel 78 MB、Outlook 47 MB）；`INTEGRATE_INSTALL` 仍失败（exit 106，待查），
但 STREAM / STAGEREGISTRY / 损坏检查都通过。

### 结果

重跑一次 `/configure`，`SLInstallLicense` 被调用 57 次，注册了 17 个
`Office16O365HomePremR_*` SKU。Word 启动后**不再有修复对话框**，
出现的是 Office 自己的原生登录界面：

```
登录后即可开始使用 Word
  使用工作、学校或个人 Microsoft 帐户信息
  使用 Word、Excel、PowerPoint 和 Outlook 创建和协作
  使用 OneDrive 云存储跨设备保存和共享文档
  [登录或创建帐户]   我有产品密钥
```

Office 仍未激活，也没有任何许可证被签发——这只是让登录入口第一次可达。

## `INTEGRATE_INSTALL` 失败：integrator 删清单时自己还开着句柄

换成家庭版之后 `setup.exe /configure` 仍返回 106，失败的任务是 `INTEGRATE_INSTALL`。
后果很具体：Office 的文件类型没被发布，`HKCR\.docx` 只有 `PerceivedType` 没有默认值，
`Word.Document.12` 根本不存在，于是 Wine 的 winemenubuilder 不会为这个 prefix 写
`wine-extension-docx.desktop`，**双击文档仍然打开旧 prefix 的 Office**。

C2R 日志里 `C2R::ProcessSpawnerAdapter::SpawnProcessAndWait` 记着完整命令行，
所以这一步能单独跑，**1 分钟复现，不必等整轮安装**：

```
wine 'C:\Program Files\Microsoft Office\root\integration\integrator.exe' \
  /I /Extension /Msi 'MsiName=C2RInt.16.msi,C2RIntLoc.zh-cn.16.msi,*' /C2R \
  'PackageGUID=<pkg>' 'PackageRoot=C:\Program Files\Microsoft Office\root'
```

退出码 32 = `ERROR_SHARING_VIOLATION`。`WINEDEBUG=+file,+server` 把它钉死了：

```
create_file( access=80100080, sharing=00000001 ) → 0x19C   FILE_SHARE_READ
create_file( access=80100080, sharing=00000003 ) → 0x1A0   READ|WRITE
ReadFile 0x19C → 4868（整份）
ReadFile 0x1A0 → 4096/772/0（分块到 EOF）
close_handle( handle=01a0 )        ← 只关了第二个
DeleteFileW 同一个文件             → 0x20
```

integrator 对每份要取消发布的清单开两个读句柄，**只关其中一个**，然后去删它。
两次打开的 share 模式都不含 `FILE_SHARE_DELETE`，所以这在真 Windows 上也会失败——
差异不在 Wine 的文件语义，而在**为什么会走到这次删除**。

### 排除掉的（写下来免得再查一遍）

- **不是 Wine 的关闭不同步。** `tools/delprobe`：开(SHARE_READ)+关后删、照 integrator
  模式开关 6 次后删、映射+解映射+关后删——全部成功；只有句柄真开着时才 32。
- **不是 msxml 占着。** `tools/xmlholdprobe`：`DOMDocument60` 解析同一份清单，
  普通路径与 `file:///` URL 两种形式加载后都能删掉。
- **不是 urlmon。** 日志里 `fixme:urlmon:SecManagerImpl_ProcessUrlAction Unsupported
  arguments` 只在 `dwFlags||dwReserved` 非零时打印，不改变返回值；实测 `policy 0`
  = `URLPOLICY_ALLOW`，返回 S_OK。
- `/proc/<pid>/fd` 里每份清单 4~6 个 fd 是 **Wine 客户端的 unix fd 缓存**，
  不等于未关闭的 Win32 句柄。真正的判据是服务端的 `close_handle`。

### 真正的触发条件：残留的旧产品清单

`ProgramData\Microsoft\ClickToRun\{PackageGUID}\` 里存的是**上一次集成**的清单集，
`root\Integration\` 里才是本次要发布的。换产品之后两者不一致：

| 目录 | 含 Excel/PowerPoint/Outlook 清单 |
|---|---|
| `root\Integration\` | 有 |
| `ProgramData\…\{GUID}\`（换产品前） | 没有——只有 ProPlus 那轮的 15 份 |

integrator 因此有一个非空的 Unpublish 组，要逐个删掉旧清单，就撞上自己的句柄。
而清理失败又让旧清单留在原地，下一轮继续撞——自锁。

把那 15 份陈旧清单挪走（带备份）后单独跑 integrator：**EXIT=0**。
再跑一轮完整 `/configure`：

| | 之前 | 现在 |
|---|---|---|
| `setup.exe /configure` | EXIT=106 | **EXIT=0** |
| `InstallTaskIntegrateinstall` | Success=false | **Success=true, Result.Code=0** |
| `ProgramData\{GUID}\` 清单 | 旧的 ProPlus 15 份 | 新的四应用 15 份 |

### 还差一步：应用 ProgID 仍未发布

集成通过了，但 `AppVMachineRegistryStore\Integration\Ownership\Software\Classes`
下 2842 条里，`Word.*` / `Excel.*` / `PowerPoint.*` / `Outlook.*` **各 0 条**；
`.docx` / `.xlsx` 只有扩展名本身。所以 `HKCR\.docx` 依旧没有默认值，
`Word.Document.12` 依旧不存在，文件关联还是接不过来。

这与退出码是两个独立的缺口。暂时把旧 prefix 的 132 个桌面条目清掉了
（备份在 `~/.cache/wine-altars-desktop-backup-20260920.tar.gz`），
免得双击文档静默打开另一套 Office；四个应用图标本身可用。

## 点「登录」没反应：两个缺失导出，一个盖着另一个

点 Word 的「登录或创建帐户」，面板收起来，然后什么都不发生；两分钟后
Word 自己死掉。trace 里能看见 `WebView2Loader.dll` 和运行时的
`EmbeddedBrowserWebView.dll` 都加载了，接着是一段不断重复的

```
GetEnvironmentVariableW (L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER" …)
GetEnvironmentVariableW (L"WEBVIEW2_USER_DATA_FOLDER" …)
…
Loaded …EBWebView\x64\EmbeddedBrowserWebView.dll
```

——环境创建失败、重试、再失败，`msedgewebview2.exe` 一次都没起来。

### 先排除 WebView2 本身

`tools/wv2probe` 直接调 `WebView2Loader.dll` 的两个导出，不经过 Office：

```
GetAvailableBrowserVersion     : 0x00000000  version=153.0.4234.48
CreateEnvironmentWithOptions   : 0x00000000 (同步返回)
handler Invoke                 : hr=0x00000000 env=…
```

2 秒内起了 2 个 `msedgewebview2` 进程。WebView2 在这个前缀里是好的，
问题在 Office 走到那一步之前。

同时排除掉的还有特性门：诊断日志 `Primary*.log` 里那条
`Office.Experimentation.FeatureQueryBatched` 事件带着 1536 个门的取值，
其中

| 门 | 值 |
|---|---|
| `Microsoft.Office.Identity.OneAuthEnabled` | true |
| `Microsoft.Office.Identity.FG.IsWebView2ForOneAuthEnabled` | true |
| `Microsoft.Office.Identity.TestGate.DisableBrokerForOneAuth` | true |
| `Microsoft.Office.Identity.BrowserSignIn` | true |

全都已经是对的。顺带纠正一条早先的猜测：`ExternalFeatureOverrides` 里那个
不带 `.FG.` 的 `Microsoft.Office.Identity.IsWebView2ForOneAuthEnabled`
**根本不在门控表里**，写不写都一样。

### 真正的第一层：`__C_specific_handler_noexcept` 是个 stub

Word 的 stderr 被同一行刷屏：

```
wine: Call from … to unimplemented function vcruntime140.dll.__C_specific_handler_noexcept, aborting
```

Wine 的 `dlls/vcruntime140/vcruntime140.spec` 里它是 `@ stub`，而调用 stub
会直接 abort 线程。WebView2 的进程内客户端 DLL 拿它当语言处理器，于是
每次环境创建都死在 unwinder 里，然后被重试——那个循环就是这么来的。

它不是 `__C_specific_handler` 的别名。Office 自带的
`root\Client\vcruntime140.dll` 里两者 RVA 不同（`0xf270` vs `0xf4c0`），
反汇编 `0xf270`：

```
mov  rdi, r8            ; ContextRecord
mov  rbx, rcx           ; ExceptionRecord
call 0x250              ; = 0xf270+0x250 = 0xf4c0 = __C_specific_handler
test byte [rbx+4], 0x66 ; UNWINDING|EXIT_UNWIND|TARGET_UNWIND|COLLIDED_UNWIND
jne  ret
cmp  dword [rbx], 0xe06d7363   ; C++ 异常
jne  ret
cmp  eax, 1                    ; ExceptionContinueSearch
jne  ret
call …; call …; call [terminate]
int3
```

即：**先调 `__C_specific_handler`，若答案是「本帧没有处理器」而异常又是
C++ 异常且不在 unwind 阶段，就 `terminate()`** ——异常正要逃出一个
`noexcept` 函数。这是 `dlls/msvcrt/except.c` 里现在实现的语义，x86_64 /
aarch64 / arm 三个分支共用同一段判定；`ucrtbase` 导出、`vcruntime140`
转发，和真实的那一对排法一致（i386 两边都没有，因为 x86 没有
`__C_specific_handler`）。

### 第二层：`0xC06D007F`，延迟加载找不到导出

abort 消失之后，底下压着的异常才露出来：

```
wine: Unhandled exception 0xc06d007f in thread 24 at address … (kernelbase+0xd907)
wine:   … L"mso30win32client.dll" +0xdea164
```

`0xC06D007F` = `VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND)`。
**模块找不到是 `…007E`，这个是模块加载成功但导出不存在**，一下就把范围
缩小了。`tools/pe-delay-imports.py` 列出 `Mso30win32client.dll` 的全部延迟
导入，对着 Wine 的导出表比：

| 项 | 判定 |
|---|---|
| `api-ms-win-core-winrt-*` | apiset，实际解析到 combase，函数齐全 |
| `WebView2Loader.dll` | Office 自带，在 Office16 下 |
| `SLWGA.dll` | Wine 确实没有——但那会是 `…007E` |
| **`USER32.CalculatePopupWindowPosition`** | **Wine 完全没有这个导出** |

`dlls/user32/user32.spec` 里它是注释掉的 stub，`NtUserCalculatePopupWindowPosition`
在 `win32u.spec` 里是 `@ stub -syscall`。

这就是 `TrackPopupMenuEx` 做的那套摆位算术，所以实现放在 `dlls/win32u/menu.c`：
按 `TPM_*` 对齐到锚点 → 与 exclude 矩形相交时，沿**调用方没要求保留的那根轴**
让开（`TPM_VERTICAL` 保留垂直对齐、左右让；缺省保留水平对齐、上下让）→
最后整体收进显示器（`TPM_WORKAREA` 则收进工作区）。`winuser.h` 里
`TPM_WORKAREA`（`0x10000`）和函数声明也一并补上，wow64win 补了 thunk。

`tools/popupprobe` 直接核对十种情形，全部通过：

```
左上/右下/居中对齐、四角越界贴屏、exclude 默认向下让开、
TPM_VERTICAL 向右让开、不相交不动、TPM_WORKAREA 贴工作区、
NULL 锚点返回 FALSE 且 GetLastError=87
```

### 结果

| | 之前 | 现在 |
|---|---|---|
| `unimplemented function` abort | 数千行刷屏 | **0** |
| 点击后 Word | 两分钟后崩溃 | **存活** |
| `msedgewebview2` 进程 | 0 | **7** |
| 顶层窗口 | 无 | `OneAuthWebView2Browser` + `Chrome_WidgetWin_1` 标题 `"Sign in"` 450x519 |
| 网络 | 无 | 到 `150.171.27.11:443` 的 TLS 连接建立 |

点下去出来的是 Office 自己的 OneAuth WebView2 登录页，不是任何脚本代劳的。

### 测试环境的一个坑：Xvfb 与真实会话

本仓库此前大量可视化测量跑在 `DISPLAY=:77` 上，那是
`Xvfb :77 -screen 0 1920x1080x24`——**没有 GPU**。WebView2 的 Chromium
合成器在上面出来的区域，`import -window` 读回来是全黑（截图 43% 黑）；
而用户真实会话 `DISPLAY=:0`（GNOME Wayland 下的 Xwayland）同样的截图
**0% 黑**。所以那片黑是 Xvfb 的产物，不是渲染坏了。

反过来，`:0` 上 mutter 不转发 XTEST：`xdotool` 能移动指针（`getmouselocation`
确认坐标到位），但合成点击送不进 Wine——点「插入」标签页，截图里选中的
仍是「开始」。所以 **`:0` 只能靠真人点，`:77` 才能做自动化点击验证**。
两个缺陷本身与显示无关（一个是 CRT 导出、一个是 user32 导出）。

## 登录之后满载：react-native-win32 的 terminate 循环（字体下拉是无辜的）

用户报「登录激活成功了，字体栏，下拉后卡死」。下拉确实卡，但卡的原因不在下拉。

### 对照：同一个下拉，两个时刻

| 时机 | 打开字体下拉 | 结果 |
|---|---|---|
| 启动 42 秒 | CPU 0% → **6%** | 列表正常弹出 |
| 启动 364 秒 | CPU **99%** | UI 不响应 |

Word 在启动后约 60–90 秒开始有**一个后台线程吃满一个核**，与是否碰过下拉无关。
第一次量到这点是在准备对照实验时——「点之前 CPU 就已经 94%」。

### 真正的循环

一个线程的栈把链条完整摊开：

```
react-native-win32+0x3170f9 → kernelbase RaiseException
  → dispatch_exception → call_seh_handlers
  → react-native-win32 的处理器 → terminate() → abort() → raise(SIGABRT)
  → mso20win32client+0x5230a9 → mso20win32client+0x41594d
```

最后那一帧在往 NULL 写——Office 崩溃处理器「强制产生转储」的惯用法。
`WINEDEBUG=+seh` 一次 2.5 分钟的运行里抓到 **836,592 次 `code=c0000005`**，
`info[0]=1 info[1]=0`（写 NULL），出错地址恒为 `mso20win32client+0x41594d`。
进程既不死也不恢复，无限重复。

即：**React Native 抛了个没人接的 C++ 异常 → terminate → abort → Office 的崩溃
处理器故意触发访问违例 → 这个违例被层层 SEH 消化掉 → 从头再来。**

### 被证伪的几条路（别再走）

| 猜测 | 证据 |
|---|---|
| 字体太多 | 列表约 1063 项（MSAA 树重复遍历会看到 6084，需去重） |
| Wine 字体代码慢 | `+font` 在点开后 2.7 MB 就停止增长；`SelectObject` 仅 24 次/秒 |
| 消息泵空转 | `GetInputState` 437 次/秒、`WM_TIMER` 14 次/秒，不足以解释 CPU |
| 线程池自旋 | 给 `timerqueue`/`waitqueue`/`ioqueue`/`worker` 四个循环和 `tp_object_execute` 都加了计数，全不触发 |
| 缺 `chakra.dll` | RN 确实延迟导入它（Wine 没有，错误 126），但 `+loaddll` 证明实际加载的是 Office 自带的 `v8jsi.dll`（V8），从没去找 chakra |
| 本轮新加的 `__C_specific_handler_noexcept` | 它的 terminate 分支带 ERR 日志，**零次触发** |

### 排查方法上踩的坑

- **`WINEDEBUG=-all` 会连 err 通道一起关掉**。自己往 Wine 里加的 `ERR()` 埋点因此
  全部静默，一度误判成「埋点不触发」。排查时必须用默认 WINEDEBUG。
- **winedbg 的 attach 会把线程停在 syscall 边界**，frame 0 不可信（总是停在某个
  syscall thunk 的 `ret` 上）；调用方帧是真的。判断线程在用户态还是内核态，
  看 `/proc/<tid>/stat` 的 utime/stime 分解。
- `ps -o %cpu` 是**累计均值**不是瞬时值，要用 `/proc/<pid>/stat` 差分。
- `pgrep -cf <pattern>` 会匹配到自己的命令行（计数虚高）；Wine 进程的 Linux `comm`
  是线程名不是 exe 名，`ps -eo comm | grep WINWORD` 得 0。
- Wine 的线程名都以 `wine_threadpool` 开头且超过 15 字符，`comm` 截断后
  worker / timerqueue / waitqueue 全都一样；临时把名字改短才能定位。
- syscall thunk 的编号要**按 thunk 边界**反汇编来认（`mov eax,<n>` 属于哪一个 thunk），
  凭偏移猜会差一个。

### 还没解决

RN 抛的是什么异常、从哪抛的，尚未定位（`react-native-win32+0x3170f9` 调
`RaiseException`）。下一步从那里入手，而不是再查字体或消息泵。

## 那个异常是什么：三个没注册的 WinRT 类

接上节。RN 抛的是哪个异常、从哪抛的，这次查清了。

### 从异常记录直接读出类型名

`0xE06D7363` 的记录里，`ExceptionInformation[2]` 是 `ThrowInfo` 指针、`[3]` 是
抛出模块的映像基址，所以 `[2]-[3]` 就是 RVA，而 `ThrowInfo` 往下全是该模块
文件里的静态数据。`scripts/pe-throwinfo.py` 顺着
`ThrowInfo → CatchableTypeArray → CatchableType → TypeDescriptor` 走下去：

```
$ pe-throwinfo.py react-native-win32.dll 0x57fb18
    .?AUhresult_class_not_registered@winrt@@
    .?AUhresult_error@winrt@@
```

**`winrt::hresult_class_not_registered`** ——C++/WinRT 在 `RoGetActivationFactory`
返回 `REGDB_E_CLASSNOTREG` 时抛的那个。

### 和激活失败一一对上

`+seh` 里失败与抛出是同线程紧邻的：

```
071c:err:combase:RoGetActivationFactory Failed to find library for L"Windows.System.DispatcherQueue"
071c:trace:seh:dispatch_exception code=e06d7363   info[2]-info[3] = 0x57FB18
```

Word 一轮里 RN 要的、Wine 没有的类共三个：

| 类 | 状态 |
|---|---|
| `Windows.System.DispatcherQueue` | **本轮已实现**（`dlls/coremessaging`），激活失败 4 → 0 |
| `Windows.Networking.Sockets.MessageWebSocket` | 仍缺 |
| `Windows.Web.Http.Filters.HttpBaseProtocolFilter` | 仍缺，**它是现在直接引爆循环的那个** |

补完 `DispatcherQueue` 之后，AV 风暴前的最后两条变成：

```
02f4: Failed ... MessageWebSocket        → e06d7363 (RVA 0x57FB18)
02f4: Failed ... HttpBaseProtocolFilter  → e06d7363 (RVA 0x57FB18)
02f4: c0000005 ×1,400,000 ...
```

### 顺带排除：Wine 的 C++ 异常处理没问题

那个 `ThrowInfo` 列了 `hresult_error` 作为可捕获类型，所以
`catch (winrt::hresult_error const&)` 本该接住。栈里又出现过
`__CxxFrameHandler4`（MSVC 2019+ 的压缩 EH 格式），一度怀疑是 Wine 匹配不上
catch。但 `dlls/msvcrt/handler4.c` 里那几个 FIXME
（`unsupported flags` / `unknown header` / `function ip_map not found` /
`unsupported ret addr`）**一次都没触发**——Wine 的 EH 是对的，
React Native 确实就是没有 catch。

### 现在的处理：不加载这个宿主

两个 WinRT 类要从 IDL 写起（`include/` 里连 `windows.web.http.idl` 都没有），
不是能顺手补完的。在补上之前，`scripts/disable-office-reactnative.sh`
用 Wine 的 `DllOverrides` 把 `react-native-win32` 置空，让 `LoadLibrary` 失败，
Office 当作可选组件缺席处理：

| | 之前 | 禁用之后 |
|---|---|---|
| 启动 90 秒后 CPU | 100% | **0–1%** |
| 运行 225 秒后开字体下拉 | 不响应 | **CPU 1%，列表正常** |
| 再点一次 | 关不掉 | **正常收起** |

代价是基于 React Native 的那几个 Office 界面不出现——而它们今天本来也不能用，
只会把整个进程拖死。脚本带 `--undo`，等 Wine 补上那两个类之后撤掉即可。

## 第二个卡死：GDI 字体锁被泄漏，同一线程自己等自己

RN 那个满载循环挡掉之后，Word 仍然会卡——但这次 CPU 是 **0%**，是等待而不是空转。
两者是独立的两个问题。

### 抓法

`dlls/win32u/font.c` 的 `font_lock` 是非递归的 `PTHREAD_MUTEX_INITIALIZER`。
把 22 处 `pthread_mutex_lock( &font_lock )` 换成一个带 5 秒超时的包装，超时就报告
持有者线程与持有它的函数；再在「持有者就是自己」时用 `backtrace()` 打出
**取锁时**和**再次取锁时**两条 unix 侧调用栈。

命中后 49 次告警全部一模一样，日志到此为止——**永久自死锁**：

```
thread 0260 in font_SelectFont has waited over 5s; held by thread 0260 in font_GetGlyphOutline
```

### 两条栈解出来的位置

用带调试信息的 `win32u.so` 对 `backtrace()` 的地址做 `addr2line`：

| | 调用链 |
|---|---|
| **取锁处** | `NtGdiExtTextOutW` → `dibdrv_ExtTextOut`(graphics.c:934) → `cache_glyph_bitmap`(graphics.c:780) → `font_GetGlyphOutline`(font.c:4209) **取锁后没有解锁** |
| **再取处** | `NtGdiSelectFont` → `dibdrv_SelectFont`(graphics.c:955) → `font_SelectFont`(font.c:4720) |

两条栈的底都是 `NtUserDispatchMessage`，是两次不同的消息派发——所以不是嵌套调用，
而是前一次**把锁带走了**。`font_GetGlyphOutline` 本身只有

```c
font_lock_acquire();
ret = get_glyph_outline( ... );
font_lock_release();
```

没有提前返回，所以只能是 `get_glyph_outline` 没有正常返回。

### 现场线索

每次死锁前紧挨着都是同样三行：

```
fixme:font:get_nearest_charset returning DEFAULT_CHARSET ...
  file = L"\??\C:\users\crossover\AppData\Local\Microsoft\FontCache\4\PreviewFont\flat_officeFontsPreview_4_42.ttf"
```

`get_nearest_charset` 只在 `select_font` 里被调用，所以那是三次**成功**的选字体。
那个文件是 Office 为字体下拉列表生成的**预览字体**（841 KB，`glyf` 表 836 KB，
约 555 个字形，每个字形是一整条字体名的轮廓）——和「滚字体列表就卡」完全对应。

### 被排除的机制

怀疑过「写用户缓冲区时页错误 → Wine 在 syscall 边界捕获 → unix 栈帧作废 →
解锁不执行」。`tools/glyphlockprobe` 专门测这个：故意让 `GetGlyphOutlineW` 写进
保护页，再做一次普通的 `SelectObject(字体)`。结果是那次调用**直接返回 0xC0000005**
（Wine 在边界上捕获并回传状态），随后的选字体正常——**这条路径不泄漏锁**。
所以机制另有其因，仍在查。

### 状态

诊断已经能在死锁瞬间打印两条栈，并记下「哪个字形请求没回来」
（`glyph_in_flight`）。在 `:77` 上人工滚动字体列表 80 次没能复现，
用户的真实会话能稳定复现，下一步靠那边的现场取这条记录。

## 手机远程转屏后 Word 被关在旧尺寸里，后来又被截成 1024x768

用户从手机上的 Windows App 连 gnome-remote-desktop（远程登录，会话是无头的，
唯一的显示器是 GRD 给的虚拟显示器 `Meta-0`）。竖屏转横屏后 Word 的窗口变大了，
内容却停在旧分辨率的框里；修过一轮之后，又变成整个会话都被截成一个小窗。

### 第一轮：时序和通知（`a8bf297`）

- mutter **先**改 X 窗口大小，这时 Wine 的显示配置还是旧的；Office 在
  `WM_NCCALCSIZE` 里按 `GetMonitorInfo` 的工作区算客户区，算出的是旧显示器。
- Wine 不给普通窗口发主机侧的 `WM_DISPLAYCHANGE`，也不重新最大化已最大化的窗口，
  所以 Office 再也没有机会重算。
- GRD 换屏时先撤旧输出再上新输出，枚举到零个显示器的那一刻被写进了注册表。

改成桌面进程独占显示配置、整体提交、零显示器的枚举不提交、变化后广播并重新最大化。
在无头 mutter 里**旋转同一个输出**的测试通过——但真实会话仍然不对。

### 第二轮：真正的根因是一条改不动的注册表链接（`a17547a`）

在用户会话里量到的：

| 问谁 | 答案 |
|---|---|
| `xrandr`（:0） | `Meta-0` 1920x1080，输出 ID `0x6d1` |
| `tools/monprobe`（同一 Wine 会话） | `SM_CXSCREEN` 1024x768，唯一显示器叫 `WinDisc` |
| Word 的日志 | 反复出现 `find_monitor_from_path Failed to find monitor with path "DISPLAY\Default_Monitor\0001&0000"`，随后 `Failed to read display config, using a virtual monitor` |
| `DEVICEMAP\VIDEO` | 只有 `\Device\Video0` → `...\Control\Video\{guid}\0000` |
| `...\Video\{guid}\0000` 读出来的内容 | `MonitorID0 = 0001&0000`、`StateFlags 0x1`——这是**旧输出 `Sources\06bb`** 的内容 |
| `Sources\06bb`、`Sources\06ce` | 各多出一个本该写在链接键上的 `SymbolicLinkValue` |

本会话里 GRD 已经换过三个输出（`06bb`、`06ce`、`06d1`）：Xwayland 给每个新
wl_output 一个新的 RandR 输出 ID，GRD 在客户端重连或转屏时换新的虚拟显示器。

win32u 把每个 source 的设置存在按驱动 source 名（这里就是输出 ID）命名的
`Sources\<name>` 键里，再用按枚举顺序命名的链接键 `...\Video\{guid}\%04x`
指过去。链接已存在时，旧代码用 `REG_OPTION_OPEN_LINK` 按名字打开它来改指向——
但这个选项**不会**打开链接本身（Wine 自己的 `ntdll/tests/reg.c` 在 Windows 上
验证过："REG_OPTION_OPEN_LINK flag doesn't matter"，只有 `OBJ_OPENLINK` 才行），
名字被解析到旧目标，新目标作为普通值写进了旧目标键，链接纹丝不动。

后果分两级：

1. 只换过输出：读回的是旧输出的模式——显示器还是旧尺寸（转屏后被关在旧框里）。
2. 换屏的瞬间新旧两个输出并存过：旧输出那次被排成非主 source（id 1），它的键里
   写下了 `0001&0000`；之后链接一直指着它，而这个显示器早已不存在 → 读配置失败 →
   整个会话用 1024x768 的 `WinDisc` 兜底，直到 wineserver 退出（这些键是 volatile 的）。

修法：链接已存在时用 `OBJ_OPENLINK` 打开链接本身再设 `SymbolicLinkValue`。

### 怎么在没有手机的情况下复现

无头 mutter，两个虚拟显示器，用 `ApplyMonitorsConfig` 轮流只启用其中一个——
每次启用 Xwayland 都会给新的输出 ID（实测 `0x21` → `0x360` → `0x362`），
和 GRD 换屏一样。测试窗口模仿 Office：在 `WM_NCCALCSIZE` 里按 `GetMonitorInfo`
算最大化客户区。

| 步骤 | X | 旧版 Wine | 新版 Wine |
|---|---|---|---|
| 只 Meta-0 | 1080x2340 | 1080x2340 | 1080x2340 |
| 换成 Meta-1 | 2340x1080 | **1080x2340**，客户区 1080x1071（被截） | 2340x1080，客户区 2340x1071 |
| 两个都开 | 3420x2340 | 两台，正确 | 两台，正确 |
| 只剩 Meta-1 | 2340x1080 | **1080x2340** | 2340x1080 |

**教训**：第一轮只测了「同一个输出转方向」，这条路径上链接永远指向同一个键，
所以测不出来。测试要复现真实客户端的**行为**（换输出），而不只是它的**结果**
（分辨率变了）。

### 部署后在用户会话里量到的

重启会话后 `monprobe`：1920x1080，工作区 (0,0)-(1920,1032)；Word 最大化后窗口
(0,0)-(1920,1032)、客户区 1912x1024，截图里功能区、标题栏、状态栏完整；日志里
不再有 `Failed to read display config`。真实转屏仍待用户在手机上确认。

## 手动双面：纸叠是平移放回的，反面要倒序、长边装订才转

用户反映：第二面印出来和 Windows 上相比转了 180°；Windows 上的做法是出纸后把纸叠拿出来，
**原样平移放回纸盒**（HP 驱动的提示只说"请按下图所示将纸叠重新插入进纸盒"，关键在图里）。

旧实现假设"整叠沿长边翻过来再放回"，于是第二遍按自然顺序、只有短边装订才转 180°。
按平移放回（出纸面朝下、纸盒从顶部取纸）重新推：

- 最后印的那张在最上面、最先被取 → 第二遍要**倒序**。奇数页时最上面那张没有反面。
- 先出机的那条边平移后变成最后进纸 → 不转的反面与正面是"短边翻"对齐 → **长边装订要转 180°，短边不转**。

两样旧实现都反了：单张纸时反面差 180°（用户看到的），多张时还配错纸。

### 不费纸地量整条链

`scripts/duplex-capture.sh`：`ippeveprinter` 假打印机 + 用真打印机 PPD 建的临时 CUPS 队列，
跑 `tools/duptest`（第 N 页页顶左侧 N 个方块），`scripts/urf-pages.py` 解码假打印机收到的 URF。
覆盖 Wine 的作业票据 → cupsd → pdftopdf → URF 全程。5 页、长边：

| | 第一遍 | 第二遍（依次进纸） |
|---|---|---|
| 旧版 | 1、3、5 正向 | 2、4、空白，不转 |
| 新版（`6cda833`） | 1、3、5 正向 | **空白、4、2，转 180°** |

平移放回后纸叠自上而下是印了 5、3、1 的三张，新版正好配上空白、4、2。空白页是 cups-filters
2.0.1 的 pdftopdf 自己补的：`page-set=even` 遇到奇数页数会补一张，再加 `outputorder=reverse`
它就排在最前（`cupsfilter` 实测；`copies=2` 时是 空 4 2 空 4 2，也对）。

**陷阱**：`DMDUP_VERTICAL` 是 2（长边），`DMDUP_HORIZONTAL` 是 3（短边）。测试程序传错一次，
结果看起来像"旋转条件被反了"，实际测的是另一种装订。

## 换远程客户端时 Word 的文档窗口跑到屏幕外

用户从手机和一台 Windows 电脑轮流连 GRD。之后 Word 的三个文档窗口都看不见了：
Wine 里它们在 (-3480,0)、(-3412,0)、(-3840,0)，`MonitorFromWindow` 为 NULL，X 窗口未映射。

journal 里 GRD 每次交接的顺序是：撤掉旧虚拟显示器 → "Switched the physical monitors back on"
（实体 4K 显示器）→ 新虚拟显示器作为主显示器加在它右边 → "dropped 1 physical"。
数字对得上：mutter 把窗口搬到实体显示器的 X 坐标 360、428、0 上；新虚拟显示器成为主显示器、
位于 X=3840 后，Win32 坐标 = X − 3840 = −3480、−3412、−3840；实体显示器撤掉后 Wine 保持
Win32 坐标不变，窗口落在任何显示器之外，X 窗口被取消映射。

Windows 在显示器被拔掉时会把它上面的窗口搬到剩下的显示器上；Wine 没有。修复（`7de4d86`）：
显示变化后，换屏前在某台旧显示器上、换屏后不在任何显示器上的可见顶层窗口，由它自己的线程
挪到最近显示器工作区的同一相对位置（放不下就缩小）；本来就在屏幕外的窗口不动。

最大化的那个文档另有原因：Office 常调 `GetWindowPlacement()` 保存窗口位置，Wine 借此把最大化
窗口**当时**的位置记进 `max_pos`，而 `get_min_max_info()` 只要 `max_pos` 不空就用它、不看
显示器——于是每次重新最大化都回到那台已消失的显示器。显示变化后重新最大化前清掉 `max_pos`。

复现（无头 mutter，`--virtual-monitor 3840x2160 --virtual-monitor 1920x1080`，按 GRD 的顺序
切换，测试程序一个普通窗口、一个最大化窗口，并像 Office 一样定时 `GetWindowPlacement`）：

| 实体撤掉之后 | 普通窗口 | 最大化窗口 |
|---|---|---|
| 旧版 | (−3481,39)，不在任何显示器上，X 未映射 | 在主显示器上 |
| 新版 | (359,39)，保持原来的相对位置 | 在主显示器上 |

复现里旧版的最大化窗口碰巧没丢，是 mutter 在中间一步已经把它挪到新主显示器上了；用户会话里
丢的那个，是被 `max_pos` 拽回去的，这一条靠代码和现场数值确认，没有在复现里单独打出来。

## 自动保存：三个互不相干、都不出声的缺口

现象依次是：点"自动保存"开关，弹出"如何启用'自动保存'？"，上传位置列表是空的、旁边一个
"登录"按钮；修好第一个后能选 OneDrive、能命名，但状态栏说"更改已保存，但因服务器忙而未能上传"；
修好第二个后能上传，但关掉再打开这份文档 Word 就崩，下次启动 Word 自己去重开它时也崩。
三处都没有一行报错，也没有 C++ 异常。

### 1. 服务目录写不进缓存：`<o:Scope></o:Scope>` 读成了 NULL（`6b8b746`）

Office 从 `odc.officeapps.live.com/odc/servicemanager/catalog` 取"可保存到哪里"的服务目录，
用 WWSAPI 的 `WsReadType` 解析后写进 `HKCU\...\Common\ServicesManagerCache\ServicesCatalog`。
在 Wine 里这个键一直是空的。Mso98 `CacheManager::WriteServicesCatalog` 在所有出口下断点，一次
就看到它从 `IdentityNotReachedTag(0x100624b)` 退出——"ServicesCatalogResults.Auth should not
have a null Scope"，出在第 16 个服务 `FP_EXCHANGE_MSA`，它的 XML 是 `<o:Scope></o:Scope>`。
而且这个检查在 `ClearAllData` 之后，所以缓存被清空、再也写不进去。

探针（`tools/wsemptyprobe`）在 Windows 上实测：可选 `WS_WSZ_TYPE` 字段遇到空元素，Windows 给
`""`，Wine 给 NULL；`WS_STRING`/`WS_XML_STRING`/`WS_BYTES` 也是 Windows 给"长度 0、指针非空"，
Wine 给空指针（Wine 自己的测试里早就有这几条 `todo_wine`）。改成空元素读作空值之后，五个
`todo_wine` 变为通过，新加的结构体字段测试在 Windows 上也通过；目录写入 28 个服务，
`CacheReady=1`，对话框里出现"OneDrive - 个人"。

中间走过的弯路：`CloudStorage::AddServicesToCatalog` 返回 false 看起来像原因，其实调用处直接
忽略返回值，而且没有注册第三方云存储时 Windows 上它同样返回 false。

### 2. 上传被 503：信封里 `xmlns:s` 写了两遍（`5b4cf10`）

`+winhttp` 看到 FSSHTTP 的 `POST .../_vti_bin/cellstorage.svc/CellStorageService` 得到
`503 Service Unavailable`，响应头 `X-Azure-ExternalError: 0x80072efe,OriginConnectionAborted`：
前端把请求转给源站，源站直接断了连接。请求头与 Windows 无异，于是在 `winhttp!WinHttpWriteData`
下断点抓正文（`tools/bpdump`），第一行就是

    <s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">

`+xmllite` 给出 Office 的调用：`WriteStartElement(L"s", L"Envelope", uri)` 之后
`WriteAttributeString(L"xmlns", L"s", NULL, uri)`。Wine 查"当前元素是否已有这个声明"时拿
`"xmlns"` 当前缀去查，查不到就又登记一份并立即写出，元素自己那份在开始标签收尾时再写一次。
`tools/xmlnsprobe` 在 Windows 上测了十几种写法（重复声明报 `WR_E_NSPREFIXDECLARED`、
`xmlns` 前缀配 xmlns URI 合法、读者复制 `xmlns=""` 原样写出等），Wine 原来错一半以上，还会写出
非法的 `xmlns:xmlns="..."`。修好后探针输出与 Windows 逐字相同；Wine 的 xmllite 测试 0 失败，
新加的测试在 Windows 上也通过。之后 POST 返回 200，文件出现在 OneDrive，Word 弹出
"自动保存：你无需再单击'保存'"。

### 3. 重开即崩：ole32 在探测复合文件时 Flush 了 Csi 的流（`47edbea`）

崩溃点是 `Csi::StreamOnIFileBranchBase::Stat`，`this+0x38`（文件分支）为 NULL；调用者是 Word 的
`FcMacFn`（取文件长度），上游 `FnOpenFnmCore`。其间没有任何异常。`tools/bptrace` 在 Csi 的流对象
初始化、提交、`Stat` 上记录 `this` 和 `[this+0x38]`，得到完整顺序：

1. `PostInitIFileBranch` → `Init`：流建好，分支有效；
2. `Stat` 被 **ole32 `StorageImpl_Construct`** 经 `ILockBytes_Stat` 调用——Word 对每个打开的文档
   用 `StgOpenStorageOnILockBytes` 探测是不是复合文件；
3. `CommitEx` → `CommitToBranchAndInvalidateStream`：分支被提交后置空；
4. `FcMacFn` 再调 `Stat` → 崩溃。

Wine 的 `StorageImpl_Construct` 失败时经 `IStorage_Release` → `StorageImpl_Destroy` →
`StorageImpl_Flush` 无条件调用 `ILockBytes_Flush`，成功时也立刻 Flush 一次；Csi 的流把 Flush 当
提交。`tools/lockbytesprobe` 在 Windows 上实测：非复合文件被拒（返回 `STG_E_FILEALREADYEXISTS`，
Wine 是 `STG_E_INVALIDHEADER`）、只读或事务模式打开再释放，都**不调用** Flush；只有直接模式可写
打开的存储在释放时 Flush 一次。改成"写过才 Flush、直接模式可写者释放时 Flush"，并做错误码映射后，
探针除读取粒度外与 Windows 一致，ole32 的 storage32 测试 0 失败、新测试在 Windows 上通过。
重新打开 AutoSave 文档不再崩溃，编辑后 `cellstorage` POST 返回 200，SignalR 以 WebSocket 建立，
标题栏显示"已保存"。

**陷阱**：改 ole32 的中途，storage32 测试里一条"文件不应变大"的 `todo_wine` 突然通过了。
查下去是我在一个没有花括号的 `if` 下插了一行，`ILockBytes_SetSize` 变成无条件执行，文件被截短
才"不变大"。`todo_wine` 突然通过，先找到机理再相信它。

### 顺带：React Native 的请求（`970945b`）

`HttpRequestMessage.Properties` 原来返回 `E_NOTIMPL`，Office 的 React Native 界面的请求连发都发
不出去。现在返回一个惰性创建、此后复用的属性表（wintypes 的 PropertySet）；Windows 上实测它是
普通的 `IMap<HSTRING,IInspectable>`，映射操作一致，但 Windows 的对象不支持 `IPropertySet` 和
`IObservableMap`，这一点不同。AugLoop 的请求随后停在 `HttpMediaTypeHeaderValue`（未注册）。

### 测试时的注意事项

- 上传失败的文档留在 `%LOCALAPPDATA%\Microsoft\Office\16.0\OfficeFileCache` 里，Word 启动时会
  自己去重开；在第 3 个修复之前，这意味着每次启动都崩。把这个目录挪开即可恢复测试。
- 从 Bash 工具后台启动的调试器会随那次调用结束而消失；追踪器和界面操作要放进同一个脚本里跑。
  `Csi.dll` 只在打开云文档时才加载，追踪器要在 `LOAD_DLL` 事件里按导出表名字认出它再布断点。
- 杀掉仍布着 int3 的调试器，断点字节会留在目标里，下一次命中就崩。

### 漫游设置的 415（`917db3d`）

`RoamingSoapService.svc` 的 POST 一直返回 `415 Unsupported Media Type`。Office 用
`WsCreateServiceProxyFromTemplate` 建代理：服务要求的东西（SOAP 1.1 信封、传输层寻址）写在模板的
**描述**里，是从服务元数据生成的；模板值只带一个缓冲区大小。Wine 只看模板值，请求以 SOAP 1.2 加
WS-Addressing 发出，SOAP 1.1 的服务对每一次写都回 415。

`tools/wsproxyprobe` 在本机起一个监听，把 Content-Type、SOAPAction 和报文原样打出来，在 Windows
上逐项实测：描述与模板值的通道属性合并使用；同一个属性两边都给就 `E_INVALIDARG`（值相同也不行）；
TCP 模板同理。顺着同一条路径补齐：代理写出的请求按通道编码（二进制编码只配 SOAP 1.2，否则
`E_INVALIDARG`，名字取静态字典，与 Windows 逐字节相同）；二进制报文标成
`application/soap+msbin1`（会话编码为 `msbinsession1`），不带 charset 和 action；SOAP 1.2 只在
传输层寻址时把 action 放进 Content-Type。写端还没有 UTF-16 和 MTOM 输出，这两种编码仍以 UTF-8
文本发出并如实标注。

还没做的：`HttpMediaTypeHeaderValue` 等 WinRT HTTP 类型、`SendRequestAsync` 仍未实现。

## 窗口互相透视：阴影窗口、换屏后的错位和翻转的 Z 序

用户报告：Word 的几个窗口之间、以及和其它原生程序的窗口之间"透视"——上层窗口上透出下层窗口的
东西，尤其是每个窗口周围那一圈半透明的边框。查下来是五个彼此独立的缺陷，前一个是主因，后四个在
换屏（手机远程转屏、换远程客户端）时叠上来。

### 主因：阴影条是 override-redirect 窗口（`a25275c`）

Office 窗口周围的软阴影是四个细长的分层工具窗口（类 `MSO_BORDEREFFECT_WINDOW_CLASS`，样式
`WS_POPUP`，扩展样式 `WS_EX_LAYERED|WS_EX_TOOLWINDOW`，无 owner），Office 用
`SetWindowPos(阴影, 主窗口, …)` 让它们在 Z 序里紧跟在自己窗口后面。winex11 把它们当成非托管窗口，
也就是 override-redirect，而 mutter 把 override-redirect 窗口一律放在所有托管窗口之上：后面那个
窗口的阴影压在前面窗口上，也压在别的程序上。

现在"紧跟在我们某个托管窗口之后的、无标题栏的分层工具弹出窗口"改为托管窗口，`WM_TRANSIENT_FOR`
指向那个窗口，类型 `_NET_WM_WINDOW_TYPE_UTILITY`，`WM_HINTS` input=False 且不声明
`WM_TAKE_FOCUS`（mutter 不会给它焦点）。mutter 让 transient 紧贴在父窗口之上、随父窗口升降；不取焦点
映射的窗口放在焦点窗口之下。为 ARGB 视觉重建 X 窗口时这个关系保留。虚拟桌面模式不受影响。

托管窗口会被窗口管理器"挪回屏幕内"，而贴着屏幕边的窗口，它的阴影本来就有一部分在屏幕外。
mutter 的约束没有全写在文档里，用 `tools/stripprobe/stripprobe`（在无头 mutter 上映射一条与阴影条
属性相同的窗口，读回它最终的位置）量出来：

| 要求的位置（屏幕 2560x1440） | mutter 放到 |
|---|---|
| 800x8 于 (192,-4) | (192,0)：上沿不许高于工作区 |
| 8x600 于 (-8,192) | (0,192)：不足 10 像素厚的一维必须完整在内 |
| 300x8 于 (2400,192) | (2260,192)：放得下的窗口首次映射时整体推回 |
| 2600x8 于 (-20,-8) | (-20,0)：比屏幕还长的一维不推，上沿照推 |
| 已映射后移到 (192,-4) | (192,0)：原本完整在内的窗口以后也被推回 |

对应 mutter 的 `constrain_partially_onscreen`（上沿不外扩；每一维至少留 `clamp(尺寸/4,10,75)`
在屏内）和 `constrain_fully_onscreen`（`require_fully_onscreen` 初值为真，此后等于"上次是否完整在内"）。
winex11 据此判断：阴影条完整在工作区内，或放不下而又满足前一条约束时才映射，否则保持未映射——被挪
开的阴影条比没有更糟，而 Windows 上屏幕外那一截本来也看不见。

**测量陷阱：会话不活动时 mutter 不映射无边框窗口。** 第一次验证时所有阴影条都 `IsUnMapped`，一度
以为是规则太严。用最小探针（`tools/stripprobe/undecorated`）复现：连一个只设了 `_MOTIF_WM_HINTS` decorations=0 的 400x300 窗口也不被
映射，直到被激活（`_NET_ACTIVE_WINDOW`）或加上装饰。原因是用户的图形会话在后台（`loginctl` 里
tty2 会话 `Active=no`，座位上活动的是 gdm；DP-1 已 DPMS off），舞台帧时钟不跑，mutter 的
`CALC_SHOWING`/`SYNC_STACK` 延迟任务挂在帧时钟上，一直不执行；Word 主窗口能出来只是因为激活路径
会同步刷新。之后所有验证都改在 `scripts/measure-desktop.sh` 那样的无头 mutter（独立总线）上做。

验证（无头 mutter，Word 三个窗口）：`_NET_CLIENT_LIST_STACKING` 里每个窗口的四条阴影紧跟在自己之上、
在更高的窗口之下，与 Win32 Z 序一致；截图里前窗右侧有渐变阴影，后窗阴影被前窗盖住；放一个纯红的
原生窗口压在 Word 右缘上，阴影条那几列读回全是 (255,0,0)；激活后面的窗口，它和它的阴影一起上来；
最大化时 Office 销毁阴影、还原时重建，新建的也正确挂接。

### 换屏时的四个缺陷

复现方法：无头 mutter 带两个虚拟显示器（`MEASURE_GEOM=2992x1440,1440x2992 scripts/measure-desktop.sh
start`），用 `scripts/monitor-switch.py` 轮流只启用一个，等价于手机转屏时 GRD 换虚拟显示器。

1. **被当成"移到屏外"的新位置（`82486d6`）。** 显示器被撤时 mutter 先把窗口挪到剩下的显示器上，
   这时 Wine 还不知道新屏幕，新位置落在旧屏幕外，winex11 的 `is_offscreen` 把它当成窗口管理器故意
   藏起来的：不告诉 Win32，并在之后每一次移动上叠加同样的偏移。转屏后 Word 的阴影条被放到屏幕底部、
   窗口左边 600 像素处，Win32 随后也被告知它们在那里。现在只有窗口管理器给的位置在**当前**屏幕上
   仍然在外时才保留偏移；换屏后所有可见窗口都与主机同步一次（原来只同步需要重新适配的窗口）。
2. **重建的 X 窗口没有 `_NET_WM_USER_TIME`（`139afd2`）。** 不激活地映射的窗口带 user time 0，告诉
   mutter 别给焦点、别放到焦点窗口之上。这个值缓存在窗口数据里，X 窗口为换视觉重建后没有再设，新
   窗口就没有这个属性，mutter 把它放到最顶：后面窗口的阴影横在前窗上。
3. **Expose 被当成"露在最上面"（`4be87be`）。** 窗口某部分收到 Expose 时，winex11 请服务器把它挪到
   Win32 里盖着那部分的窗口之上（`update_window_zorder`）。在合成管理器下每个窗口有自己的像素图，
   映射或变大时就有 Expose，与是否被别的窗口挡着无关。换屏时所有窗口一起改尺寸，逐个"挪到上面"，
   Win32 Z 序被整个翻过来：前台窗口在屏幕上最上面，在 Win32 里却在最下面。用 `tools/zorder/zwatch.exe`（每 20ms
   采样一次 Z 序）配合 `+timestamp` 把变化钉在一次只带 `SWP_NOZORDER` 的移动上，才找到这条旁路。
   现在合成管理器存在时（`_NET_WM_CM_Sn` 有属主，Xwayland 总是如此）Expose 只重绘。
4. **还没接上桌面的输出被当成屏幕（`7c83b5e`）。** 切换的那一刻主机已经报出新输出，但它的 source
   还没接入桌面（`state_flags 0`）、显示器矩形是空的。已有的"一刻没有显示器就不写下"只数显示器个数，
   这个中间态被提交，读回时没有主 source，读到的进程统统退到 1024x768 的替身显示器。现在只有接入
   桌面、有位置的显示器才算数。三轮横竖屏来回切换：回退 0 次，跳过中间态 1 次。

回归：user32 的 `win`、`msg`、`monitor`、`sysparams` 在同一无头显示上各跑改动前后两版。`msg`
（238）和 `monitor`（177，全是 Xwayland 下改分辨率不生效）失败集合逐条相同；`sysparams` 0 失败；
`win` 只有已知不稳定的 `test_activateapp` 焦点时序差异，重跑不再出现。

### 没能复现的：只重绘了上半截

用户会话里见过一次：竖屏手机上两个 Word 窗口同在 (143,0)、1452x1548，上面那个只有 y<774 的部分
是自己的内容，下面露出的是另一个文档的页面（像素逐行读出来确认过）。在无头环境里，窗口高度从
1444 变到 2992 的最大化转屏、普通窗口转屏，重绘都完整。那次正好处于上面第 4 条的回退窗口期
（窗口原来是 1440x774，774 恰是 1548 的一半），可能是 Office 按替身显示器布局过一次；修掉第 4 条后
没再能构造出来。再遇到时要抓：`tools/zorder/zorder.exe` 的 Z 序、`xwin-pixels.py` 读该 X 窗口本身的像素（区分
是 Word 没画还是合成器没更新）、以及当时日志里有没有 `Failed to read display config`。

## React Native 的实时通道：WebSocket 的关闭语义、co_await 的回程，以及如实回答合约之后

Word 里 React Native 的 AugLoop 通道要一条 WebSocket（`Windows.Networking.Sockets.MessageWebSocket`）。
从“能连上”到“会话真正跑起来”，经过了三层：WinHTTP 的 WebSocket 语义、MessageWebSocket 的 WinRT
外壳，以及 C++/WinRT 在 `co_await` 之后回到原线程的那一步。第三层修好之后又引出了第四件事：Wine 回答
“有哪些 API 合约”的方式。

### WinHTTP：关闭、失败和协议检查（`01cd3ea`）

全部用进程内服务器逐字节构造帧，在 Windows 与 Wine 上对照（`tools/wsprobe/probe_winhttp_ws.c`）：

- 对端的 close 帧只关它那一侧：本端直到自己 shutdown 前都还能发送。Wine 原来在收到 close 帧后就拒绝发送。
- 1005 表示“没有状态码”，发出的是空 close 帧；收到空 close 帧时报 1005。只有 close 帧可以携带的
  状态码（1000–1003、1007–1011、3000 以上）才能发出，其余一律 `ERROR_INVALID_PARAMETER`：被拒的
  shutdown 照样把本端关掉、但什么也不发；被拒的 close 则不改变 socket 的任何状态。
- 服务器违反协议即判连接失败：close 状态码非法、close reason 或文本消息不是合法 UTF-8（过长编码、
  代理区都算；序列跨分片可以，停在序列中途则不行）、控制帧分片或超过 125 字节、没有开头的续帧、
  上一条消息还没完又开新消息。这些情况返回 12152，并**立即断开连接**；连接自行结束则返回 12030。
  之后对这个 socket 的任何调用都以同一个错误失败，`QueryCloseStatus` 报 4317。
- ping 的负载要原样放进 pong 回传。Wine 原来既不读取也不回传，负载字节留在流里，被当成下一帧的帧头。
- 在另一个线程里阻塞着的同步接收：关句柄会取消它（12017）并重置连接；调用 `WebSocketClose` 则让它
  让路，由 close 自己去读 close 帧。
- 所有函数都先检查句柄，再检查参数。

### MessageWebSocket（`c25d94c`，测试 `ee9d50b`）

同样逐项对照 Windows（`tools/wsprobe/probe_ws.c`）：控制对象的默认值；握手请求带
`Cache-Control: no-cache`；Information 和 OutputStream 从一开始就存在、始终是同一个对象；多次写按
调用顺序逐帧发出（原来在线程池里并行，共用 winhttp 的同一个帧缓冲）。关闭方面：`Close` 之后写入和连接
立即返回 `RO_E_CLOSED`；非法状态码或超过 123 字节的 reason 返回 `E_INVALIDARG` 且不改变任何状态。
连接因断开或协议错误而失败时**不触发 Closed**，而是再来一次 MessageReceived，读取数据时返回
0x80072efe 或 0x80072f78，并且不写出参；之后的写入先返回成功、再异步失败；调用 Close 时什么也不发，
在返回之前同步触发一次 `Closed(1006)`。react-native-windows 正是靠这几条判断连接状态的。

### co_await 回不来：`IContextCallback::ContextCallback` 是桩（`8e683bc`）

上面都修好之后，AugLoop 仍然在头几分钟里连了 16 条 WebSocket。`+combase` 里每次连接后都有一条
`RoOriginateLanguageException(E_NOTIMPL)`，前面紧跟着 `thread_context_callback_ContextCallback` 的
FIXME，调用参数是 `ICallbackWithNoReentrancyToApplicationSTA` 的第 5 号方法。C++/WinRT 在
`co_await` 一个 WinRT 异步操作时会记下当前的对象上下文；操作在线程池里完成后，要通过
`ContextCallback` 把协程的剩余部分送回那个上下文执行。Wine 这里一律返回 E_NOTIMPL，于是每一次
`co_await` 都抛异常，不管操作本身成功与否。

对照 Windows（`tools/ctxprobe/probe_ctx.c`）实现后：

- 上下文按套间而不是按线程划分：MTA 的所有线程（包括 implicit MTA）共用一个上下文，每个 STA 各有一个。
- 回调总是在上下文所属的套间里执行：本套间内直接调用；进入 MTA 时，由一个进入 MTA 的线程执行；
  进入 STA 时，经该套间的窗口投递，与一次入站调用走同样的路径。
- 套间已经销毁时返回 `RPC_E_DISCONNECTED`。

探针在 Wine 上的输出与 Windows 逐行相同。部署之后，AugLoop 只建了 4 条连接，收发各约八十条消息，
会话建立起来了。

### 顺手补上的

- `Windows.System.Profile.SharedModeSettings`（`b21cb2a`）：共享 PC 模式关闭。与之相邻的 `RetailInfo`
  工厂也按 Windows 修正了类名、信任级别和 `GetIids`。
- `CLSID_AppVisibility`（`39dbf3c`，新的 twinapi.dll）：Word 主线程启动时要创建它两次。在 Windows 上，
  这个类在 ssh 会话里拒绝创建（0xc0000022），只能在桌面会话里测。为此给 `scripts/winrun.sh` 加了
  `--desktop`，通过一次性计划任务在已登录用户的桌面会话里运行探针。
- `CreateFile2` 漏传了 `dwSecurityQosFlags`，而且对任何扩展参数都报“忽略”，每次启动约 900 条（`8d0a88e`）。

### 如实回答合约之后：Office 改走 Windows.UI.Composition

Word 启动时会依次查询 `RoIsApiContractMajorVersionPresent("Windows.Foundation.UniversalApiContract",
7..15)`。Wine 的这个函数原来是桩，一律回答“没有”，Word 于是以为自己跑在 1809 之前的 Windows 10 上。
`RoIsApiContractPresent` 甚至没有导出。改为与 `ApiInformation` 共用同一张表（`76a6f19`，Wine 报的
Win10 2004–22H2 对应 UniversalApiContract 10）之后，Word 的 AirSpace 动画线程开始激活
`Windows.UI.Composition.Core.CompositorController`。激活失败，ShipAssert 标签 0x23ca613 对地址 0
写入，故意让线程崩溃；下一次启动便弹出“是否以安全模式启动”。

定位方法：`+seh` 抓到对地址 0 的写入，`rcx` 是一个 6 位十六进制数，那就是断言标签。在 Mso40UI 的
.text 中检索它的小端字节，前一字节 `ba` 即 `mov edx, imm32`。往前反汇编，找到失败的调用和它读取的
48 字符 HSTRING 字面量，也就是这个类名。

按 winmd 里的 IID 检索，Mso40UI 共引用 64 个 Composition 接口：视觉树、画刷、绘图表面、关键帧动画、
裁剪、阴影、形状、效果和背景画刷、InteractionTracker，以及手动提交用的 CompositorController；此外还有
`DCompositionCreateDevice3`。在 Windows 上，这些类全部注册在 dcomp.dll 里；Wine 的 dcomp 只有一个 47 行的桩。

**决定：保持新路径，不再靠“回答没有”把 Word 引回旧路径。**在 Wine 的 dcomp 里实现一个进程内的
Composition 引擎：用 D3D11/D2D 渲染视觉树，提供 HWND 目标和 DirectComposition 设备，并按 Office
实际用到的部分逐项对照 Windows。在它能用之前，Word 会一直卡在这个断言上。

## Windows.UI.Composition：dcomp 里的合成引擎

上一节的决定落地了：Wine 的 dcomp.dll 现在带着一个进程内的 Composition 引擎。合约如实回答时，
Word 通过它启动，窗口、功能区、各个窗格、“文件”后台视图和按键提示都能正常画出来。

### 引擎（`14b2c41`、`2024884`）

- **合成器**：只能在有 DispatcherQueue 的线程上创建。它不是 CompositionObject，是 agile 的。
  CompositorController 的合成器只在调用 Commit 时绘制，有改动时在自己的线程上异步触发 CommitNeeded；
  普通 Compositor 在改动后很快自动提交。
- **目标**：一个窗口最多一个 DesktopWindowTarget。提交时，每个目标的视觉树用 Direct2D 画进这个窗口上的
  一条交换链，使用树里所显示表面所在的 D3D 设备。
- **绘图表面**：在 D3D 纹理里保存像素，虚拟表面按 512 像素分块。BeginDraw 的更新矩形如果落在一块之内就原地绘制，
  否则画到一张暂存纹理上，EndDraw 时再拷回各块。
- **表达式**：解析一次成树，之后每帧求值。支持 C 的运算符、各种向量/矩阵/四元数类型、swizzle、`this.*`，
  以及 Windows 文档列出的函数。
- **动画**：表达式动画和关键帧动画可以挂在视觉对象、画刷、裁剪、属性集、InteractionTracker 和
  VisualInteractionSource 的属性上，包括 `Offset.X` 这样的分量。关键帧按时间推进，有关键帧动画运行时，
  由 DispatcherQueueTimer 每 16ms 在合成器线程上画一帧；ScopedBatch 和 CommitBatch 报告动画何时结束。
- **InteractionTracker**：Office 用表达式按内容大小算出 MaxPosition，位置按动画后的边界钳制。
  输入还不会驱动它。

每个对象创建时的样子、各个错误码、StartAnimation 接受什么，都按 Windows 桌面会话里的 probe 实测
（`tools/compprobe`、`tools/animprobe`、`tools/interactprobe`）。几条出乎意料的：

- 关键帧动画默认 Duration 是 250ms，StopBehavior 是 LeaveCurrentValue。
- 类型检查是严格的：`1` 不能给布尔属性，标量也不能给 Vector3。
- VisualInteractionSource 的两个 Rails 默认为真，Scale 默认为 0；一个视觉对象只能有一个源。
- 属性集的名字不区分大小写，名字一旦定了类型就不能改。
- RedirectVisual 在自己的原点重画它的源，源自身的 Offset 不计入（这一条是用 PrintWindow 读像素确认的）。

`dlls/dcomp/tests` 在 Windows 桌面会话里 401 项全部通过。Wine 下只剩经 PrintWindow 读合成像素的检查是
todo，那几处像素改用 mutter 截屏核对过。

### 一个一个补上 AirSpace 要的东西

办法是：部署 dcomp，带 `warn+dcomp,fixme+dcomp,warn+seh` 启动 Word，看 AirSpace 线程在 ShipAssert
（对地址 0 的写入）之前最后一条 FIXME 是什么，那就是下一个缺口。依次是：

1. VisualInteractionSource；
2. `Offset.X` 上的表达式（tracker 缺 IsPositionRoundingSuggested）；
3. CompositionConditionalValue；
4. VisualInteractionSource 的 Rails 要能被动画；
5. NineGridBrush；
6. VisualCollection 的 `IIterable<Visual>`；
7. 打开恢复文档时才用到的 DropShadow 和 RedirectVisual。

### 功能区空白、文字乱码：d2d1 的命令列表（`775f583`）

每个缺口都补完之后，功能区的图标、选项卡和恢复窗格仍然是空的。日志里 259 次
`d2d_device_context_EndDraw: Unimplemented for command list target`：NetUI 先把界面元素录进
ID2D1CommandList，再用 DrawImage 画到表面上，而 Wine 的 d2d1 只会录制，不会回放。现在 DrawImage 通过一个
内部的 command sink 回放已关闭的列表，EndDraw 返回上下文自己的错误码。

回放之后，文字变成了错字和方框，选项卡标签也串了位。根因是：录制时，字形索引、advance、偏移、run 描述，
以及位图、图像的矩形，都以**绝对指针**的形式存进列表自己的缓冲区，而缓冲区随着列表增长会 realloc，
之前那些命令的指针就全部悬空了。改成相对命令起点的偏移之后，文字就正确了。新测试专门录制一个会迫使列表
扩容的序列；它在 Windows 上通过，拿掉这个修复在 Wine 上失败。

### DispatcherQueue：定时器、专用线程和关闭（`4566ae2`）

合成的逐帧渲染需要 DispatcherQueueTimer，于是连同同一模块里相邻的缺口一起补齐：CreateTimer、
DQTYPE_THREAD_DEDICATED 真正的专用线程、ShutdownQueueAsync，以及 ShutdownStarting（带 deferral）和
ShutdownCompleted 两个事件。上游现有的测试去掉 todo 之后全部通过，新加的定时器测试在 Windows 上也通过。

### 测不到的

参照机 winref 的桌面会话看起来处于锁屏或断开状态：DWM 按需合成（PrintWindow 能用），但不走帧。
InteractionTracker 从不回调，EnsurePreviousCommitCompletedAsync 一直是 Started。
所以跟时间有关的合成行为在那台机器上测不出来，只能按文档语义实现；同步的 getter 和错误码照常能测。

### 滚轮滚不动：InteractionTracker 的状态机（`c6024d3`）

Word 文档用滚轮滚不动，只有滚动条在动。开 `+dcomp` 一看就明白了：Word 的文档画布用一个
InteractionTracker 做平移和缩放。`TryUpdateScale(0.99)` 就是状态栏上的 99%。翻页走的是
`TryUpdatePositionWithOption`，一直是好的；滚轮的平滑滚动则把一个 Vector3 关键帧动画交给
`TryUpdatePositionWithAnimation`，动画只有 1.0 处一个关键帧，时长 267ms。这个方法原来是桩，位置原地不动。

现在 tracker 是文档里写的那个状态机：Idle、CustomAnimation、Inertia，以及只有输入才能进入的 Interacting。

- **请求 ID 与回调**：每个 Try* 请求先取下一个请求 ID，Interacting 时被忽略。进入新状态时告诉 owner，
  带上把它带进去的那个请求的 ID；每帧变了的值也用这个 ID 报。
- **自定义动画**：作为一个 "motion" 运行，即由 tracker 自己在自己的值上跑的动画，而不是挂在属性上。
  它和 StartAnimation 一样校验、一样复制、一样计入批次。每帧读出后夹在上下界之内，文档说自定义动画总是被钳制。
  缩放动画绕给定的中心点进行。所有动画结束后回到 Idle，报的是启动它的那个请求。
- **立即更新**：`TryUpdatePosition`、`TryUpdatePositionBy`、`TryUpdateScale` 结束当前的一切运动。
  PositionUpdateOption 允许时，缩放动画可以继续。
- **惯性**：速度请求让它惯性滑行。每根轴每秒损失同一比例的速度，即衰减率，未设置时为 0.95；最后停在自然终点，
  或者第一个条件成立的惯性修饰器给出的位置，都在上下界之内。缩放在对数域里按同样的方式滑行。
  InertiaStateEntered 报告自然终点、修饰后的终点和初速度；中心点修饰器移动缩放惯性所围绕的点。
  Motion 修饰器还不执行。

同步能测的部分在 Windows 桌面会话里实测（`tools/trackerprobe`，Windows 11 build 29671）：

- 类型不对的动画（Vector2 或标量表达式给位置）返回 E_INVALIDARG，照样消耗一个请求 ID，但不写回。
- 越界的 ClampingOption/PositionUpdateOption 先写回 ID，再返回 E_INVALIDARG。
- StartAnimation 可以挂在 tracker 的上下界上，但不能挂在 Position、Scale、自然终点和速度上。
- 往结果指针传 NULL，Windows 直接崩溃。

那台机器的会话是锁屏的，合成器不出帧，所以随时间变化的部分只能照文档实现。对应的测试在没有帧的地方跳过：
dcomp 测试在 Windows 上 431 项通过、1 项跳过，在 Wine 上 474 项全部通过。

### 普通 Compositor 从不提交（`3e3e74d`）

写上面的测试时发现，直接激活的 `Windows.UI.Composition.Compositor`（不是 CompositorController 的那种）
从来没有提交过。新合成器按 Windows 的语义一开始就是"有改动"，而自动提交只在"没改动 → 有改动"时才排队，
于是它什么都没画过，动画也从没跑过，因为逐帧循环要从一次提交开始。现在创建时就把第一次提交排上。
AirSpace 的窗口用的是 CompositorController，所以 Word 的主界面一直没受影响。

### 另一台设备画的表面（`4cac468`）

普通合成器开始提交后，Word 启动时的一个窗口冒出 40 条 `render_surface ... is on another device`。
一个窗口目标用它的树里第一个表面所在的设备来画，其他设备上的表面此前一律被跳过。
而在 Windows 上，DWM 会把各个设备画的东西合成到一起。

现在的做法：

- **跨设备拷贝**：每个图块记录自己像素的版本，在目标的设备上保留一份镜像；版本变了，就经 CPU 从原设备读回再上传。
- **加锁**：一帧开始前，先拿到树里所有表面所在设备的锁，再拿合成器的锁。
- **迟到的表面**：加锁之后才挂进树的表面等下一帧再画。

用 Ctrl+W 触发的“保存对此文件所做的更改？”对话框完整画了出来。

### 滚动时的 GL 报错：颠倒的裁剪矩形（`9ba5d82`、`da08093`）

滚轮能滚之后，每次滚动都会刷出几百条 `GL_INVALID_VALUE in glScissorArrayv: width or height < 0`。
AirSpace 在滚动时设置了下边在上边之上的裁剪矩形，wined3d 把负高度原样交给了 OpenGL。
`tools/scissorprobe` 在 Windows 上用 WARP 和硬件驱动、D3D11 和 D3D10 各测了一遍：

- 只要有一个矩形颠倒，整次 RSSetScissorRects 就被丢弃，同一次调用里的正常矩形也一起丢掉；
  RSGetScissorRects 读回的仍是之前的，绘制也按之前的裁剪。
- 空矩形（left 等于 right）会被保留，并把一切都裁掉。

Wine 原来的结果取决于 GL 上一次留下什么：清屏会重置裁剪，所以清屏之后的那次绘制根本没被裁剪。
现在按 Windows 的方式丢弃这次调用，探针在 Wine 上的输出与 Windows 逐字一致。
顺带发现 D3D10 的 RSGetScissorRects 在 rects 非空时不回写 count，而 Windows 会回写成实际数量，也一并改了。

### 测量环境的坑：会话的 DISPLAY

最大化的 Word（1440x2992）只画到第 1080 行，下面露出桌面。原因在测量脚本：它先用 regsvr32 注册 dcomp，
这一步继承了 shell 的 `DISPLAY=:77`（一个 1920x1080 的 Xvfb），于是这个 Wine 会话的 wineserver 和 explorer
桌面都在 :77 上，屏幕尺寸也取自 :77，而 Word 的窗口在 :2 上。winex11 把窗口表面裁到它认定的虚拟屏幕以内，
就只剩 1080 行。**启动 wineserver 的那个进程决定整个会话的显示**，会话里的所有进程必须用同一个 DISPLAY。
`scripts/word-iter.sh` 现在也在 :2 上启动这一步。

### 缩放时的 d2d1：Flush、绘制状态和交换链（`2e4d6ca`、`6599737`）

Ctrl+滚轮缩放时，Word 每秒几十次调用 ID2D1DeviceContext::Flush，而它原来是个桩：先 Present 目标，再返回
E_NOTIMPL。`tools/d2dflushprobe` 用 DC 渲染目标（硬件、软件两种）在 Windows 上测清楚了：

- **不 Present**：Flush 不交付像素，DC 要到 EndDraw 才拿到像素。
- **绘制之中**：返回至今的第一个错误及出错时的标签，并清掉它。
- **绘制之外**：Flush 和 EndDraw 都返回 D2DERR_WRONG_STATE，带的是当前标签。
- **错误的生命周期**：第一个错误一直保留到 Flush 或 EndDraw 报出为止，出错后的绘制不执行。
  BeginDraw 不清错误，所以在 BeginDraw 之外画一笔，下一个 EndDraw 会报出来；嵌套的 BeginDraw 也一样。
- **Push/Pop 不配对**：没有 push 就 pop，或者 EndDraw 时还有未 pop 的，是 D2DERR_PUSH_POP_UNBALANCED；
  pop 的种类不对（压的是裁剪，弹的是图层）是 D2DERR_POP_CALL_DID_NOT_MATCH_PUSH。
- **Present 失败**：不带标签。

现在上下文记录自己是否在绘制之中，维护一个裁剪与图层共用的 push 栈，只保留第一个错误；上游一个
`todo_wine`（绘制中 BindDC 应失败）也随之通过。

另一个问题：d2d1 每次绘制都会换入自己的 D3D 设备上下文状态，把目标的 RTV 绑在里面，之后从不解绑。
于是窗口改尺寸时，交换链的旧后备缓冲还被引用着，wined3d 报 `Something's still holding back buffer 0`，
旧缓冲也泄漏了。Word 启动时这条 ERR 出现 27 次。现在切换目标时清掉这个状态，这条 ERR 不再出现。

### PrintWindow：让窗口自己重绘（`d216875`）

Word 画每一帧都要问 `IsWindowRedirectedForPrint`：被打印时它改用 GDI 画，而不走合成。`tools/printredirectprobe`
在 Windows 11 上测到：

- **消息**：PrintWindow 根本不发 WM_PRINT。窗口和它的子窗口照常重绘，WM_PAINT 在前，WM_NCPAINT 和
  WM_ERASEBKGND 由 BeginPaint 发出；从别的线程调用也一样。
- **重定向标志**：重绘期间 `IsWindowRedirectedForPrint` 只对被打印的那个窗口返回 -1，子窗口返回 0；
  带 PW_RENDERFULLCONTENT 时也会重绘，但返回 0。
- **错误**：窗口无效时返回 ERROR_INVALID_WINDOW_HANDLE；DC 为 NULL 时照样返回 TRUE，错误为 ERROR_INVALID_HANDLE。

Wine 原来发 WM_PRINT。现在的做法：给被打印的窗口加一个窗口属性（跨进程也看得见），立即重绘，
再把窗口（或客户区）的内容拷出来。交换链里 D3D 画的内容还不在拷贝结果里。

### 其他 Word 启动时撞到的缺口

- **GetFileInformationByHandleEx**（`2cacc70`、`d1b0398`）：Word 打开文件时要 FileStorageInfo，另外还有
  FileCaseSensitiveInfo、FileNormalizedNameInfo、FileRemoteProtocolInfo，原来都返回 ERROR_CALL_NOT_IMPLEMENTED。
  `tools/fileinfoprobe` 测了 Windows 的回答。扇区大小、有无寻道惩罚、是否支持 TRIM、分区是否对齐，
  都取自文件所在块设备在 sysfs 里的描述；btrfs 这类文件系统则从挂载信息找到源设备。缺的几个结构体一并加进了头文件。
- **VirtualDesktopManager**（`79ed618`）：Word 用它问窗口是否在当前虚拟桌面上。按 `tools/vdmprobe` 测到的
  Windows 行为，在"只有一个桌面"的前提下实现；桌面 ID 存在 explorer 存它的注册表位置。
- **xmllite 的 IsDefault**（`e3b1eaf`）：Wine 不解析 DTD 内部子集，所以不会有来自 DTD 默认值的属性，
  如实返回 FALSE，不再每次启动刷一万两千条 FIXME。

### 还没做的

- 输入驱动 InteractionTracker（触摸板、滚轮重定向、触摸）：状态机和惯性已经有了，但还没有输入接进来；
  惯性的 Motion 修饰器和 NaturalMotion 也还不执行。
- 真正的模糊：阴影现在是几层矩形近似。
- d2d1 的图层（PushLayer 在 Wine 里是桩），所以组不透明度和非矩形裁剪都只是近似。
- Effect/Backdrop 画刷和 ShapeVisual。
- DirectComposition 设备（`DCompositionCreateDevice*`）。
- PrintWindow 拷不到交换链里的内容（PW_RENDERFULLCONTENT），以及 WS_EX_NOREDIRECTIONBITMAP。
- Windows 拼写检查 API（ISpellCheckerFactory），Word 会尝试创建它；Word 自己的拼写检查不靠它，用 Office 的
  `PROOF\MSSP7EN.LEX`，在 Wine 下可用（`tools/officeautomationprobe/word-spell.vbs`，2026-09-28）。——已实现，见文末
  “Windows 拼写检查 API”一节。
- 手动提交模式下，动画帧画的是当前的树，不是上次提交的那一版。

## Word 启动时的系统调用：电源通知、周期计数、DPI 托管与挂起的 I/O

合成引擎之后，Word 启动日志里剩下的 FIXME 多是一批"小"系统调用：它们返回了什么都不影响 Word
能不能起来，但每个都答错了。`tools/sysprobe`、`tools/powersettingprobe`、`tools/cycleprobe`、
`tools/dpihostprobe`、`tools/iopendingprobe` 在 Windows 11（build 29671）上测了它们的回答，
Wine 实现逐条对照；新加的 Wine 测试都在 Windows 上跑过。

### 电源设置通知（`a1de13b`、`3a0296e`、`6b0cdfe`）

Word 启动时为六个电源设置注册窗口，Wine 原来返回 `0xdeadbeef` 且什么也不说。Windows 的做法：

- 注册后很快、从**另一个线程**把当前值告诉注册者——窗口收到**发送**的 `WM_POWERBROADCAST` /
  `PBT_POWERSETTINGCHANGE`，`DEVICE_NOTIFY_CALLBACK` 的回调被调用——之后每次变化再告诉一次。
- 值：电源来源、电池电量（笔记本才有）、当前方案与其"性格"（都是 Balanced
  `{381b4222-…}`，这台机器只有这一个方案）、显示器和控制台显示（会话锁着时为 0）、节能模式、
  空闲后台任务（DWORD 2，文档说它没有意义）。离开模式没开时什么都不发，不认识的设置也一样。
- 注册成功把 last error 置 0；`Unregister*(NULL)` 失败并置 `ERROR_INVALID_PARAMETER`；
  flags 只认 0/1/2；服务句柄不属于本进程时是 `ERROR_SERVICE_NOT_IN_EXE`；挂起/恢复注册不接受服务句柄。

Wine 现在在 user32 里维护真正的注册表：值来自 `NtPowerInformation`，只有一个 Balanced 方案；
电源来源和电量有人注册时每 5 秒看一次，变了就告诉。powrprof 的 `PowerSettingRegisterNotification`、
`PowerRegisterSuspendResumeNotification` 只收回调，交给 user32；`PowerRegisterForEffectivePowerModeNotifications`
从另一线程告诉 Balanced，补上了缺失的 `PowerUnregisterFromEffectivePowerModeNotifications`；
`PowerGetActiveScheme`、按方案的 `PowerEnumerate` 和 `PowerReadFriendlyName` 如实描述那一个方案。
`PowerDeterminePlatformRoleEx` 读 SMBIOS 机箱类型（`3a0296e`）。

没做的：服务收不到通知（还没有投递到服务控制处理函数的路径，未知的服务句柄也照样接受）；Wine 看不到系统挂起，挂起/恢复注册
永远不会被通知；会话锁定/解锁同理（`WTSRegisterSessionNotification` 只做了参数检查，`1cc1ffa`）。

顺带修了 ntdll 读电池（`13cd9f4`）：很多笔记本的电池在 sysfs 里只有 `energy_*`/`power_now`（μWh、μW），
Wine 只读 `charge_*`，于是容量为 0，`GetSystemPowerStatus` 永远报 100%。

### 周期计数与别的进程的 CPU 时间（`3f198ac`）

Word 启动时查几百次 `ProcessCycleTime`（原来是返回 0 的桩），也查 `QueryThreadCycleTime`（原来不实现）。
`tools/cycleprobe` 测到：Windows 的计数是线程运行期间走过的时间戳计数器（TSC）周期——对着线程的内核+用户
时间看，自旋 0.3 秒和 3.3 秒的线程都是每 100ns 约 281 个周期；两次查询计数必然增长；
`CurrentCycleCount` 对非当前线程就是查询那一刻的 rdtsc。

Wine 的实现：运行时间取系统能给的最细粒度——本线程/本进程用 `CLOCK_THREAD_CPUTIME_ID`/`CLOCK_PROCESS_CPUTIME_ID`，
别的线程读 `/proc/<pid>/task/<tid>/schedstat`（纳秒），别的进程用 `clock_getcpuclockid`——乘以 TSC 频率
（进程内对着单调时钟校准一次，2ms）。注意 TSC 频率不是睿频上限：这台 Ryzen 9 5900X 的
`cpuinfo_max_freq` 是 4.95 GHz，TSC 是 3.7 GHz。

同一个函数里 `ProcessTimes` 对任何进程都返回**本进程**的时间（原注释 "user/kernel times only work for current
process"）。新增的 `get_process_times` 请求（与 `get_thread_times` 对称）交回仍在运行的进程的 Unix pid，
从 `/proc` 读它的时间；本进程改用 `getrusage()`，精度从时钟滴答（10ms）到微秒。这改了服务器协议（1812）。

### DPI 托管行为与非客户区缩放（`d2a2350`、`c8a9043`）

- `SetThreadDpiHostingBehavior` 记在线程信息里并返回旧值；非法值返回 `DPI_HOSTING_BEHAVIOR_INVALID` 并置
  `ERROR_INVALID_PARAMETER`。窗口记住创建时线程的行为，`GetWindowDpiHostingBehavior` 如实回答。
- `EnableNonClientDpiScaling` 只在按显示器感知线程创建的那个窗口的 `WM_NCCREATE` 里成功；PMv1 还把 last error
  置 0，PMv2（本来就自己缩放边框）连错误码都不碰；其余情况不说原因地失败，NULL 窗口是 `ERROR_INVALID_WINDOW_HANDLE`。
- 测这个时发现 wineserver 给按显示器感知线程创建的顶层窗口一律存 PMv1（0x12），Windows 是 0x22。窗口过程在
  窗口的感知上下文里运行，于是 PMv2 窗口在自己的 `WM_NCCREATE` 里也以为自己是 v1。现在版本号来自线程。

### 其余几个

- **`ThreadIsIoPending`**（`20cdaee`）：线程池退役线程前要问——Windows 把线程发起的 I/O 挂在线程上，线程退出就取消。
  服务器现在在 `get_thread_info` 里说线程有没有还没完成的 async：重叠读挂起（绑不绑完成端口都算）、
  同步读阻塞中为 TRUE，完成或取消后为 FALSE；需要 `THREAD_QUERY_INFORMATION`。协议 1813。
- **`TokenUIAccess`**（`3f64ea2`）：原来答 1。只有为 UI 访问启动、签名且装在受信任位置的进程才有，普通进程是 0。
- **`RegisterTouchHitTestingWindow`、`ChangeWindowMessageFilterEx`**（`aafc0a6`）：没有触摸输入、没有 UIPI，
  但检查与回报按 Windows：自己的窗口什么值都接受，NULL 是 1400，别的进程的窗口是 5；消息过滤记在窗口属性里，
  `ExtStatus` 说明是否已放行/已拦截，重置会忘掉。

### 测量上的一个坑：printf 实参里的 GetLastError

`printf("... %p error %lu", Call(), GetLastError())` 测出来的是**调用之前**的错误码：C 不规定实参求值顺序，
x64 上 mingw/MSVC 从右往左求值。sysprobe 和 powersettingprobe 因此把"置 87""置 0"测成了"不改错误码"，
并被写进了 Wine 测试——在 Wine 上照样通过，直到把测试拿回 Windows 跑才暴露（四处）。探针现在每个调用单独一句，
结果和错误码先存进局部变量。**写完 Wine 测试一定要回 Windows 跑一遍**，这是唯一能发现探针自身错误的环节。

### 部署：协议号变了

这一批两次改了服务器协议。协议号一变，`/opt/wine-altars` 里的 `wineserver` 必须和 `ntdll` 一起换；
请求编号变了时（新增请求插在中间），所有直接发请求的模块也要换：`ntdll.so`、`win32u.so`、`winex11.so`、
`winewayland.so`、`nsiproxy.so`，以及 PE 的 `ntoskrnl.exe`、`nsiproxy.sys`、`winex11.drv`、`winewayland.drv`。
`scripts/word-iter.sh` 只换列出的 DLL，这些要先手工换好。

## Word 启动时的第二批：加密、账户全名、MSXML 的解析设置、语言与进程策略

上一批之后，Word 启动日志里的 FIXME 从 800 多条降到 509 条。这一批补的是剩下的大头：它们大多不影响 Word
能不能起来，但每个都答错了，有的每次启动要错几百次。和上一批一样，每一项都先用探针在 Windows 11 上量
（`tools/oidfuncprobe`、`usernameprobe`、`bcryptobjectprobe`、`msxmlpropprobe`、`saxdtdprobe`、
`languageprobe`、`procpolicyprobe`、`rtclassprobe`、`exportnameprobe`），再逐条对照 Wine 实现；新写的 Wine 测试也都拿回 Windows 跑过。

### crypt32：找到了解码器，却说没有（`f8c4a86`）

Word 解码 `SPC_INDIRECT_DATA_OBJID`，而 wintrust 只在 `CryptDllDecodeObject` 函数集下注册了它的解码器。
`CryptDecodeObjectEx` 在那里找到并解开了，但之前先说了 "Unsupported decoder"，还把在另一个函数集里没找到时的
`ERROR_FILE_NOT_FOUND` 留成了这次成功调用的 last error；编码一侧更早，对 crypt32 没有内建的每个结构都先这么说，
一个 DLL 都还没查。

现在两个函数集都没有时才说；在另一个集里找到的函数会清掉错误码——Windows 成功后是 0（两个函数集都是）。
谁都不认识的结构，四个调用都以 `ERROR_FILE_NOT_FOUND` 失败，和 Windows 一样。

### 账户的全名（`65b0ecb`、`4573d8d`）

Word 向 `GetUserNameExW` 要 `NameDisplay`、`NameUserPrincipal` 和 `NameDnsDomain`，每次都打印 not implemented。
`tools/usernameprobe` 在 Windows 11 上测了一个不在域里的用户（探针只打印名字的形状，不打印名字本身）：

- `NameDisplay` 是账户的全名；不给缓冲区时以 `ERROR_MORE_DATA` 失败，并给出含结尾 0 的大小。没有全名的账户映射不到。
- 域账户才有的名字（用户主体名、DNS 域在内）以 `ERROR_NONE_MAPPED` 失败——不在域里时这就是正确答案，不再是 FIXME。
- 格式号之间的空号（4、5、11）也映射不到，Wine 原来答 `ERROR_INVALID_PARAMETER`；`NameSurname` 之后才是参数错误。

全名从哪来：Windows 为本地账户保存全名，Unix 账户的全名是 GECOS 的第一栏。从 Unix 启动的第一个进程用
其余 `WINE*` 变量所描述的账户查一次，放进 `WINEUSERFULLNAME`，它启动的进程都继承；没有全名的账户就没有这个变量。
`NetUserGetInfo` 的 level 10 对当前用户给同一个全名。Windows 还会把全名拆成 `NameGivenName` 和 `NameSurname`，
这里不知道怎么拆，映射不到。

### bcrypt 的对象缓冲区（`0fa9481`）

Office 给每个哈希和密钥都传一块对象缓冲区，Word 启动时打印 270 次 "ignoring object buffer"。
`tools/bcryptobjectprobe` 测到 Windows 把对象放在这块缓冲区里，返回的句柄就指向缓冲区内部；Wine 把对象另外存放，
除了句柄指向哪里，调用者看不出区别。Windows 检查的，现在 `BCryptCreateHash`、`BCryptDuplicateHash`、
`BCryptGenerateSymmetricKey`、`BCryptImportKey`、`BCryptDuplicateKey` 同样检查：

- 长度为 0 的缓冲区：`STATUS_BUFFER_TOO_SMALL`；
- 给了长度却没给缓冲区：`STATUS_INVALID_PARAMETER`。

比 `BCRYPT_OBJECT_LENGTH` 短的缓冲区仍然收下。Windows 对哈希最多允许短 22 字节，对 AES 密钥 30 字节——这是它为对齐
留的余量——再短就拒绝。也和 Windows 一样，算法没以 HMAC 打开时，`BCryptCreateHash` 拒绝 secret，长度为 0 也拒绝。

### MSXML 的解析设置：ProhibitDTD 与 MaxElementDepth（`f30fb50`、`20b94d4`）

Word 给文档设 `ProhibitDTD` 和 `MaxElementDepth`，给 SAX 读取器打开 `prohibit-dtd`。msxml 原来忽略这两个和同类的
另外六个设置，也不把值交回；SAX 读取器存下了值，被问时却说不认识这个特性，照样解析 DTD。
`tools/msxmlpropprobe` 和 `tools/saxdtdprobe` 在 Windows 11 上测到：

- 默认值：MSXML 6 禁止 DTD、允许 256 层，不允许 XSLT 脚本和 `document()`；MSXML 3 允许 DTD、5000 层，两者都允许。
  `MaxXMLSize` 都是 0；`ResolveExternals`、`NewParser`、`NormalizeAttributeValues`、`UseInlineSchema` 在 6 里关着，
  3 则根本不回答（`E_FAIL`）。
- 值会转换：`VT_I4` 的 1 变成 `VARIANT_TRUE`，字符串 `"5"` 变成 5；负的深度是 `E_INVALIDARG`。
- 禁止 DTD 时出现文档类型声明（不论声明了什么），加载失败：MSXML 3 是 `0xc00ce556`，6 是 `0xc00ce584`；
  嵌套超过限制：3 是 `E_ABORT`，6 是 `0xc00ce586`。0 表示不限。
- SAX 的 `prohibit-dtd` 在 MSXML 3 默认关、6 默认开。打开时遇到文档类型声明（有没有内部子集都一样）解析就停：
  错误处理器收到 fatal error，`parse` 返回同一个码——MSXML 3 是 `0xc00ce556`，位置在名字处；6 是 `0xc00cee4e`，
  位置在 `DOCTYPE` 关键字末尾。

现在这些设置都保存，按 Windows 的方式交回；决定文档能是什么样的那两个，在解析器的回调里执行。MSXML 允许的层数
高于 libxml2 自己的 256 层上限时，解除后者（`XML_PARSE_HUGE`）。解析错误目前只给错误码，没有原因文字。
原来标着 todo 的 `NormalizeAttributeValues`、`MaxElementDepth` 测试现在通过。

### 把文档当输出流（`7c7f2b7`）

`f404cd6` 让文档可以当流来写、提交或释放时加载写进去的内容，并让 `MXXMLWriter` 在文档结束时提交输出流。
测试里暴露了两个后果：输出目标是文档的 XSL 处理器往文档的流里写了，却从不提交，于是 `transform()` 之后文档是空的，
而 Windows 里文档拿到了结果；`MXXMLWriter` 提交所有输出流，Windows 一个都不提交——调用者自己的流被 `Commit` 了六次。
现在处理器和写入器只告诉文档自己的流"输出完了"（按它是什么认出来），别的流一概不碰。

### webservices 的结构选项（`cc8233a`）

Office 写的结构用 `WS_STRUCT_IGNORE_TRAILING_ELEMENT_CONTENT` 和 `WS_STRUCT_IGNORE_UNHANDLED_ATTRIBUTES` 描述，
Word 启动时 154 次写入每次都说不支持这两个选项。两者都只说读取时跳过什么，不改变写出的内容；读取本来就会跳过
没有字段描述的属性，也就是第二个选项要的。

### Language 的静态方法（`8e5778b`）

Word 向 `Windows.Globalization.Language` 的工厂要 `ILanguageStatics`，原来没有。`tools/languageprobe` 测到：

- `IsWellFormed` 按 BCP 47 的语法判断，grandfathered 标签整体接受，不分大小写。Windows 自己多两条：4 到 8 个字母的
  语言子标签不接受；同一个单字母扩展出现两次不合格式，而重复的变体可以。`NULL` 不合格式，但不是错误。
- `CurrentInputMethodLanguageTag` 是线程当前键盘布局的语言，写成语言标签；中文带上文字子标签：locale 是 `zh-CN` 时
  答 `zh-Hans-CN`。

`ILanguageStatics2` 的 `TrySetInputMethodLanguageTag` 把线程切到该语言已安装的键盘布局，没有这样的布局就答否。

### 进程的缓解策略、电源节流与堆（`ccb27bd`、`73903f1`、`8ac455a`）

Word 查询自己和它启动的进程的缓解策略，设置其中两个，并设置自己的电源节流状态。原来查询返回 TRUE 却什么都不写，
设置什么也没做就返回成功，`GetProcessInformation` 不认识电源节流这个类。`tools/procpolicyprobe` 在 Windows 11 上测到：

- 64 位进程一开始就有 ASLR（bottom-up、高熵），shadow stack 的 API 不在进程内提供；其余策略一开始都关着。
  ASLR 运行中不能改（拒绝访问）；其余策略只能加，不能撤（拒绝访问）。
- 一个 DWORD 大小的策略就用一个 DWORD 查询和设置；DEP 用它自己的结构大小，64 位进程设置 DEP 时答不支持。
- 电源节流状态按版本查询，只能查本进程（别的进程是参数错误），答的是最后一次设置的值。

ntdll 的 `ProcessMitigationPolicy`、`ProcessPowerThrottlingState` 现在为本进程保存并回答这些；别的进程答它启动时的值。
策略只记录，不执行；缓解选项的掩码不知道该是什么。

`HeapEnableTerminationOnCorruption` Word 也问，Wine 原来答这个类没实现。它对整个进程生效：64 位进程从一开始就有，
任何进程都可以用 `HeapSetInformation` 打开；Windows 答一个 ULONG 的 TRUE，现在一样。堆损坏在 Wine 里仍然只报告，
不结束进程。

### GetGuiResources（`9ddb247`）

原来是返回 0、错误码 `ERROR_CALL_NOT_IMPLEMENTED` 的桩；Word 用它查自己的 GDI 对象数。现在和 Windows 一样由
`NtUserGetGuiResources` 实现：数本进程句柄表里的 GDI 对象（不含库存对象），会话表里本进程的 USER 对象，
`GR_GLOBAL` 时是整个会话的；峰值是数到过的最大值。和 Windows 一样，进程为 NULL 是 `ERROR_INVALID_PARAMETER`，
不认识的 flag 返回 0 且不动 last error。别的进程的 GDI 对象这里看不到。

### AppPolicy（`5c38bf1`）

这里的进程从不属于某个包，每个 `AppPolicyGet*` 回答的正是 Windows 给桌面进程的值：不做线程初始化、用 `ExitProcess`
结束、经典桌面窗口模型。只是去掉了 FIXME。

### 名字带点的内置 DLL 找不到自己（`184e8e1`）

改好的 `ILanguageStatics` 进不了 Word：`WINEDEBUG=+module` 显示加载器找的是 `"windows.globalization"`，
找不到内置库，于是映射了前缀 `system32` 里的那份副本——它和前缀上一次更新一样旧。

前缀里的内置 DLL 副本只是个"指针"：加载器读它导出目录里的名字，再去 Wine 的安装目录（或构建目录）里找同名的内置库，
所以只换 `/opt` 里的 DLL 就能生效。可 winebuild 从 spec 文件取模块名，只在名字里**没有点**时才补 `.dll`：
`windows.globalization.spec` 于是成了 `windows.globalization`，安装目录里没有这个文件。
`tools/exportnameprobe` 读了 Windows 上这些文件的导出名：`Windows.Globalization.dll`、`twinapi.appcore.dll`、
`WINSPOOL.DRV`、`ntoskrnl.exe`……都带扩展名。受影响的正好是名字里带点的 33 个模块：32 个 `windows.*` 加上 `twinapi.appcore`；装好的文件里，别的模块导出名都和文件名一致。

现在 winegcc 在输出文件名正是 spec 名加扩展名时，把输出文件名交给 winebuild 作模块名；显式给了名字的（kernel32 的
`-Wb,-F,KERNEL32.dll`）照旧。重新链接后逐个比对了全部 1341 个有导出目录的模块：变了的正好是这 33 个的两种架构，
和 Windows 只差大小写（Wine 的文件名一律小写）。
已有的前缀在下一次更新（`wineboot -u`，换了 Wine 版本时会自动做）时拿到新副本；在那之前，或者像这里只换 DLL 的部署，
要把这些模块的副本也复制进前缀的 `system32`/`syswow64`。这以后，它们和别的内置 DLL 一样，只换安装目录就能生效。

### 还没做的

这一批之后 Word 启动打印 502 条 FIXME（上一批之后 509 条，每次启动因渲染次数不同略有出入）。按条数排：

- `d3d11_device_context_Map` 忽略 `D3D11_MAP_FLAG_DO_NOT_WAIT`（57）：要知道 GPU 是否还在用这个资源，得靠 wined3d 的命令流。
- xmllite 的 `MultiLanguage` 属性（35）、`RoGetActivationFactory` 不看线程模型和激活方式（每个类都打印一次）。
- ETW：`EventRegister` 发的是假句柄，`EventSetInformation`、`EnableTraceEx2`、`StartTraceW` 都是桩，Word 注册了 15 个提供者。
- `get_dummy_preferred_ui_language`（18）、msctf 的组合与输入范围、`IShellItem2::GetPropertyStore`、`Wer*` 注册、
  netprofm、appx 和 useractivities 各缺一个接口、dwmapi 的属性 21 和 33，以及一串一两次的。

## Word 启动时的第三批：界面语言列表与 XML 字符编码

第二批里的两个高频 FIXME 不是同一类问题：`get_dummy_preferred_ui_language` 把资源回退列表缩成一个
locale；xmllite 虽然把 `XmlReaderProperty_MultiLanguage` 保存下来，却不使用它，也没有按 XML 声明切换编码。
在 Windows 11 实测时，先分开问“名单是什么”和“字节怎样解码”，再分别实现。

### 首选界面语言不是当前 locale 的别名（`907521e`、`d60a799`）

`tools/uilangprobe` 下的探针分别询问 user、system、process、thread 列表，设置线程/进程语言后重问，
还按语言检查父级、过滤器和缓冲区。Windows 上，用户和系统各有 UI 语言与回退的 `en-US`；线程的合并名单
先有自己设置的语言，再有进程名单，再有用户语言和它的 `LOCALE_SPARENT` 父链。以中文为例，
`zh-CN` 的父级依次为 `zh-Hans`、`zh`。`MUI_MERGE_SYSTEM_FALLBACK` 为每一项加入父链，
`MUI_THREAD_LANGUAGES` 则只答线程自己的名单。线程与进程各自最多保存五项，合并结果可以更长；名单按顺序去重，不认识的语言名被略过。

现在 ntdll 分别保存线程和进程名单，在查询时依标志合并；线程退出时释放自己的名单。控制台与复杂文字过滤器
会从线程名单排掉不能用的语言，并在它自己的名单末尾放入回退的 `en-US` 或可用的控制台语言；合并名单则先接
上用户语言，再接被过滤项的替代。`GetThreadUILanguage` 读第一项，`SetThreadUILanguage` 更新线程名单，
不再“返回成功但什么都不记”。参数、计数、两层 NUL 与缓冲区不足时的行为都由探针覆盖。

边界：Wine 自带的 locale 数据与这台 Windows 的部分语言文字分类并不完全相同（例如 `sa-IN`），而且这里
还没有从 Windows 式用户语言设置维护一份独立于 Unix locale 的持久首选语言列表。旧的 kernel32 `thread`
测试中 `ThreadIsIoPending` 的 `todo_wine` 已经不再需要（`d60a799`）。`locale` 测试在 Wine 的 zh_CN 与 en_US
环境都没有失败；原生测试里的既有环境失败与新增断言分开核过。

### XML 声明决定真正的解码方式（`efd7c40`）

`tools/xmlmlangprobe` 在 Windows 上以同一段字节试 `windows-1252`、ISO-8859、GB2312、Shift_JIS 等声明，
分别给不给读取器 `IMultiLanguage2`。xmllite 本身懂 UTF-8、US-ASCII、Windows 1250–1258 与
ISO-8859-1–9；其它名字向传入的 MultiLanguage 对象查询。声明要求 UTF-16 一类宽字符但流是字节，
或宽字符流却声明字节编码，返回 `MX_E_ENCODINGSWITCH`；不认识的编码在没有对象时返回
`MX_E_ENCODINGSIGNATURE`，对象也不认识时返回 `MX_E_ENCODING`。

读取器现在解析完声明再决定码页，保留跨读取块的多字节字符；无效 UTF-8 字节不再无声地被替换成普通文本，
而是令 XML 字符检查失败。`MultiLanguage` 属性仍按 COM 引用计数保存与交回，但现在也参与转换。
已有 reader/writer 测试和新增声明编码测试在 Wine 上均无失败，新增测试在 Windows 上也无失败。
**未解决：** `CreateXmlReaderInputWithEncodingName` 强制指定编码的路径，在那台 Windows 上有一处与
XML 声明路径不同的解码行为；现阶段没有把它混进已经确认的声明规则里。

### mlang 的两代字符集查询不能混为一谈（`9443b55`）

`tools/charsetprobe` 同时调用 `IMultiLanguage::GetCharsetInfo` 与 `IMultiLanguage2::GetCharsetInfo`。
后者的名称/家族码页/互联网编码表（包括 `latin1`、`ISO_8859-1`、`UTF-16` 等别名）已按 Windows 实测
补入；前者主要查 MIME 注册表，同一个名字可能给不同码页，不能简单转发到后者。Wine 也在 MIME 注册表
补上 `x-user-defined`。新接口的探针输出逐行匹配；旧接口的少数别名与 Windows 仍有差别，不能说它已
完全相同。新增 mlang 测试在 Wine、Windows 上均无失败。

重装这三项模块并启动 Word 后，中文界面、恢复窗格与文档正文均正常绘制；这次启动的 FIXME 中不再出现
`get_dummy_preferred_ui_language` 和 `Ignoring MultiLanguage`。ETW 的提供者注册与会话本身是下一项，
不能因相关日志变少就把事件追踪称作已实现。

## ETW 提供者注册：句柄、traits 与 ANSI 入口（`26c3a97`、`2b06b12`）

`tools/etwprobe` 中的三个探针分别测现代提供者、traits 和经典提供者的 ANSI/Unicode 入口；
同一 PE 在 Windows 11 和 Wine 上逐行比对。`EventRegister` 不再给所有提供者同一个假句柄，注销后也不再
把写入和设置 traits 当作成功；traits 的编号、长度检查与“只能成功设置一次”按原生测量处理。
`RegisterTraceGuidsW/A` 都分配可注销的提供者句柄，填充类别句柄；A 入口把可选 MOF 字符串按 ANSI 码页
转成 Unicode，再走 W 的注册路径。注册失败时的返回值、输出是否改动和 `LastError` 都由原生探针覆盖。

原生测试曾把 `RegisterTraceGuids` 的注册句柄交给 `GetTraceEnableFlags`，并从此前调用留下的错误码推断规则；
它实际要的是控制回调得到的 logger 句柄。那条断言已删，修正后的新增测试段在 Windows 和 Wine 上都没有
失败。Windows 的 eventlog 整套测试仍有原有测试段的环境相关失败，不能称整套原生通过。探针还显示
类别计数大于零而类别数组或类别 GUID 为空会使 Windows 测试进程崩溃，不能把它们写成应返回参数错误的断言。

**边界：**这里仅实现“没有会话监听”时的提供者侧状态。`StartTrace`、`EnableTraceEx2`、
`GetTraceLoggerHandle` 和实际投递尚未实现；MOF 路径被接收但不会加载文件。重装三份 ntdll
（两种 PE 架构及 unix 侧）并在 `:2` 重启 Word 后，功能区与文档正文仍正常显示；这只能排除
显眼的启动回归，不能证明真实 ETW 会话可用。`scripts/winrun.sh` 已改为传回远端进程的退出码：
以前即使 Windows 测试报告失败，脚本也固定退出 0；往后仍要读取测试输出里的失败数，不能只看包装器状态。

## winref 的最新 Windows 合约：版本号必须和能力一起推进

`tools/contractprobe` 在 winref 的 Windows build 29671 上测得 `UniversalApiContract` 上限为 **20**，
`FoundationContract` 为 4，`WwanContract` 为 3。Wine 当前只报告 Universal 10：这不是
“已等同 Windows 11”的证明。旧探针传空合约名时在原生系统中途异常退出；新版不再把危险的
无效输入混入正常覆盖，完整运行并保存了原生输出。

将 Wine 的 Universal 上限**临时**改为 20 后，Word 会在版本 15 返回真时停下版本探测，
且向 compositor 查询合约 13 才有的 `ICompositorWithBlurredWallpaperBackdropBrush`；Wine
目前返回 `E_NOINTERFACE`。窗口、功能区和文档页仍能绘出，说明 Word 对这次查询有回退，
**不说明合约 20 的所有接口已实现**。实验后已把源码和安装版恢复到 10 并复测 Word，
没有把仅改数字的诊断当成兼容性修复。

### Click-to-Run 的 COM 类在包里，却没投射到 Wine 注册表

`~/.wine-c2r-test` 中的 `Word.Application`、`Excel.Application` 等 ProgID 原先查不到，
虽然四个 EXE 都在，安装器产生的 `root/vreg/*.vreg.dat` 也保存了它们的真实 CLSID 与
`LocalServer32`。winref Windows 的同名 ProgID 都已注册；差的是 App-V 虚拟注册表叠加/发布，
不是 Office 二进制缺失。用本机 Office 蜂巢（不是从另一台机器复制身份或授权）投射
Word 的实际注册后，`CreateObject("Word.Application")` 能新建文档、编辑正文、保存
并关闭；生成的 DOCX 是完整 ZIP/OOXML，正文与输入相符。

`export-office-registry.py --automation-only` 从各应用自己的 App-V hive **读取** CLSID，
只导出 Word、Excel、PowerPoint、Outlook 的 Application ProgID 与本地服务器注册，
不凭空编 GUID；`apply-office-registry.sh` 现在识别这种 `31-automation-*.reg` 导出，
且空目录会失败而非假报成功。三款其余应用的 ProgID 已按本机安装元数据投射并与
winref 上的 GUID 对照；**仅有注册键不能证明应用能用**。后来 Word、Excel、PowerPoint
各自在真实应用的 COM 路径下新建、修改并保存了本地 DOCX/XLSX/PPTX，均解包核对内容；
Excel 另外经 GUI 点击空白工作簿，功能区与网格实际绘出。Outlook 尚未启动或访问邮箱，
没有发送邮件或打印任务。`office-debug.sh run` 已修复隐藏 Wine 非零退出码的问题，
避免把这一类失败误写成通过。

### Excel 新建工作簿：不是 COM 注册失败，是旧交互源占住了 Visual（`a0fe160`、`f7081fd`、`fb62cc5`）

补上包里已有的 ProgID 后，Excel COM 激活和 `Visible` 成功，`Workbooks.Add` 却永不返回；
直接从 Excel 开始页点空白工作簿，标题变为「工作簿1」，界面仍停在开始页。
起初 `tracker_statics2_SetBindingMode` 的八次 `S_OK` 只是桩，确实不绑定：winref 上
`tools/trackerbindingprobe` 测得同一 Tracker 可按 X/Y/Scale 分别绑定**不同伙伴**，
同一对重设模式会取代旧值，跨 Compositor 返回 `E_ACCESSDENIED`。Wine 现在按轴建立关系，
传递选定轴的即时/惯性/自定义动画值；本地和 winref 的同步状态测试通过。winref 桌面锁屏，
没有帧推进，**动画的时间行为尚不能直接与原生逐帧比对**。但修完这项，Excel 白屏仍在——
不能因为找到一个真缺口就把它冒充白屏根因。

真正的断点来自 Excel `+seh` 的一次 `c0000005`：对地址 0 写入，Office 标签
`0x1e440099`、失败 HRESULT `E_INVALIDARG`。在 `Mso40UIwin32client.dll` 里按小端字节找
标签，追到 `VisualInteractionSource` 的静态 `Create`（IID
`{369965e1-8645-4f75-ba00-6479cd10c8e6}`）。`+dcomp` 把同一个 Visual 的调用串起来：

```
Create(visual) → InteractionSources.Add(old) → InteractionSources.RemoveAll()
               → Create(同一 visual) → E_INVALIDARG → Office 主动 fail-fast
```

Wine 的旧源还有 Office 的外部引用，所以 `source_destroy` 没运行，`visual->interaction_source`
仍指着它。`tools/interactionsourceprobe` 在 winref 测到恰好的区别：**RemoveAll 后即使旧对象仍活着，
同一 Visual 可再 Create；单个 Remove 后仍不可再 Create**。只在 `sources_RemoveAll` 对本实现
的源解除 Visual 的旧预留，照旧释放集合引用，别把 `Remove` 改成同样行为；新源设置后，
旧源销毁时已有的相等守卫也不会误清它。同一探针 Wine 输出逐行匹配原生，新增 dcomp 测试
先红后绿，Wine 和 winref 的完整 `composition` 测试均无失败（原生锁屏帧项跳过）。

部署新版 dcomp 并重启专用前缀后，Word 的正文仍正常绘出；Excel 自动化探针终于执行到
工作簿新建、A1 写入、XLSX 本地保存与关闭，XML 中 A1 确为写入文字。Excel GUI 的开始页
点空白工作簿也显示出真实功能区与表格网格。PowerPoint 的标题幻灯片与 PPTX 保存亦已
核验；这些是具体的端到端路径，不应外推成 Office 全功能或 Windows 合约 20 完成。

`winrun.sh --desktop` 此轮也修掉了长测试尚未结束就读取空输出、误报 0 的竞态：
为每次运行使用独立任务/输出名，PowerShell 写完成标记与进程退出码后才读回；用实际
返回 0 与返回 7 的小程序分别验了成功/失败路径。测试汇总中的失败数仍要另查。

### D3D11 的 `DO_NOT_WAIT` 不能静默变成阻塞 Map（`ba6a991`）

Word 启动时几十次带 `D3D11_MAP_FLAG_DO_NOT_WAIT` 的 `Map` 原来一律被忽略，
既可能无声地等完 GPU 操作，也无法按 Windows 报 `DXGI_ERROR_WAS_STILL_DRAWING`。
`tools/d3dmapprobe` 同一 PE 在 winref Windows build 29671 的 WARP 与 Wine 软件后端实测：
动态缓冲的 `WRITE_DISCARD`/`WRITE_NO_OVERWRITE` 搭配该标志是 `E_INVALIDARG`，
未知 bit 2 也是参数错误，输出指针归零；空闲 staging 纹理的 READ/WRITE/READ_WRITE
可以直接映射；紧跟 GPU Copy 的 READ/DO_NOT_WAIT 在 winref 本轮返回“仍在绘制”。

`dlls/d3d11` 现在检查标志与写入方式，把 DO_NOT_WAIT 传给 `wined3d`；命令流在该资源
仍有排队操作时直接答忙而不等待，忙时不触碰映射状态。确定性探针行与 Windows 一致；
紧跟 Copy 的 busy/ready 次数取决于两端工作线程与 GPU 时序，不拿它作逐行相等的断言。
受测 Wine 软件后端的成功 Map 在高精度测量中未出现长时间阻塞，但**命令流尾部前进不等于
硬件 GPU fence 已完成**：尚未对每个 GL/Vulkan 后端证实严格非阻塞，不称完整实现。

专门的 `d3d11 --map-do-not-wait` Wine/winref 测试各六项、0 失败；一次普通 D3D11 全套
单线程测试到 480 秒仍未结束，不能称全套通过。重装 d3d11、wined3d 两架构 DLL 后，
Word 功能区及正文仍绘出，Excel 的 COM 新建、编辑、保存 XLSX 仍通过；原来那条
`Ignoring map_flags` FIXME 不再出现。此项是局部合约推进，不等于 Windows 20 全面支持。

### 后端 BO GPU fence 的非阻塞 Map（`c95e723`，继 `ba6a991` 之后）

命令流已消费并不代表 GPU 已完成。沿 `wined3d` 的实际 BO 映射链检查后，GL 原先会在
`wined3d_bo_gl_map()` 内等待 command fence，Vulkan 会在
`adapter_vk_map_bo_address()` 内等待 command buffer；两者都没有处理
`WINED3D_MAP_DONOTWAIT`。现在 GL 在 fence 尚未完成时提交/flush 并零超时查询，
Vulkan 在必要的 HOST_READ barrier 提交后调用 `vkGetFenceStatus`；若仍忙则向 D3D11
返回 `DXGI_ERROR_WAS_STILL_DRAWING`，不把失败映射记为已映射、也不提前将纹理位置
标记为脏。Vulkan 的每个 BO 记录 HOST_READ barrier 所属 command-buffer id，以免每次
重试都再提交一个新的 barrier，形成无法完成的重试循环；非 coherent 内存的 invalidate
移到 GPU 等待及映射之后。

两架构 `wined3d` 已编译，本机数轮 `d3d11 --map-do-not-wait` 在 GL 为 21 项、
Vulkan 为 13～21 项，均 0 失败、0 跳过（次数随异步忙碌/重试变化）；Vulkan `+d3d` trace
确实记录了一次 `VK_NOT_READY` 的 GPU fence 忙碌返回。相同 PE 的旧探针在两后端
均执行完毕。替换 `/opt/wine-altars` 两架构 DLL（先关 Office，再用新 inode 替换）
之后，Word 新建空文档并切换插入功能区正常绘出；Word DOCX、Excel XLSX、PowerPoint
PPTX 的本地 COM 创建/编辑/保存和 OOXML 文本校验均通过。PowerPoint 仍只记录到
`saved`，没有 `closed` 进度标记，不能称其退出回调已验证。没有打印或读取 Outlook 邮箱。

本轮 winref 名称解析失败，没有跑新增的原生测试；之前六项 winref 基线不是这组扩展测试的
新结果。GL GPU-fence 忙碌分支未在本机复现；非 BO 映射之前的纹理位置加载、buffer
位置转移、共享 GL 分配块、无 fence 的驱动仍可能阻塞。不要把这次
局部推进说成所有 GPU 后端、Office 流程或 Windows 合约 20 已全部完成。

### PowerPoint 重启后的 COM 类对象失效（Wine `044f721`）

最终版 D3D DLL 部署后再跑 Office COM：Word DOCX、Excel XLSX 通过，PowerPoint
`CreateObject("PowerPoint.Application")` 却返回 VB 429。注册表中真实的 ProgID、CLSID、
`LocalServer32` 仍在；`+ole,+seh` 显示 `rpc_get_local_class_object()` 从 RPCSS 取得一个
旧的本地服务器对象，`CoUnmarshalInterface` 因服务器已退出返回 `0x800706ba`。
此前 PowerPoint 首次保存 PPTX 后没写 `closed` 标记，单次成功不能证明服务器注销类对象。

`tools/comclassprobe` 用不依赖 Office 的独立 PE 重现：子进程注册可多次使用的类对象，
父进程跨进程取得一次后直接结束子进程，再查同一 CLSID。旧 RPCSS 返回
`0x800706ba`（退出码 6）；新 RPCSS 连续两轮返回 `0x80040154`（退出码 0），说明
失效类不再当作活服务器提供。`programs/rpcss/rpcss_main.c` 注册类时从本机 RPC
绑定取得服务器 PID，持有 `SYNCHRONIZE` 进程句柄；查类时以零超时检查其退出状态，
删除失效项后继续找其它同 CLSID 注册。未取得进程句柄时保留既有行为并警告，
没有把异常伪装成成功。

仅 64 位 `rpcss.exe` 是该 WOW64 构建的实际服务目标，已编译、备份并以新 inode
替换安装版，重启 Office 测试前缀使新服务生效。同一 RPCSS 生存期里，探针连续两次
通过；PowerPoint 连续两次 COM 激活、创建并保存本地 PPTX。新服务下 Word DOCX、
Excel XLSX 也重新创建并保存，四个文件的预期文本均经 ZIP/OOXML 验证。
两次 PowerPoint 都只留下 `saved` 而无 `closed` 标记；
它的退出/回调语义仍未证明。winref 暂不可解析，本探针尚未在原生 Windows 测精确错误码；
此修复不代表全部 Office 或 Windows 最新合约已完成，也没有打印或访问 Outlook 邮箱。

### PowerPoint `Presentation.Close` 暴露下一层 WinUI Composition 缺口（`6f44b44`）

在 `powerpoint-save.vbs` 的保存、关闭、退出各调用前后加入进度标记，并对关闭错误单独
记录。重启 RPCSS 后 PPTX 实际保存且 OOXML 内含目标文本，但 `Presentation.Close`
可能卡住，另一次返回 VB **462**（远程服务器不可用）；此前仅凭 `saved` 误以为完整
自动化成功。`+ole,+seh` 证实 462 来自 PowerPoint 进程崩溃后 COM RPC 返回
`0x800706ba`，**不是**保存格式或注册表项的问题。

同一线程的崩溃前紧邻调用是
`RoGetActivationFactory("Windows.UI.Composition.CompositionEffectSourceParameter",
IID_ICompositionEffectSourceParameterFactory)` 返回类未注册，随后 Office 以内部标签
`0x0269c61b` 故意触发空指针异常。该 WinRT 类的接口本来就在
`windows.ui.composition.idl`，但 dcomp 没有注册或实现。Wine `6f44b44` 实现真实
的名字持有/复制、`ICompositionEffectSourceParameter::get_Name`、
`IGraphicsEffectSource` QueryInterface、对象引用计数及 WinRT 激活工厂，
并将它注册到 `classes.idl`，不是只改合约版本数字或返回假对象。
独立 dcomp 测试修复前 **520 项/1 失败**；更新隔离前缀 WinRT 类注册后
**530 项/0 失败、0 跳过**。两架构 dcomp 编译、备份并以新 inode 部署，Office 前缀执行
`wineboot -u` 后的注册项指向真正的 `dcomp.dll`。Word/Excel 的 COM 本地保存与 OOXML
内容复测通过，PowerPoint 的 PPTX 仍能保存。

**PowerPoint 关闭仍未修复**：新日志证实 Office 真正创建 `source1`/`source2` 并
查询它们的 `IGraphicsEffectSource`，接着调用尚为 `E_NOTIMPL` 的
`ICompositor::CreateEffectFactoryWithProperties`，马上发生另一处空地址异常；
测试脚本先后记录过 `presentation close failed: 462` 与
`presentation close failed: -2147023170`（`0x800706be`），均以退出码 4 报告，
不再把「已保存」误报为整套自动化通过。只在临时诊断 DLL 的
`+dcomp` 中读取 Office 实际传入的 `IGraphicsEffect::Name`，得到 **`Crossfade`**；
其 `GetRuntimeClassName`、`GetIids` 都返回 `E_NOTIMPL`，不能凭类名推断它的全部
图结构。测量后已撤销诊断代码，并重新以新 inode 安装与提交一致的生产版 dcomp。
随后只在另一版临时诊断 DLL 中枚举本次实际传入的 `animatableProperties`：
**`Crossfade.Source1Amount`** 和 **`Crossfade.Source2Amount`**。这不是 SDK
`CLSID_D2D1CrossFade` 的单个 weight 属性，不能仅凭同名 Crossfade 把它绑定到
那个效果。随后根据微软公开的 [Graphics Effects interop 头文件](https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/winrt/windows.graphics.effects.interop.h)
只在临时诊断 DLL 中查询真实的 `IGraphicsEffectD2D1Interop`：Office 对象确实支持
IID `{2fc57384-a068-44d7-a331-30982fcf7177}`，其 `GetEffectId` 给的是
**`CLSID_D2D1ArithmeticComposite`** `{fc151437-049a-4784-a24a-f1c4daf20987}`；
`GetSourceCount=2`，两个来源恰为已创建的 `source1`、`source2`。
`GetProperty(0)` 是四个 float 的系数 `[0, 1, 0, 0]`，`GetProperty(1)` 是 false 的
ClampOutput；两个动画属性都映射到系数属性索引 0，分别映射 `VECTORY` 与 `VECTORZ`，
即独立调节来源 1、2 的贡献。`GetPropertyCount=4`，但本次索引 2/3 的查询返回
`E_INVALIDARG`，不得据数量编造属性值。这些是**本轮 Wine 内运行的 Office 对象调用**，
不是 winref 原生探针。

现有 Wine `dlls/d2d1` 虽已注册 ArithmeticComposite effect，却在
`ID2D1DeviceContext::DrawImage` 收到 effect 图像时仅处理 bitmap/command list，
其余报 `Unhandled image`。另一项确实相关、可独立验证的缺口是
`D2D1_PRIMITIVE_BLEND_ADD` 只保存状态，实际 bitmap/几何绘图仍绑定 source-over
GPU blend state。Wine `2a20837` 为 ADD 建立真正的 D3D11 加法 blend state，按当前
primitive blend 选择，并在重置 target/销毁 context 时释放。半透明红、蓝图元
重叠的像素测试原来 **29 项/1 失败**（加法结果错误），修后本机 GL、Vulkan
各 **29 项/0 失败、0 跳过**；两架构 d2d1 已备份、用新 inode 部署。
紧接着测到 `ID2D1DeviceContext::DrawImage(bitmap, ..., D2D1_COMPOSITE_MODE_PLUS)`
也把模式忽略，仍画成 source-over：扩展像素测试修前 **37 项/1 失败**。
Wine `e903728` 只对真正的 bitmap 图像按 PLUS/SOURCE_OVER 临时选择对应 GPU
blend state，绘制后恢复外层 primitive blend；GL、Vulkan 最新专项测试各
**45 项/0 失败、0 跳过**。这不是对 effect image 的假支持：后者的 `DrawImage`
仍然打印 `Unhandled image`。两架构新 DLL 用新 inode 部署后，Word、Excel
COM 本地保存及 OOXML 内容验证通过，PowerPoint PPTX 也实际保存，但它的
`Presentation.Close` 仍报 RPC 错误、测试退出码为 4。上述修复只是构建真实
两源合成所需的前提，不等于 effect image 已能画。
先前窗口截图中幻灯片仍是空白，尽管保存的 PPTX 里有预期文字；文件内容通过不能充当
真实屏幕渲染通过。所以完整修复至少还涉及 WinUI effect factory/brush 的来源绑定、动画属性与最终
D2D/合成渲染，**不能**仅给 `CreateEffectFactoryWithProperties` 返回不会绘制的假成功。
本轮临时诊断代码已撤销，安装版恢复为回归测试通过的生产 DLL；只有一次加大量
`+dcomp` 输出的诊断在幻灯片创建后超时，不据此断言原生保存或关闭行为变化。
本轮 winref 仍不可解析，没有将新增类的边界情况冒称原生测量，也未打印或访问 Outlook 邮箱。

### Direct2D 效果图与 WinUI 效果画刷：PowerPoint `Presentation.Close` 通过（Wine `4b42eb3`、`70ea39d`、`c8d9f9a`、`9774404`、`2835e35`）

**先分清谁在画。** 这两类效果都不是 Office 自己画的：

- **PowerPoint 直接用 D2D 的部分**（`CreateEffect` Shadow、2DAffineTransform 后调用 `DrawImage`）：由
  Direct2D 在 GPU 上算。微软 [Effects overview](https://learn.microsoft.com/en-us/windows/win32/direct2d/effects-overview)
  写明：每个效果建立内部 transform graph，每个 transform 容纳逐像素执行的着色器；
  “内置效果和自定义效果都这样工作”，自定义效果框架“就是用来创建 D2D 内置效果的同一框架”。
- **关闭演示文稿时的 Crossfade**：PowerPoint 不调用 D2D，只把 `IGraphicsEffect` 描述交给
  `Compositor.CreateEffectFactory` 编译，再用 `CompositionEffectBrush` 绑定来源、驱动动画。
  [Visual layer](https://learn.microsoft.com/en-us/windows/uwp/composition/visual-layer) 文档把
  app 里的 Visual 称为“合成器中视觉状态的代理”，效果与动画独立于 UI 线程渲染。Windows 上
  这是进程外的系统合成器（DWM），其内部实现未公开，本文不作猜测。
- **Wine 的对应做法**：Wine 没有 DWM，dcomp 本来就在进程内用 D2D 画视觉树，因此让 dcomp
  把效果描述翻译成 D2D 效果图，是复用同一套实现，不代表 Windows 的合成效果经过 app 进程的 d2d1。

**d2d1（`4b42eb3`）：按原生结构实现效果图渲染。** 起初写的“单个超级着色器按 CLSID 分支”
方案已放弃：它画得出几个内置效果，却让应用自注册的自定义效果永远画不出来。现在的结构是：

- **通用渲染器**：`effect_render.c` 只通过 transform 的公开接口，从输出往回走：
  `MapOutputRectToInputRects` 算出每个输入要提供的区域；bitmap、command list 或嵌套 effect
  先画进中间纹理；再运行每个 draw transform 的像素着色器。
  - 顶点着色器按自定义效果的约定提供 `SCENE_POSITION`，以及 `TEXCOORDn`（uv 与一个效果像素的 uv 尺寸）。
  - offset、bounds adjustment、border、blend transform 都能画。
  - 只渲染裁剪区或目标可见的部分，最后按 DrawImage 的合成模式画到目标上。
- **draw info 与准备顺序**：`ID2D1DrawInfo` 真正保存输入描述、输出缓冲和常量。
  `PrepareForRender` 在首次绘制前、以及属性、图或 DPI 变化后调用，`SetDrawInfo` 先于它执行。
- **内置效果**：Flood、ArithmeticComposite、Composite、Crop、2DAffineTransform、GaussianBlur、
  Shadow、Grayscale 实现为真正的 `ID2D1EffectImpl` 加 transform 与各自的 HLSL；
  `GetImageLocalBounds` 支持效果。

**d2d1 的测量与坑：**

- **Windows 实测期望转为通过**：Wine 测试中由 Windows 实测写成的一批期望原先是 todo_wine，现在全部通过。
  包括 2D 仿射的 6 组边界与像素图形、Crop 边界、Flood 边界与整面 hash、Grayscale、
  自定义像素着色器（`0x661a334c`），以及 SetOutputBuffer 校验。
  - 2D 仿射的输出边界规则：输入外扩半像素后做变换，取变换结果的包围盒，输出是像素中心落在盒内的所有像素。
- **新增公式测试**：`test_effect_rendering` 只取文档公式能推出的值（算术合成、合成模式、嵌套效果、
  `image_rect` 偏移、阴影上色、模糊、command list 输入），**未在 Windows 上运行**。
- **测试结果**：d2d1 全量在 GL、Vulkan 下各 **17641 项/0 失败**。
- **坑：SRV/RTV 冲突**。刚被 D2D 画入的纹理（例如 command list 回放的目标）仍作为 RTV 绑定在
  D2D 自己的状态里。先绑 SRV 会被 D3D11/wined3d 强制置空，读到全透明。每个 pass 必须先绑输出再绑输入。
- **坑：无限边界的表示**。无限边界用 `LONG_MIN/LONG_MAX` 并做饱和运算；Windows 上 Flood 报 `±(float)INT_MAX`。
- **坑：IGNORE 透明度的输入**。IGNORE alpha 的 bitmap 作为效果输入时，保留其中存储的 alpha（Windows Grayscale 测试可证）。
- **附带修复（`70ea39d`）**：`SetInput(NULL)` 原来会解引用空指针。Win2D 用它清除输入，Windows 也接受。

**dcomp（`c8d9f9a`、`9774404`、`2835e35`）：**

- **接口声明**：新增 `windows.graphics.effects.interop.idl`，按 SDK 声明 `IGraphicsEffectD2D1Interop`。
- **效果工厂**：在创建时整体取下效果图，包括各节点的 CLSID、属性值和来源（参数名或嵌套效果）。
  - 可动画名经 `GetNamedPropertyMapping` 解析；查不到的效果名或属性名一律拒绝。
  - `LoadStatus=Success`。
- **效果画刷**：按参数名保存来源画刷；可动画属性是画刷自己的值，也能来自它的属性集并参与动画。
  - 带点号的名字（`Effect.Property`）若整体就是对象的属性，便不再拆成“属性.成员”。
  - 开始、停止动画与表达式求值三处都遵循这一规则。
- **绘制**：在目标的 context 上建 D2D effect，并套用当前属性值。各来源画刷先在独立 context 上
  画成与 visual 同尺寸的 bitmap，以免带入目标的裁剪。不透明度用两个输入都接输出的 ArithmeticComposite 实现。

**PowerPoint 的实际用法（本轮 `+dcomp` 实测）：**

- 两个来源都是 surface brush。
- 另建表达式 `1-CompObj.Crossfade.Source2Amount`，CompObj 是画刷本身，并启动在
  `Crossfade.Source1Amount` 上。
- `2835e35` 之前，这个表达式因为 `Crossfade.Source2Amount` 被拆成两段而无法求值，
  `StartAnimation` 静默返回 `E_INVALIDARG`（PowerPoint 不报错）；修后两个画刷上都正常 started。
- composition 测试 **574 项/0 失败**。其中这条表达式测试做过红绿验证：撤掉修复即以 `0x80070057` 失败。

**结果：**

- 两架构 d2d1、dcomp 已用新 inode 部署。
- PowerPoint 探针依次到达 `presentation closed`、`application quit`、`closed`，
  **退出码 0**（此前固定为 4）。PPTX 幻灯片文字正确。
- Word、Excel COM 本地保存与 OOXML 内容回归通过。

**遗留问题：**

- **关闭阶段的 GL 错误**：表达式动画持续运行后，关闭阶段合成器会再向正在销毁的窗口呈现一帧，
  出现一次 `swapchain_blit_gdi Failed to blit`，之后每释放一个 CompositionDrawingSurface 就报一次
  `wglSetPixelFormatWINE ... device context 0`（185 条，与 184/185 个表面、画刷释放数一致）。
  - 进程仍正常退出。
  - 不直接改成“隐藏窗口不呈现”，原因是应用可能在显示窗口前就提交了唯一一帧，那样会变成空白；
    需要先做窗口生命周期的正确处理。
- **性能与质量**：效果图每帧重算，没有缓存；中间纹理按 1:1 DIP 分辨率，缩放时会糊。
- **尚未实现的内置效果**：ColorMatrix、Blend、Saturation 等仍无渲染。2D 仿射的 hard 边框
  按 soft 处理，三次插值按线性近似。PushLayer 当时仍是存根（后由 `134a166` 实现，见下一节）。
- **屏幕渲染（本轮复查）**：新探针 `powerpoint-show.vbs` 打开已有 PPTX、显示后再关闭（退出码 0）。
  - 按窗口 ID 截取整个 PowerPoint 顶层窗口（`:2` 是 1440×2992 竖屏、无根 Xwayland，根窗口不能读取），
    编辑区中下部的幻灯片**完整显示**：标题、副标题占位与备注栏均可见，左侧缩略图也带标题文字。
  - 之前记录的“幻灯片空白”，至少这次是只截了上部造成的。
  - 想用旧 DLL 对照缩略图，但两个旧 DLL 都换回时，PowerPoint 打开后约 12 秒内即崩溃
    （E_NOTIMPL 链条）；只换回旧 d2d1 时，新 dcomp 的 `SetInput(NULL)` 又让它在关闭时以 462 失败。
    因此缩略图的变化**不能**归因于某一个 DLL。这也说明两个 DLL 必须一起部署。
- **原生对照**：winref 本轮仍不可达，以上结论均未与原生机器做对照测量。

### d2d1 的弧、图层与线型：占位框的虚线画出来了（Wine `78185ed`、`134a166`、`36fd8d0`）

效果图之后，d2d1 里还有三处 Office 会走到的基础绘图是桩：

- **弧（`78185ed`）**：`AddArc` 原来把弧当成到终点的直线，饼图、圆角轮廓和用弧拼的圆都成了多边形，
  半圆（弧加弦）甚至是空的。
  - 现在按 SVG 用端点定义弧的方法（端点、半径、旋转、扫向、大小弧）求出椭圆，
    拆成每段不超过 90° 的三次 Bézier（控制点系数 $k = \frac{4}{3}\tan\frac{\delta}{4}$），
    最后一段精确落在给定终点。
  - D2D 的 `CLOCKWISE` 在 y 轴向下的坐标系里对应 SVG 的 sweep=1；没有跨度的弧就是直线。
- **图层（`134a166`）**：`PushLayer`/`PopLayer` 原是桩；更糟的是 `ID2D1DeviceContext::PushLayer`
  不计入 push 栈，于是随后的 `PopLayer` 成了不配对的 pop，这个错误让到 `EndDraw` 为止的所有绘制都作废。
  命令列表回放图层正是走这条路。
  - 现在图层是与目标同尺寸、同格式的离屏位图：清空，或按
    `INITIALIZE_FROM_BACKGROUND` 复制目标现有内容；内容边界裁剪其中的绘制。
  - `PopLayer` 通过几何遮罩（遮罩变换乘以 push 时的世界变换）或内容边界合成回去，
    带图层不透明度与不透明度画刷；画刷变换取世界变换的逆，使图层像素与设备像素一一对应。
  - 留在栈上的图层在 `EndDraw`、换目标和 context 销毁时释放。
- **线型（`36fd8d0`）**：`DrawGeometry` 原来忽略线型，所有描边都是实线、平头。
  - 现在先按绘制精度（容差除以世界变换与 DPI 的缩放）把几何展平，再按虚线切段：
    虚线长度和 `dashOffset` 以线宽为单位，奇数长度的虚线数组走两遍，使线段与间隔轮流出现。
  - 每段以平头描边；线帽另行填充：方帽把线段延长半个线宽，圆帽是半径为半线宽的圆，三角帽是三角形。
  - `FIXED` 线宽不随世界变换缩放，`HAIRLINE` 恒为一个像素，缩放取 $\sqrt{|\det M|}$。

**坑：同一图形画两遍，Wine 的填充就坏了。** 点线（DOT）的每一段长度为零、两端都是圆帽，
最初在同一点加了两个一模一样的圆。Wine 的填充走约束 Delaunay 三角剖分，遇到重合的图形会把点与点之间也填满。
现在长度为零、两端都是圆帽的段只加一个圆。这不是线型本身的规则，而是 Wine 填充实现的限制：
以后往填充路径里加图形时，都要避开完全重合的图形。

**测试：** 弧、图层、线型各有新测试（弧：半圆与四分之三圆的边界和像素；图层：不透明度、嵌套、
内容边界、几何遮罩及其变换、`ID2D1DeviceContext` 的图层、留在栈上的图层；线型：各种虚线、
线帽、偏移、`FIXED` 与 `HAIRLINE`）。期望值都按文档推出，**均未在 Windows 上运行**。
线型测试先红后绿（实现前 34 项失败）。d2d1 全量在 GL、Vulkan 下各 **18147 项/0 失败**。

**Office 上的结果：**

- 两架构 d2d1 已按新 inode 部署。Word、Excel、PowerPoint 三个保存探针全部通过，文件均为合法 OOXML。
- 同一份 PPTX 由 `powerpoint-show.vbs` 显示并按窗口截图：
  线型实现之前，副标题占位框（“单击此处添加副标题”）是**实线**；之后是**虚线**，与 Windows 一致。
- 顺带修正了 Word 探针：它原来不调用 `Quit`，自动化启动的 `WINWORD.EXE /Automation -Embedding`
  在脚本结束后一直驻留。现在只退出自己启动的 Word（已在运行的 Word 不动），退出后进程随即消失。
  驻留本身在 Windows 上是否会发生，本轮没有在原生机器上测量，不据此判断为 Wine 缺陷。
- 新增 `scripts/office-regress.sh` 依次运行三个探针，并检查各自写出的文件和到达的步骤。
  注意等待条件：`OfficeClickToRun.exe /service` 常驻，`wineserver -w` 永远等不到，
  所以脚本等待的是具体应用进程退出。

**遗留：**

- 线的连接方式（join）除斜接外仍按斜接画；圆帽、三角帽叠在描边之上填充，半透明画刷会在帽处叠两次。
- 图层的 `IGNORE_ALPHA` 未实现；不透明度画刷与遮罩变换同时使用时是近似。
- 曾有一次 GL 下的全量测试在无汇总行的情况下提前结束（退出码 5，疑为 0xC0000005 截断）；
  开启异常日志后五次都未复现，原因未知。

### 线条连接、GL 上下文的悬垂交换链与共享句柄死锁（Wine `89a0f91`、`8f9cc9f`、`ba6fad0`、`a3d7408`）

本轮先用 `err+all,fixme+all` 对 Word、Excel、PowerPoint 的保存探针做了一次普查，再逐条追查。

**d2d1 的线条连接（`89a0f91`）。** 普查里 PowerPoint 报 `Line join 0x2 is drawn as a miter`（0x2 即 ROUND）。

- Wine 的描边轮廓按几何对象只建一次，与线型无关；连接处是 4 个顶点的斜接楔形，顶点着色器按
  prev/next 方向外扩。连接方式是绘制时参数，所以放在着色器里裁：
  - 斜角与 MITER_OR_BEVEL：楔形尖点与对侧内点的偏移长度 $|q| = 1/\sin(\tfrac{1}{2}\theta)$
    （$\theta$ 为两段的夹角），超过斜接限度就收回到斜角边上，位置是 $q/|q|^2$。
  - 圆角：像素着色器丢弃离连接点超过 $w/2$ 的像素。外侧偏移线正好在两段的外角处与该圆相切，
    所以楔形与圆盘的交集就是圆角扇形。
- 顺带修了一个旧缺陷：路径正好直行穿过的顶点原来也生成“连接”，沿下一段多画 25 个单位；
  下一段更短时会越过终点。
- 测试：直角拐角（线宽 10）三处像素区分斜接、斜角与圆角，另测共线顶点不越过终点。
  红测 20 项失败；d2d1 全量在 GL、Vulkan 下各 **18627 项/0 失败**。期望按几何推出，未在 Windows 上运行。
- 仍未做：MITER 超过限度时 D2D 会截断尖角，这里仍画完整尖角；正好掉头的顶点在斜角模式下仍多画 25 个单位。

**GL 上下文的悬垂交换链（`ba6fad0`、`a3d7408`）。** PowerPoint 的普查日志里，有一次出现了 1926 条
`wined3d_context_gl_set_gl_context Fallback to backup window ... failed too`，此后 10 次里再没出现。
带 `warn+d3d` 重跑时抓到了线索：回退前上下文用的“DC”是 `0034006B00300064`，按 UTF-16 解码是文本 “d0k4”。

- 根因（Wine 11.0 上游代码，上游 master 相同）：GL 后端一个设备每个线程只有一个上下文，由各交换链共用。
  上下文在 Present 时跟着切到那个交换链，但交换链销毁时没有代码把它移开，于是
  `wined3d_context_gl_update_window` 每次激活都从已释放的内存读窗口和 DC。
- 修复：交换链销毁时，在命令流线程上把指向它的上下文移到设备的隐式交换链。
  另一处：上下文已为某个窗口退到备用 DC 后，不再每次激活都换回那个失效的 DC 重试。
- 效果：PowerPoint 运行中的回退从每次 173 次（修第一处后仍有一次 755 次，是那个失效窗口的循环）
  降到 1 次（一个窗口先于其交换链销毁的正常情形），不再出现垃圾 DC。
- 那次 1926 条“failed too”没有带追踪复现过，只能推断与此有关。以前记录的关闭阶段 185 条
  `wglSetPixelFormatWINE` 错误，今天修复前的几次运行里本来就没有出现，也不能归因于这次修复。
- 回归：d3d9（d3d9ex、device、visual）与 d3d11 在 GL 下，修复前后的失败集合完全相同。
  新增的 d3d11 测试（设备在随后退出的线程上创建）修复前后都通过，本身不能区分修复与否；
  修复效果靠 `+d3d` 追踪确认。

**共享句柄表的死锁（`8f9cc9f`）。** 为了查上面的回归，把 d3d11 全套在 GL 下跑到了底，结果它在
`test_multisample_resolve` 永远卡住。winedbg 的调用栈：

- 主线程在 `Map` 里持有 wined3d 互斥锁，在 `wined3d_resource_wait_idle` 里等命令流线程空闲；
- 命令流线程在 blit 里释放一张临时纹理，经 `resource_cleanup` 进入早先 altars 加的
  `wined3d_resource_release_shared_handle`，要拿同一把互斥锁。
- 共享句柄表改用独立的临界区后，d3d11 全套在 GL 下 15 秒内跑完（563041 项）。
  Office 的读回若恰好遇上这种 blit，也会以同样方式冻结，所以这一条同样关系到 Office。

**顺带确认、无需修的：**

- `marshal_object ... {a6ef9860-...}`（IDispatchEx）与 `{fc4801a3-...}`（IObjectWithSite）：
  代理/存根都在，失败的是 `CreateStub` 对对象本身的 QI——cscript 在探测 Office 对象是否支持
  IDispatchEx，对象不支持，返回失败是正确行为，只是 Wine 用 ERR 打印。
- `GetLongPathNameW` 对 `\\?\C:\windows` 等路径原样返回，这些路径本来就是长名，结果与 Windows 相同。
- `SecManagerImpl_ProcessUrlAction Unsupported arguments`：只是对 UI 相关标志打 FIXME，照常执行。

**普查中发现、尚未处理的：**

- 前缀没有 wine-mono/.NET：OfficePLUS 服务（.NET 程序集）启动失败；VSTO 加载项与 Excel 的
  Power Query 同样依赖 .NET。
- ETW：Office 启动 ETW 跟踪会话（`StartTraceW` 桩），C2R 用 `wevtutil im` 安装清单（桩）。
- Excel、PowerPoint 读写计划任务时 taskschd 有大量 FIXME。


### 共享句柄与 keyed mutex 按 Windows 语义（Wine `4a064df`、`2fd34b5`）

早先为 Office AirSpace 做的跨设备共享表面（在 keyed mutex 交接时经系统内存搬运内容）能用，但 API
语义与 Windows 不符：d3d11 的 `test_shared_resource` 有 336 条失败、`test_keyed_mutex` 有 13 条。

- **句柄（`4a064df`）**：每个共享资源现在是一个 D3DKMT 资源，用 Wine 11 的 win32u 已有支持
  （`D3DKMTCreateAllocation2` 的已有系统内存标准分配、`D3DKMTShareObjects`）。
  - 普通共享或 keyed mutex 共享：给全局句柄（KMT）。它不是进程句柄，`CloseHandle` 与
    `DuplicateHandle` 都会拒绝；`OpenSharedResource` 接受它。
  - NT 句柄共享：`GetSharedHandle` 返回 `E_INVALIDARG`；`CreateSharedHandle` 每次给一个
    `DxgkSharedResource` 类型的新 NT 句柄，要求命名时放在会话的 BaseNamedObjects 目录；
    `OpenSharedResource1` 与 `OpenSharedResourceByName` 接受它。
  - 不共享的资源：`GetSharedHandle` 返回 `S_OK` 与 NULL。
  - D3DKMT 资源的私有运行时数据记录进程号与序号，用来在本进程中找回资源；
    其他进程的资源不能打开（存储本来就不共享）。
  - 创建校验：两种共享方式同时要求、只有 NT 句柄标志、特性级别低于 10.0，都按 Windows 拒绝。
  - 仍有的偏差：打开方的纹理副本报告与原资源相同的标志，但 `GetSharedHandle` 不给句柄
    （Windows 给原资源的句柄）。dxgi 层对缓冲区等非纹理资源仍无条件给出 `IDXGIKeyedMutex`。
- **keyed mutex（`2fd34b5`）**：原来只记录“是否被持有”。现在记录持有者：
  - 重复获取、释放自己未持有的锁返回 `DXGI_ERROR_INVALID_CALL`；
  - 持有者被销毁后，其他参与者一律得到 `WAIT_ABANDONED`；
  - 只有带 `SHARED_KEYEDMUTEX` 标志的纹理提供 `IDXGIKeyedMutex`。
- **验证**：
  - d3d11 全套在 GL 下只剩 10 条原有失败（修改前 416 条），
    `test_shared_resource`、`test_keyed_mutex` 与新增的 `test_shared_resource_nt_handle` 全部通过。
    新测试未在 Windows 上运行。
  - Vulkan 下另有随机波动的 `test_texture` 与 21072 行失败，修改前的构建同样出现。
  - 可见的 Word 启动后功能区、文档恢复窗格完整绘制；带 `warn+d3d11` 运行时，
    没有任何参与者越权获取或释放，也没有内容等待。三应用保存探针通过。
- **普查中另见**：
  - Word 启动时，d3d11 对 `ID3D11Buffer` 被 QI 为 `ID3D11Texture2D` 约 140 万次
    （Office 判断资源类型，失败是正确结果）。
  - `scissor_rects_valid Ignoring inverted scissor rect` 数千次：Office 设了上下颠倒的裁剪矩形。
    这与 Windows 一致，早先已按实测处理（`9ba5d82`：整次调用作废，之前的裁剪矩形保留），只是 WARN 较多。

### PowerPoint 的形状与图片效果：两处崩溃、一处堆破坏与 ColorMatrix（Wine `7b34517`、`1868358`、`89b2615`、`bb104cd`、`86eb48e`）

新探针 `powerpoint-effects.vbs` 依次施加渐变、阴影、发光、柔化边缘、映像、三维棱台、图片重新着色、文字效果。
起初第二步（阴影）就让 PowerPoint 崩溃，其后各步都以 462 失败。逐层查下去是三个互不相干的缺陷：

- **wintypes：数组属性值的所有权（`7b34517`）。** `warn+heap` 报出重复释放，发生在 PowerPoint 经
  `CompositionEffectSourceParameter` 组装合成效果的线程上。
  - 根因：`IPropertyValue::GetXxxArray` 把值内部的存储直接交给调用者。按 WinRT ABI，返回的数组应是
    调用者用 `CoTaskMemFree` 释放的副本，于是调用者一释放，值析构时又释放一次。
  - `GetStringArray` 更糟，它把字符串就地复制进值自己的数组。dcomp 读取效果颜色（浮点数组）时正按 ABI 释放。
  - 修复：返回 `CoTaskMemAlloc` 副本，字符串数组在副本上复制。上游 master 同样有此缺陷。
  - 测试：每种数组取两次，应得到两个不同的数组（修复前 18 种都返回同一个），再加字符串数组的所有权测试。
- **d2d1：命令列表指回已释放的上下文（`1868358`）。** 堆修好后，阴影一步在 `ID2D1CommandList::Close`
  里读到 `0xFEEEFEEEFEEEFEEE`（已释放内存的填充值）。
  - 根因：早先 altars 让命令列表记住把它设为目标的上下文，以便 `Close` 时解除目标；上下文销毁时却没有清掉这个指针。
    PowerPoint 正是先释放上下文、再关闭命令列表。
  - 新测试在 `warn+heap` 下能复现同一崩溃地址（d2d1 `+0x1F507`）。
- **d2d1：`SetDrawInfo` 的时机（`89b2615`）。** 发光一步在 gfx.dll 里读空指针。
  - 用 `+d2d` 追踪与反汇编确认：Office 自注册的发光效果在 `Initialize` 里先 `SetSingleTransformNode(this+8)`，
    再加载着色器，并立即经 `this+0x10` 处保存的 draw info 调 `SetPixelShader`（vtable 第 10 项）。
  - 也就是说，Windows 在 draw transform 加入变换图时就同步调用了 `SetDrawInfo`；Wine 原来是在 `Initialize` 返回后才调。
  - 修复：节点加入即调用；失败时留到绘制时重试并报告（文档说 `SetDrawInfo` 的失败从 `EndDraw` 返回）。
  - 这一时序是从 gfx.dll 在 Windows 上能工作推断的，没有在 Windows 上直接测量。
- **d2d1：ColorMatrix（`86eb48e`，`bb104cd` 让 FIXME 打印 CLSID 以便定位）。** 图片的灰度、亮度对比度
  都经 ColorMatrix 实现，原来它只有属性、没有渲染，图片整个不显示。
  - 现在是像素着色器：`[r g b a 1]` 乘 5×4 矩阵；预乘模式（默认）作用于去预乘后的颜色，再重新预乘；
    直通模式直接作用于输入；按 ClampOutput 钳制。
  - transform 常量上限从 64 字节提到 128 字节。
  - 测试按文档推出，未在 Windows 上运行。

**结果：**

- 效果探针全部步骤通过并保存，截图中阴影、红色发光、柔化边缘、渐隐映像、棱台高光、灰度并提亮的图片、
  文字的发光/阴影/映像都正确绘制。
- 图片的模糊艺术效果看不出来，原因未查（可能是所用枚举值不是模糊，也可能 Office 在 CPU 端处理）。
- d2d1 全量在 GL、Vulkan 下各 **18749 项/0 失败**；wintypes 1725 项/0 失败；三应用保存探针通过。
- GL 下 d2d1 全量偶尔仍会以退出码 5 提前结束（这次 5 次中 1 次，无汇总行），与以前记录的现象相同，原因未明。

## d2d1 的几何算法：组合、轮廓、测长、加宽与回放，照 Windows 实测实现（wine-src `41c007d`）

Excel 图表探针暴露出 `d2d_rectangle_geometry_CombineWithGeometry ... combine_mode 0x3 ... stub!`。查下去，d2d1 的几何算法几乎全是桩：
各类几何的 `CombineWithGeometry`、`Outline`、`CompareWithGeometry`、`ComputeLength`、`ComputePointAtLength`、`Widen`、`GetWidenedBounds`，
路径的 `Stream`，group 的 `Simplify`/`Tessellate`/`ComputeArea`/包含测试，椭圆与圆角矩形的 `GetBounds` 和包含测试。

**先测原生。** `tools/d2dgeomprobe/d2dgeom.c`、`d2dstroke.c` 在 winref（build 29671）上跑，输出留存为 `*.win.txt`；
七位有效数字的数据再用 Python 模型拟合，规则都逐位复现后才写进 Wine：

- **展平**：每段三次 Bézier 递归二分，直到两个控制点与弦上 1/3、2/3 处的点在 L∞ 下都小于容差。
  用它复现了 `ComputeArea`（圆 309.3903/314.12088、椭圆 156.9767）与 `ComputeLength`（62.5969、50.53018）；
  上游两条 `ComputeArea` 的 `todo_wine` 因此通过（面积改在 double 下按填充区域求）。
- **组合/轮廓输出**：先 `SetFillMode(ALTERNATE)`；图形从最上再最左的顶点开始，按起点 (y, x) 降序；
  每点一次 `AddLines(1)`、显式回到起点、`EndFigure(CLOSED)`；外正内反；**曲线保留为 Bézier**。
- **比较**：相同为 IS_CONTAINED，边或角接触即为 OVERLAP。
- **测长的切线**：该段两端曲线导数的加权插值，权重是到达该端点那一段的参数长度，曲线自身端点为 1/18。
  六种段长与不同段长相接的数据验证到 1e-7。
- **加宽**：MITER 超上限在“上限 × 半线宽”处截平（上限按 ≥1），MITER_OR_BEVEL 才退斜角，折返时斜接向前伸出；
  椭圆、圆角矩形的 `Simplify` 在开图前设 `FORCE_ROUND_LINE_JOIN`，所以加宽处处圆角。
- **圆弧**二分到每段 ≤90°（270° 是 4 段）；**Stream** 按段原样回放（直线、三次曲线合并，二次曲线、弧逐个）。

**实现**：新文件 `dlls/d2d1/polygon.c`。两个展平后的几何叠放：边在相交、端点落在边上、共线重叠处切开，
相距几个 float ulp 的端点并为一个顶点，每段记住两侧各被哪个几何填充；组合就是留下结果只在一侧的段并沿边界走成图形，
同一曲线上的段按参数拼回子曲线。加宽把笔画摊成凸块（段矩形、连接楔、线帽，圆的用曲线画再展平）再求并。

**顺带修的缺陷**：圆角矩形 `Simplify` 的四角上下颠倒；圆弧分段与 Windows 不同；路径包围盒用二次近似代替三次曲线；
路径 `FillContainsPoint` 把曲线当成端点连线、且不计容差；不带样式的描边命中测试不含连接；恰在图形末端结束的虚线用了虚线帽；
容差为 0 时展平无限递归。还有一个自己引入又修掉的：`qsort` 比较函数用静态全局指针取键值，d2d1 测试多线程并发时偶发给出错误图形。

**结果**：

- d2d1 全量在 Wine GL、Vulkan 下 **20711 项/0 失败**；同一测试 PE 在 winref 桌面会话 20770 项，
  只有上游原有的顶点缓冲测试 2 项失败（`Map` 已映射缓冲区返回不同指针，与本次无关）。
- 两个探针与 Windows 输出除末位 float 差异外一致，剩余差异见 `tools/d2dgeomprobe/README.md`。
- 部署后三应用保存回归通过；Excel 图表探针全部步骤通过，截图中带阴影柱形图、折线图、三维饼图、数据条、色阶、图标集、迷你图正确；
  `+d2d` 下 Excel 调用了 5 次矩形的 `CombineWithGeometry`（EXCLUDE，其中 4 次容差为 0.5），不再是桩。

**与 Windows 的已知差异**：`Widen` 的图形结构不同（Windows 是 WINDING、open、自重叠，这里是闭合不重叠，填充相同）；
加宽曲线时用的是展平后的曲线，面积略有出入（椭圆 533.3 对 531.2）；hollow 图形不透传进组合结果。

**退出码 5 的根因（已修，在 dwrite）**：d2d1 全量偶尔以退出码 5 中途结束、无汇总行（GL、Vulkan 都见过，约四五次一次），
是 dwrite 的竞争。非交互的 winetest 调用 `SetErrorMode(SEM_NOGPFAULTERRORBOX)`，崩溃时既不启动调试器也不打印
“wine: Unhandled page fault”，进程直接以异常码的低字节退出（0xC0000005 → 5），所以 `+seh` 4 次、strace 20 次都一无所获
（它们还改变了时序）。只加 `WINETEST_INTERACTIVE=1`，前缀里设 `HKCU\Software\Wine\WineDbg\ShowCrashDialog=0`，
第 1 轮就拿到 winedbg 回溯：`dwritefactory_CreateTextFormat` → `factory_get_system_collection` →
`IDWriteFontCollection_QueryInterface` 读地址 0。

dwrite 工厂缓存系统与 EUDC 字体集合却不持有引用，集合在最后一个引用释放时自己从工厂摘下。旧代码先检查指针、再读一次去 QI，
中间没有锁：另一线程恰好放掉最后一个引用，就读到 NULL 或已释放的内存；两个线程同时创建时，输的一方先放掉自己的，
再对赢家的多放一次，引用比持有者少一个。d2d1 测试的多个线程同时从一个工厂创建文本格式，正好撞上。
现在在工厂锁下查看缓存，引用计数非零时才加引用交出，集合摘下自己也在同一把锁下；同时新建的一个让位给已缓存且仍被持有的那个。
修后 24 次（GL、Vulkan 各 12 次）全部完整结束；dwrite 自身测试的失败项前后相同。提交 `918d65f`；部署到 /opt 后三应用保存回归通过。

## d2d1 的渐变：扩展模式、gamma、颜色空间与预乘，照 Windows 实测实现（wine-src `cabc95c`）

d2d1 的渐变 stop 集合只存了 stop：所有渐变都按 CLAMP、在 sRGB 里插值，`GetColorInterpolationGamma`/`GetExtendMode` 是桩，
1.1 的 `ID2D1DeviceContext::CreateGradientStopCollection` 返回 `E_NOTIMPL`，对象也不是 `ID2D1GradientStopCollection1`。
更要紧的是渐变颜色从不预乘：渐变到透明色时，透明端照样按不透明的颜色叠上去。

**先测原生。** `tools/d2dgradprobe/d2dgrad.c`、`d2dgrad2.c`、`d2dgrad3.c` 在 winref（build 29671）上画进 WIC 位图，输出留存为 `*.win.txt`。
测出的规则（细节见该目录 README）：

- 位置先按扩展模式归入 [0,1]（截断、取小数、周期 2 的三角波），再在按位置稳定排序的 stop 间插值；落在 [0,1] 外的 stop 只通过它们在区间内造成的颜色起作用。
- 直通模式插值未预乘的颜色与 alpha，从插值空间换到输出空间，再预乘；预乘模式换空间时先去预乘。
  1.0 的集合等于 pre=sRGB（gamma 2.2）或 pre=scRGB（gamma 1.0）、post=sRGB、8 位、直通；gamma 1.0 时 stop 先按 sRGB 曲线换成线性，
  `GetGradientStops1` 交回换算后的值。UNORM 精度把 stop 截到 [0,1]，浮点精度保留超界值。
- 零长度、零半径、每像素重复一次以上的渐变显示两端各半或周期平均色；焦点在椭圆外时取射线与椭圆较远的交点，看不到的区域画末端颜色。
- 非法参数：0 个 stop、CUSTOM 空间、越界的 gamma/扩展/插值模式为 `E_INVALIDARG`，精度 UNKNOWN 为 `D2DERR_INVALID_CALL`。

**实现。** 集合对象实现 `ID2D1GradientStopCollection1`，记下 pre/post 空间、精度、扩展与插值模式；着色器缓冲区里放排好序、换到插值空间（按精度截断、按模式预乘）的 stop，末尾放整个周期的平均色。
像素着色器按上述规则处理扩展模式、空间转换与预乘；位置的屏幕导数表明一像素跨过半个周期以上时向平均色过渡。
另修两处：`isinf` 在所带 vkd3d 的 SM4 后端尚未实现（`E5017`），改以 $2^{24}$ 为界；1.1 创建函数的精度检查按实测返回 `D2DERR_INVALID_CALL`。

**PowerPoint 怎么用。** 新探针 `tools/officeautomationprobe/powerpoint-gradients.vbs` 在 `trace+d2d` 下显示：
PowerPoint 全用 1.0 的 `CreateGradientStopCollection`，形状渐变多是自己预算的 200～256 个 stop、gamma 1.0，另有 3、7 个 stop 的 gamma 2.2，
**扩展模式全是 MIRROR**，画刷全是线性渐变——“从中心”的矩形路径渐变由四块镜像的线性渐变拼成。以前 Wine 一律按 CLAMP 画。

**结果：**

- 三个探针与 Windows 逐列对照，除下面的差异外在 1 级以内。
- d2d1 全量在 Wine GL、Vulkan 下各 **21437 项/0 失败**；同一测试 PE 在 winref 桌面会话（GPU）上 21496 项，只有原有的顶点缓冲测试 2 项失败，新加的渐变测试全部通过。
- 部署后三应用保存回归通过；渐变探针的截图中线性、从中心、从角部、彩虹、渐隐到透明（下面的深色条逐渐透出）、半透明中间 stop、渐变文字都正确。

**与 Windows 的已知差异**：Windows 用由 stop 生成的纹理采样渐变，WRAP 接缝半个纹素内混入另一端颜色，压进一两个像素的 CLAMP 渐变被滤波平均，
Wine 逐像素精确求值，这些像素差 2～5 级；gamma 1.0 下接近 0 的线性值，Windows 的 CLAMP 结果比精确 sRGB 曲线低 2 级，原因未明。

**截图另见**：形状与渐变文字的边缘有锯齿——Wine 的 d2d1 没有实现逐图元抗锯齿，`SetAntialiasMode` 只记下模式。下一节补上。

## d2d1 的逐图元抗锯齿，与三次曲线、ClearView 两处相邻缺口（wine-src `53cf591`、`44fc0e8`、`69802d1`）

Wine 的 d2d1 从未实现抗锯齿：`SetAntialiasMode` 只记下模式，所有填充、描边、裁剪都按像素中心取舍。Office 全程用 `PER_PRIMITIVE`
（Excel 图表、PowerPoint 的跟踪里 `SetAntialiasMode` 数百次，全是模式 0），所以形状、图表线条、渐变文字边缘都是锯齿。

**先测原生。** `tools/d2daaprobe/d2daa.c` 在 winref 上把白色画在透明黑底上，alpha 即覆盖率：Windows 是精确的面积覆盖——
左边在 10.25 的矩形给 191，上边在 10.5 给 128；`PushAxisAlignedClip` 的抗锯齿边同样按面积（10.3 → 178，50.2 → 51）；
1 像素宽斜线一行的覆盖和约为 $w/\sin\theta$，没有像素被整个覆盖。

**顺带发现的曲线缺陷（`44fc0e8`）。** 同一探针的 ALIASED 输出里，一条强弯曲三次曲线围成的区域在 y=16 少了 8 个像素。
根因是上游的 FIXME：每段三次 Bézier 只用一段“中点最佳”二次曲线近似，误差为 $\frac{\sqrt3}{36}\lvert p_3-3p_2+3p_1-p_0\rvert$，
半径 500 的圆约差 4 像素。现在按参数等分，直到每段误差不超过曲线尺寸的万分之一（至多 32 段）；首段起点是 BEZIER 顶点，其余是
SPLIT_BEZIER 顶点——回到三次曲线的代码（包围盒、Simplify）本来就跳过它们，所以原始控制点仍是每段三次曲线一对。拆分后 ALIASED 输出与 Windows 逐像素一致。

**实现（`69802d1`）。** 每个抗锯齿图元先在它的设备空间包围盒（描边用 `GetWidenedBounds`——`DrawLine` 画的是 HOLLOW 图形，
路径的 `GetBounds` 会跳过它）与裁剪的交内，画一张 R8、8 倍多重采样的覆盖遮罩：三角形的边由多重采样给覆盖；曲线、描边曲线、圆角连接
不再在像素中心 `clip()`，而是按“隐函数值除以梯度”得到的像素距离给出盒式滤波覆盖；遮罩用 MAX 混合，同一图元的各部分重叠处不累加。
再画一个覆盖包围盒的四边形，画刷乘以遮罩各样本的平均。像素对齐的矩形（文字位图、`DrawBitmap` 常见）跳过遮罩。
抗锯齿裁剪在像素着色器里逐边按重叠面积计算，交集的每条边沿用它来源那个裁剪的模式；`Clear` 仍按像素中心。遮罩的局部清除要用
`ClearView`，而 d3d11 的 `ClearView` 是个什么都不做的桩——`53cf591` 实现了它：渲染目标视图按矩形清除（空矩形不动、没有矩形清整个），
UAV 暂时整个清除，并加了测试。

**走过的弯路：逐样本着色。** 最初想让遮罩这一遍逐样本着色，让曲线的 `clip()` 在每个样本处求值。结果 GL 下曲线正确而 Vulkan 下退化，
补偿后又反过来。`tools/d3d11sampleprobe` 在 Windows 上测清了 D3D 的语义：读 `SV_SampleIndex` 的着色器，普通属性仍在像素中心插值，
只有 `sample` 修饰才在样本处，`SV_Position` 始终是像素中心。Wine 两个后端各偏一边——GL 把普通属性放到样本处，Vulkan 的
`SV_Position` 给样本位置——而 vkd3d 的 HLSL 编译器根本不认 `sample` 修饰（上游 master 也没有）。d2d1 改用上面的解析覆盖率，
不依赖逐样本着色；这三处 D3D 缺陷随后单独修了，见下一节。

**结果：**

- 探针里矩形、裁剪与 Windows 逐值相同；直边是 8 级采样的量化值（三角形边 32 对 48），曲线边缘差十几级以内；GL、Vulkan 结果一致。
- d2d1 全量在 GL、Vulkan 下各 **21543 项/0 失败**（新增抗锯齿测试）；同一测试 PE 在 winref 桌面会话（GPU）21602 项，只有原有的顶点缓冲 2 项失败。
- d3d11 全量中新 `ClearView` 测试在 GL、Vulkan 下无失败（其余失败项与改动无关，均不调用 `ClearView`）。
- 部署后三应用保存回归通过（61 秒）；PowerPoint 渐变探针截图中椭圆外缘、渐变文字边缘平滑，四块镜像渐变拼成的“从中心”椭圆没有接缝。

**仍有的差异**：直边是 8 级量化而非精确面积；`Clear` 在抗锯齿裁剪内仍按像素中心；原样画出不受上限约束的 MITER 连接会被遮罩包围盒截掉（Windows 在上限处截平；后来在 `bbce22a` 修了，见“FillMesh 与描边连接”一节）。

## D3D 的逐样本插值语义，与 Vulkan 后端复用过期管线（wine-src `0eea94d`、`7a3e3e6`、`b6df687`、`fc2489e`）

上一节的探针 `tools/d3d11sampleprobe` 把 D3D11 的语义量清之后，逐项修 Wine：

- **vkd3d HLSL：`sample` 插值修饰。** 词法上用尾随上下文，只有后面跟着空白和标识符时 `sample` 才是关键字，
  把 `sample` 当变量名的着色器不受影响；代码生成映射到 `LINEAR_SAMPLE`/`LINEAR_NOPERSPECTIVE_SAMPLE`。
- **vkd3d SPIR-V：`SV_Position` 回到像素中心。** 未加 `sample` 修饰的 `SV_Position` 一律取 `floor(FragCoord.xy) + 0.5`：
  逐像素时 FragCoord 本就在 n+0.5，结果不变；逐样本时从样本位置回到像素中心。
- **wined3d GLSL：centroid 与 sample 插值。** 以前两者都只打 FIXME（Office 运行时见过的 “interpolation mode 0x3” 即 centroid）。
  GLSL 4.40 起插值限定符可以只写在片段着色器，但接口块成员上的 centroid/sample 两端必须一致，而顶点着色器变体在 4.40+ 并不按
  片段着色器的插值模式区分——所以两端都不声明，片段着色器把输入拷进 `ps_in[]` 时分别用 `interpolateAtCentroid`、
  `interpolateAtSample(…, gl_SampleID)` 取值；逐样本着色器里的普通输入用 `interpolateAtOffset(…, vec2(0.0))` 取像素中心。
- **wined3d Vulkan：着色器销毁后复用过期管线。** 修 Vulkan 时探针的结果随着色器创建顺序变化，一旦保留所有着色器就恢复一致。
  根因：图形管线缓存以 `VkShaderModule` 句柄为键，着色器变体销毁时只清掉上下文的当前引用，缓存里用该模块建的管线留着；
  驱动复用句柄后，新着色器命中旧管线，执行的是已销毁的着色器。现在销毁变体时把引用该模块的管线从缓存摘下并延迟销毁。

**结果：**

- 探针在 GL、Vulkan 下四种情形都与 Windows 逐值相同。
- d3d11 全量：**Vulkan 从 303 项失败降到 12 项**——大量测试在循环里反复创建、释放像素着色器，之前都撞上了过期管线；
  `ps_ibfe` 的 4 个边界用例在两个后端都已通过，去掉 `todo` 后 GL 从 17 项降到 13 项。新增 `test_sample_interpolation`
  （着色器由 Windows 的编译器编译）。
- 同一测试 PE 在 winref 桌面会话：5562179 项、180 项失败（上一次 179 项，多出的一项在 `test_unbind_shader_resource_view`，
  Windows 上与本次改动无关）；新的 `test_clear_view`、`test_sample_interpolation` 与去掉 `todo` 的 4 个 `ps_ibfe` 用例都通过。
- d2d1 全量两个后端仍 0 失败；部署后三应用保存回归通过（48 秒）。

## d2d1 的 FillOpacityMask，与 DrawBitmap 的倒置源矩形（wine-src `8755ae5`）

`FillOpacityMask` 的两个入口（1.0 带 content 参数的与 1.1 的）一直是桩：只校验参数、只往命令列表里录，画到位图目标上什么也不出。
它是“一张 alpha 位图当遮罩，画刷透过它上色”，灰度文字位图、图标蒙版常这样画，命令列表回放时也经由它。

**先测原生。** `tools/d2daaprobe/d2dmask.c` 在 winref 上把 4×4 的 A8 遮罩放大、缩小、截取后逐行打印 alpha：
遮罩按双线性采样、在遮罩自己的边上夹取（取中间 2×2 时边上照样混入源矩形外的纹素）；源矩形先规范化再与遮罩求交，完全在外时整块取角落纹素，
零宽时什么都不画；没有目标矩形时按源矩形的 DIP 尺寸放在原点（192 DPI 的遮罩是双线性缩小）；只用 alpha；content 只校验，非法值
`E_INVALIDARG`；非 ALIASED 目标上两个入口都是 `D2DERR_WRONG_STATE`。

**实现。** 把 `DrawBitmap` 的放置计算抽成 `d2d_bitmap_get_placement`（规范化、求交、映射到目标矩形或原点），用遮罩建一个 CLAMP、
LINEAR 的位图画刷当不透明度画刷去填目标矩形——正是 `FillGeometry` 的不透明度画刷通路，颜色画刷可以是任何类型。两个入口也补上了
不在 `BeginDraw` 内时的 `D2DERR_WRONG_STATE`。

**顺带修的相邻缺口。** 同一探针显示 Windows 的 `DrawBitmap` 也把倒置的源矩形规范化，而 Wine 遇到倒置的源矩形就整个忽略、改画整张位图。
现有测试只倒置过覆盖整张位图的源矩形，那时两种做法结果相同，所以一直没暴露。

**结果：**

- 探针在 GL、Vulkan 下与 Windows 逐行相同，只差恰好落在半数上的双线性值的舍入（±1）。
- 新增 `test_fill_opacity_mask`（含命令列表录制后回放、倒置源矩形的 `DrawBitmap`）；d2d1 全量在 GL、Vulkan 下各 **21945 项/0 失败**；
  同一测试 PE 在 winref 桌面会话 22004 项，只有原有的 2 项失败（顶点缓冲那两项）。
- 部署后三应用保存回归通过。

## d2d1 的 FillMesh 与描边连接的斜接上限（wine-src `ee62e28`、`bbce22a`）

**FillMesh（`ee62e28`）。** `CreateMesh`、`ID2D1TessellationSink` 与几何的 `Tessellate` 早已实现，唯独 `FillMesh` 只往命令列表里录、
画到位图上什么也不出。`tools/d2daaprobe/d2dmesh.c` 在 winref 上用 alpha 0.5 的画刷量出：网格就是依次画出的三角形列表——交叠处混合两次，
共享边按 D3D 的左上规则只画一次，两种绕向都画，退化三角形不画，受世界变换；非 ALIASED 目标与从未 Close 的网格都是
`D2DERR_WRONG_STATE`，空网格 `S_OK`。网格的三角形数组本身就是顶点缓冲（每三点一个三角形），直接走填充三角形的着色器、不用索引。
同一探针还量出 Windows 画 ALIASED 曲线也先折线化：半径 10.3 的圆 `FillGeometry` 填 326 个像素，比 Wine 的精确曲线（332 个）少；
圆的 tessellation 用 `FillMesh` 填出的 324 个像素 Wine 与 Windows 相同。

**描边连接（`bbce22a`）。** `tools/d2daaprobe/d2djoin.c` 量了 V 形与原路折返的描边在各种连接下画到哪里，结果与之前按 Windows 实测写的
`Widen` 一致，而渲染器有两处不同：

- MITER 连接超过上限时，Wine 按全长画尖角，Windows 在 `上限 × 半宽` 处垂直于角平分线截平。不给样式的描边就是上限 10 的 MITER，
  所以这影响所有默认描边的尖角（折线图的尖峰）：8° 的 V，Windows 画到尖点外 20 像素，Wine 画到 28.7。
- 原路折返（180°）时，上游代码不论连接类型都向前画一个 25 个单位长的矩形；Windows 是 MITER 画到上限、ROUND 画半圆、BEVEL 与
  MITER_OR_BEVEL 不延伸。

描边顶点现在带一个“在连接里的角色”：连接由连接点、两侧外边的端点和斜接尖点组成，尖点有两份，分别从两侧算出（同一点），
顶点着色器在截平时让每份沿自己那一侧退到上限线上，斜切时两份退到同一个斜切点，未超限时不动；原先那个与尖点对称的内侧点
（线段很短时会伸出线段之外）不再需要。折返处向前的两个顶点按样式决定伸出多远。ROUND 连接的尖点在两倍半宽处截平后再由像素着色器
按圆裁掉，避免尖角时出现极长的三角形。

**结果：**

- 两个探针在 GL、Vulkan 下与 Windows 逐行相同，只剩混合舍入（191 对 192）与两处贴边 0.04 像素的顶点定点化差异。
- 新增 `test_fill_mesh`、`test_stroke_join_limits`；d2d1 全量在 GL、Vulkan 下各 **23871 项/0 失败**；同一测试 PE 在 winref 桌面会话
  23930 项，只有原有的 2 项失败。抗锯齿探针的输出不变。
- 部署后三应用保存回归通过。

**仍有的差异**：Windows 画曲线（ALIASED 与抗锯齿都是）先折线化，Wine 用精确曲线，边缘差一个像素上下；ALIASED 模式下半透明描边
在连接处与线段重叠的地方会混合两次（抗锯齿模式用覆盖遮罩取并集，没有这个问题）。

## 按 Office 实际撞到的桩排序：COM 远端 QI 与加载失败的返回值（wine-src `b8692b5`、`048eaaf`）

图形这边补完 `FillOpacityMask`、`FillMesh` 和连接之后，用 `excel-charts.vbs`、`powerpoint-effects.vbs` 两个自动化探针在
`WINEDEBUG=fixme+d2d,fixme+dwrite,fixme+dcomp,fixme+d3d11,fixme+dxgi` 下各跑一遍：每一步都成功，d2d 只剩“选项被忽略”一类无害的
fixme，没有撞到任何绘制桩。改用全通道 `err+all,fixme+all` 看 PowerPoint 一次会话，排在前面的是：

- `err:ole:marshal_object Failed to create an IRpcStubBuffer ... {a6ef9860-...}`（IDispatchEx）**216 次**。脚本引擎对 PowerPoint 的远端对象
  探测 IDispatchEx，`IRemUnknown::RemQueryInterface` 对每个接口都调 `marshal_object`，而它先加载该接口的代理/存根工厂、建存根，
  直到存根连接或登记 ifstub 时才 QI 对象、失败、打 ERR。“对象不支持”本是这次远端 QI 的正确答案。`b8692b5` 改成先 QI，
  不支持就直接返回它的 HRESULT：结果不变，216 条 ERR 与相应的工厂加载、存根构造都没了；ole32 marshal/compobj、combase roapi 测试前后一致。
- `err:ole:apartment_add_dll couldn't load in-process dll "...\ClickToRun\msoxmlmf.dll"`。这条摘要里一直挂着的“路径问题”查清了：
  OfficeClickToRun.exe 的内嵌清单故意把 InfoPath 的 XML MIME 过滤器声明在自己目录里一个不存在的 `msoxmlmf.dll` 上，
  注释写明“只从我们的文件夹加载”——Windows 上同样加载失败。不是 Wine 的错。
- 顺带量出 `tools/comprobe/missingdll.c`：服务器加载不了时 Windows 返回加载器的错误（0x8007007e / 0x800700c1），Wine 一律
  `E_ACCESSDENIED`。`048eaaf` 改为 `HRESULT_FROM_WIN32(GetLastError())`，加了 `test_missing_server`；ole32 测试里原本就有
  `broken(hr == HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND))` 的注记，与此一致。同一探针还发现 **HKCU\Software\Classes 下注册的类
  Wine 的 COM 看不见**（HKCR 没有合并视图），见 `tools/comprobe/README.md`；Office 的类全在 HKLM，眼下不受影响。
- 其余：`taskschd` 读任务 XML 的大量 “unhandled element”（C2R 注册的计划任务）、urlmon `ProcessUrlAction`、
  `Windows.Security.EnterpriseData.ProtectionPolicyManager` 与 `Windows.UI.Composition.Interactions.*` 的激活失败、
  `wevtutil im` 桩；另有 2 次 `RemRelease failed`/`get_stub_manager_from_ipid not found`，改动前后次数相同。
  `{e19c7100-9709-4db7-9373-e7b518b47086}` 的 QI 失败（MSXML、netprofm、WMI 各种对象都被问到）不在任何 Windows/Wine 头文件里，
  像是 Office 内部“是不是自家对象”的探测，E_NOINTERFACE 正是应有的回答。

两次改动部署后三应用保存回归都通过。顺手修了 `scripts/build-winetest.sh`：测试目录里带同名 `.spec` 的源文件（ole32 的 `testlib.c`、
combase 的 `wine.combase.test.c`）是测试加载的辅助模块，不再编进测试程序；源文件名按整词匹配（原先会从 `wine.combase.test.spec` 里截出 `wine.c`）。

## 计划任务的定义对象与 XML 往返，以及 xmllite 在分块与文档结束处（wine-src `56fadec`、`edba3c9`、`8b58e3c`）

上一节 fixme 排序里 `taskschd` 的大量 “unhandled element” 背后是实在的缺口：C2R 用 `RegisterTask(xml)` 注册的任务在 Wine 里被解析成
一个几乎空的定义，再按 Wine 自己的格式写出去，存下来的是 `<Triggers/>`、空的 `<Principal>`、没有命令的 `<Exec/>`。
`tools/taskschdprobe/taskdump.c` 把 winref 上导出的 10 个 Office 任务逐个 `put_XmlText`，打印对象模型看到的一切和 `get_XmlText` 写回的
XML，再用对象模型造一个 11 种触发器俱全的任务并读回；Wine 与 Windows 相差 1719 行。

`56fadec` 按实测补全了定义对象：各类触发器及其重复模式与日程、可按序号或 id 删除的触发器集合、空闲/网络设置、失败重启、
`ITaskSettings2`、以 SID 保存并按名字返回账户的主体（含隐含的登录类型）、带 id 与 Context 的动作集合；XML 按 Windows 的格式写
（UTF-16 声明、CRLF、元素次序、哪些只在设置过才写），读时按 Windows 校验取值并返回同样的 `SCHED_E_*`。`scheduler.c` 加了逐字比对
XML 并读回的 `test_XmlText`，原来两处 `todo_wine` 通过；winref 与 Wine 上都通过（winref 另有一处与本改动无关的上游域名假设失败）。

往返时撞到 xmllite 两处真问题，各自量过 Windows 后修掉：

- `edba3c9`：UTF-16 文档每读一块都把原始缓冲里已拷过的数据再拷一遍，大文档的元素重复、乱序或丢失（taskdump 的“读回”一项就停在这里）。
- `8b58e3c`：元素未闭合时文档末尾会无限重复最后一个节点（元素每次深一层）——Windows 是 `MX_E_INPUTEND` 并粘住；注释、CDATA、
  处理指令的结束符跨在两次读入的块之间时整个看不见；只有声明的 UTF-8 文档触发断言，UTF-16 的把开头读两遍。按
  `tools/xmlliteprobe/xmleof.c` 的实测一并对齐了缺根元素、多根元素、非开头的 `<?xml`、名字处截断等返回值，`reader.c` 加了
  `test_document_end`、`test_markup_end_chunk_boundary`、`test_utf16_chunks`，winref 与 Wine 都通过，旧代码在跨块测试上死循环。

之后 taskdump 与 Windows 逐行一致；三应用保存回归通过；Office 重新注册的 5 个任务存下来有全部触发器、Users 组主体、命令与参数。
还没对齐的：`tools/taskschdprobe/regtasks.c` 显示 Windows 的 `IRegisteredTask::get_Xml` 是服务端自己的规范化序列化（另一种元素次序、
省略默认值、规范化时长、补 `<URI>`），任务文件是 UTF-16LE；Wine 返回定义的 `get_XmlText`，文件是 UTF-8 加一行注释。另有
`RegistrationInfo.SecurityDescriptor`、各部分自己的 `XmlText`、`_NewEnum`、ComHandler 等其他动作类型、各对象的 IDispatch 仍是桩。

后续（wine-src `13a0fba`、`c8d033d`、`ff3d5e4`）：把 taskschd 余下的桩按 winref 实测补完。IDL 按 Windows 的类型库改成双接口、带 SDK 的
DISPID 与参数，并加上 Windows 10 的 IExecAction2、IPrincipal2、ITaskSettings3、IMaintenanceSettings 与三种非 Exec 动作；每个对象的
IDispatch 经类型库实现（最派生接口），集合有了枚举器，`Schedule.Service` 的 VBScript 在 Wine 与 Windows 上输出逐字一致。兼容级别按
Windows 的规则由各 setter 抬高、由声明的版本约束读取；注册信息的 SecurityDescriptor 也保存了。探针见 `tools/taskschdprobe/README.md`。
顺手让 `scripts/build-winetest.sh` 给测试目录里标了 `makedep client` 的 IDL 生成客户端桩（schedsvc 的 rpcapi/atsvcapi 由此可编）。

## DWM 窗口属性照 Windows 11 实现，以及自绘标题栏的窗口不再被窗口管理器装饰（wine-src `1af6bd6`、`5d52423`）

三应用在 `err+all,fixme+all` 下的普查里，WinRT 类已全部能激活；剩下有实质的是 `DwmGetWindowAttribute(33)`（Windows 11 的圆角偏好）
返回 `E_NOTIMPL`、`DwmSetWindowAttribute` 是完全的桩。`tools/dwmprobe/dwmattr.c` 在 winref 的桌面会话里测了 0–40 号属性的读写、尺寸与
窗口种类规则，`5d52423` 照此按属性表实现（值存为窗口属性，跨进程可见），dwmapi 测试加了 `test_window_attributes`，winref 与 Wine 都通过。

答对之后 Office 改走 Windows 11 的框架：标题栏画进客户区，只留左右下 4 像素与顶部 1 像素的非客户区。win32u 按“有非客户区就按样式的
标准框架（含标题栏）缩可见矩形、让窗口管理器装饰”处理，结果 GNOME 标题栏盖住了 Office 自己的标题栏。`1af6bd6` 让客户区伸到标题栏
位置的窗口保持整窗可见、不加装饰；user32 `win` 测试的失败与改动前相同（均为该显示上的输入/焦点环境项），三应用回归通过。那几像素的
非客户区现在由 Wine 按经典样式画成细边框；Windows 11 上它们是隐形的，之后可以再细化。

## COM 激活过滤器照 Windows 实现，以及 OLE 从数据建对象看不见嵌入对象（wine-src `5510cc7`）

Word 启动时调用 `CoRegisterActivationFilter` 注册自己的过滤器；Wine 原先接下就不再理会，等于任何激活都不经 Office 过问。
`tools/comprobe/actfilter.c` 在 winref 上把 COM 激活对象的每条途径走了一遍（契约见 `tools/comprobe/README.md`）：每次激活都先问过滤器、
再找类（没注册的类也先问）；替换参数进来总是 GUID_NULL，顶替的类用同一类型再问一次；类型按途径区分——直接创建与 `OleCreate` 为 0，
类 moniker 与 `OleLoadFromStream` 为 FROM_MONIKER，`OleLoad` 为 FROM_STORAGE，`OleCreateFromData` 为 FROM_DATA，
`CoGetInstanceFromFile` 为 FROM_FILE，文件 moniker 为两者之和；代理/存根工厂只在首次载入时问，combase 自带的标准代理不问；
只有一个过滤器，另注册一个是 `CO_E_NOT_SUPPORTED`，COM 不引用它。`5510cc7` 照此实现（类型经线程局部状态从 ole32 告诉 combase），
新测试 `ole32:activation` 在 winref 与 Wine 上都 134 项 0 失败，其余 ole32/combase 测试的失败与改动前相同，三应用保存回归通过。

顺带修了同一路径上的缺口：OLE 的剪贴板格式只在第一次用剪贴板时注册，此前 `OleQueryCreateFromData` 回答 S_FALSE、
`OleCreateFromData` 找不到任何嵌入对象；现在按需注册，并按 Windows 的次序先问 `Embedded Object`、再取 `Embed Source`。

Office 里：普通会话中过滤器被问几十次，全是类型 0、全部放行。ActiveX 控件在激活之前就被 Microsoft 365 的策略拒绝
（“由于您的策略设置，无法插入此对象。”）。`tools/officeautomationprobe/word-embed.vbs` 在 Word 里嵌入 Excel 工作表时，过滤器放行了
`Excel.Sheet.12`，Excel 以 `-Embedding` 启动并注册了类对象，随后 Word 对 Excel 的一次跨进程调用返回 0x800703e6，Word 报 Excel
“尚未安装或无响应”——换回改动前的 combase/ole32 结果相同，是下一个要查的既有缺陷。

## Word 里嵌入并就地编辑 Excel 工作表：四层依次挡住的缺口（wine-src `8741ba9`、`548ca9b`、`c3cde46`、`ab9bb8e`）

上一节的 0x800703e6 是 Excel 在服务 `IOleObject::DoVerb` 时访问违例（服务端桩把异常变成 ERROR_NOACCESS 回给 Word）。修一层露出下一层，
四层各自量过 Windows：

1. **comctl32 跨进程读子类栈**（`8741ba9`）。故障地址在 comctl32 v6 的 `GetWindowSubclass`：Excel 对 **Word 的窗口**调它，`GetPropW` 取回的是 Word
   进程里的指针（窗口属性跨进程可见），解引用即崩。`tools/subclassprobe/subclass.c` 测得 Windows：别的进程的窗口三个辅助函数都返回 FALSE、
   不动任何东西；同进程别的线程能 Get、能 Remove、不能 Set；`DefSubclassProc` 在子类过程之外返回 0；v6 的属性名是 `UxSubclassInfo`。
2. **从发送来的消息里向外调用**（`548ca9b`）。随后 Word 在 `document.Close` 里卡死：Word 主线程停在 `NtUserDestroyWindow`（向 Excel 的跨进程子窗口
   发 WM_DESTROY 并等待），Excel 在 WM_DESTROY 里调 `IOleInPlaceSite::DiscardUndoState` 并等 Word。`tools/comprobe/inputsync.c` 测得 Windows 的规则：
   处理别的线程发来、仍在等回复的消息时，只有 `[input_sync]` 方法能调出去（以发送消息投递，等待中的调用方照样处理），其余立即
   `RPC_E_CANTCALLOUT_ININPUTSYNCCALL`——Wine 原先只在已有未完成调用时才拒绝。并逐个方法量出 Windows 11 上哪些是 input_sync（11 个），widl 把
   `RPCFLG_INPUT_SYNCHRONOUS` 写进过程标志，通道经 ORPCTHIS 带到对端，服务端对 STA 改用 SendMessage；`TranslateAccelerator` 的代理照 Windows
   本地回 S_FALSE。COM 自己用 SendMessage 让主套间建对象（DM_HOSTOBJECT）不受此限。
3. **默认处理器的 QI**（`c3cde46`）。CrossOver 把处理器对自身未实现接口的 QI 一律改成 CO_E_OBJNOTCONNECTED，且只对进程内服务器转发，
   运行中的本地服务器对象（Excel）的 IOleInPlaceObject、IDispatch 都拿不到。`tools/comprobe/handlerqi.c` 在 winref 桌面会话里用真的 Excel 测得：
   从未运行 E_NOINTERFACE，运行中转给对象，运行过再关闭才是 CO_E_OBJNOTCONNECTED，IOleLink 始终 E_NOINTERFACE。这也修好了 ole32 `marshal`、
   `ole2` 两个一直失败的测试。运行中返回指针的身份仍不是处理器（Windows 聚合了代理管理器，Wine 的 `CoGetStdMarshalEx` 还是桩）。
4. **CrossOver 的 shm surface 在持 USER 锁时跨进程发送**（`ab9bb8e`）。`OLEFormat.Activate` 时 Excel 整个卡死：插桩（线程退出各步、USER 锁的
   持有者与获取时的回溯、持锁代码的检查点）一路追到——父窗口在别的进程的子窗口用 CX HACK 23950 的共享内存 surface，其 flush 向父窗口
   进程发 `WM_WINE_FLUSHSHMSURFACE`；设窗口位置时 `apply_window_pos` 持着 USER 锁调 `update_surface_region`→设形状→flush，发送前的
   `user_check_not_lock` 断言失败，线程带着 USER 锁停住；下一个退出的线程在 user32 的 THREAD_DETACH 里持着加载器锁等 USER 锁，其余线程
   等加载器锁，winedbg 附加也因要建线程而卡住。现在持锁时 shm flush 只保留脏区，锁释放后的下一次 flush 再发。

之后 `tools/officeautomationprobe/word-embed.vbs` 在 Wine 下完整走完：嵌入工作表、保存（docx 里有 `word/embeddings/Microsoft_Excel_Worksheet.xlsx`
与预览 EMF）、重开、就地激活、经 `OLEFormat.Object` 读到 `Sheet1`、关闭、退出；三应用保存回归通过，ole32/combase/comctl32 测试的失败数与
改动前相同（其中两个既有失败被修好），新测试在 winref 与 Wine 上都 0 失败。仍未解决：一次 Word 在 `Quit` 时于 wwlib 内空指针崩溃（未能复现）；
偶尔 Word 退出后 Excel 过几秒才退出；默认处理器的聚合身份；只有 IUnknown 的本地服务器对象 Windows 也能 `OleRun`（ole_server 测试的 todo_wine）。

## 就地激活之后看得见工作表：Excel 主窗口、消息过滤器、Word 的滚动动画与跨进程的 DC（wine-src `e7f8f28`、`c28e1b3`、`728920b`、`6d87101`、`4dee4a1`）

上一节之后脚本走得完，但激活的工作表看不见：Word 窗口里多了 Excel 的功能区和编辑栏，文档里却只有嵌入对象的预览图，以及几块带阴影边框的
残影。一层层查下来：

1. **Excel 的主窗口盖在 Word 上**（`e7f8f28`）。就地激活时 Excel 先显示自己的主窗口、随即隐藏；在窗口管理器还没映射它时隐藏，winex11 在映射
   完成后按“期望状态”撤回——但下一次不带 SWP_HIDEWINDOW 的位置变化把“正在映射”当成窗口仍要显示，重新设了可见样式，把撤回换成了映射，
   一个空白窗口留在 Word 上面。现在按期望状态判断。
2. **消息过滤器**（`c28e1b3`、`728920b`）。IRemUnknown 的调用不该交给应用的过滤器（Word 的过滤器每次都去 QI COM 的内部对象）；调用类型按线程
   是否在 COM 里等待区分（`tools/comprobe/msgfilter.c`）。之后看到 Word 的过滤器对 Excel 的 `IOleInPlaceFrame::SetBorderSpace` 答
   `SERVERCALL_RETRYLATER`——Wine 照样执行了。`tools/comprobe/retrylater.c` 量出 Windows 的全套语义（退回调用方、由调用方过滤器的
   `RetryRejectedCall` 决定放弃或何时重发，没有过滤器的单线程套间一律 `RPC_E_CALL_REJECTED`，多线程套间区分两种回答，`htaskCaller`
   是调用方线程），进程内与跨进程都与 Wine 逐行一致。这一层单独修好不改变画面，但它是 Word 与 Excel 之间确实会走到的路径。
3. **Word 自己把 Excel 的就地窗口藏起来了**（`6d87101`）。`tools/officeautomationprobe/embedevents.c` 记录窗口事件：EXCEL9（Excel 的就地窗口，
   是 Word 文档窗格 `_WwG` 的跨进程子窗口）显示、隐藏、移动、再显示，约 100 毫秒后又被隐藏，此后再没显示。`+win` 显示最后那次
   `SetWindowPos(…, SWP_HIDEWINDOW|SWP_NOREDRAW…)` 来自 Word 的主线程。在 Word 里对 `win32u!NtUserSetWindowPos` 下断点（`tools/bptrace`，
   `BPTRACE_ARG7=8f` 只报这种隐藏，`BPTRACE_STACK` 扫栈），用 wwlib 的公开符号解析（见下）得到：
   `CRootLayer::SetScrollArea → ScrollTo → BeginScrollZoomAnimation → ALayer::HideHwnds → CHwndLayer::HideHwnds`。即 Word 的 AirSpace 渲染层
   开始一次滚动/缩放动画时，先藏起它承载的子窗口；动画结束（`CRootLayer::HandleViewportMove → EndScrollZoomAnimation → ShowHwnds`）再显示——
   隐藏不足 250 毫秒就用 `MsoSetTimer`（→ `SetCoalescableTimer`，id 0x119）250 毫秒后由 `ShowHwndTimerProc` 再试。
   对这些函数同时下断点（它们很少被调用，不改变时序）看到：第一次动画结束了；第二次 `BeginScrollZoomAnimation` 来自一次**目标与当前相同**的
   `ScrollTo(-116, -325)`，之后“视口移动”通知再也没来。`ScrollTo` 只和自己记下的“上次位置”（`CRootLayer`+0x148/+0x14c）比较，而这个位置被
   动画期间的通知改写为 -326——**差 1 像素**。
   通知的来源是 WinComp（Windows.UI.Composition）后端的 InteractionTracker：Word 用 `TryUpdatePosition(-325, ClampingOption.Disabled)` 滚动，
   同时 Content 视觉的 Size 用 267 毫秒的缓动动画变化，跟踪器的 MinPosition/MaxPosition 是引用 Content.Size 的表达式动画，经
   `IsPositionRoundingSuggested ? Round(Floor/Ceil(…))` 取整，上下界始终相差 1。Wine 的空闲跟踪器每帧把**上一帧已钳过的位置**再钳进新边界，
   于是位置先被钳到 -445，再被上升的下界一路推到 -326 停住（下界最终是 -326、上界 -325）。现在以不钳位方式请求的位置被保留：边界钳住的是
   显示的位置，边界容得下时就回到请求的位置；新请求、Adjust、离开空闲状态都结束这种保留。之后 EXCEL9 在激活期间一直显示，工作表的
   列标、行号、选中的 A1、滚动条与阴影边框都画在文档里。

   **这一步未经 Windows 实测**：winref 当时锁屏，锁屏会话不产生合成帧——`tools/trackerprobe/clamp.c`（复刻上面的情形：不钳位地请求一个边界随后才
   到达的位置）在 Windows 上一个回调都收不到、位置始终为 0，接了窗口目标也一样。依据只有文档的一句“Idle 与 CustomAnimation 状态下
   Position 总在 MinPosition 与 MaxPosition 之间”，以及 Word 在 Windows 上不可能这样一直藏着就地窗口。会话解锁后应先跑 `clamp.exe`
   （`scripts/winrun.sh --desktop`）对照，再决定保留还是修正这种语义。**10 月 2 日已在桌面会话实测：Windows 并不保留请求的位置**，
   这段语义已撤掉，见文末“交互跟踪器按 Windows 逐帧对齐”。

4. **文档盖住了工作表的上半部分，工作表旧位置留着残影**（`4dee4a1`）。在 X 上直接截屏（Wine 的 `PrintWindow` 只拷窗口表面，与屏幕不符）
   看到：合成层画的文档（嵌入对象的预览图）盖住了 EXCEL9 的上半部分，旧位置下方露出 EXCEL9 的旧像素。`+win,+x11drv` 显示 Word 的合成
   线程每帧经 `_WwG` 的缓存 DC 取 `WS_CLIPCHILDREN` 的系统区域、把离屏内容 StretchBlt 到顶层 X 窗口；这个裁剪里挖掉的洞正是 EXCEL9 的
   **旧位置**（下移 122 像素）。EXCEL9 在 Excel 进程里移动，`invalidate_dce` 只把 Excel 进程的 DC 标脏，Word 进程的缓存 DC 一直用旧的可见区
   （dce.c 里本就注明“跨进程失效尚未支持”）。现在父窗口在别的进程时，向它投递 `WM_WINE_INVALIDATEDCE`，由它的线程把该窗口及其后代的 DC
   标脏。之后在 X 上截屏，就地工作表完整画在文档里，没有残影。

当时还没解决的（后文“Word 就地激活时换上 Excel 的功能区”一节解决了它）：Excel 的功能区显示在 Word 的功能区**下面**而不是取代它。Excel 建了共享菜单后调 Word 的 `IOleInPlaceFrame::InsertMenus`，
Word 的实现（`SDOF::InsertMenus`，转给 Office 命令栏对象的虚方法 +0x1d0）第一步就对这个菜单调 `GetMenuInfo`，Wine 的菜单只在创建它的进程里
可用（`grab_menu_ptr: other process menu`），于是 E_INVALIDARG；Excel 随即销毁共享菜单、以空菜单调 `SetMenu`（Word 答 E_FAIL），Word 保留
自己的功能区。Windows 的 HMENU 跨进程可用；要在 Wine 里做到，得把菜单操作转给创建菜单的线程（句柄表记着它）代办，或把菜单放进共享内存，
连同菜单项里的子菜单与位图句柄。winref 上拿不到原生的就地激活，无法对照 Word 换功能区的具体做法。

### Office 的 PDB 要经 OMAP 才能对上地址

wwlib、mso 等 Office DLL 链接后经过重排（BBT），公开 PDB 里的符号仍是链接器给的地址：PDB 另存链接时的节表和一张 OMAP 表。不经换算，
每个地址都对到毫不相干的函数上（wwlib 自己导出的 `FMain` 被解析成一个 JSON 序列化函数），PDB 的 GUID 却与 DLL 完全匹配。
`scripts/pdb-addr2sym.py` 现在先按 DBI 可选调试头里的 OMAP_TO_SRC 把映像 RVA 换回源地址，再按原始节表找最近的公开符号；导出函数
都对到自身 +0x0。`--lookup 名字片段` 反过来经 OMAP_FROM_SRC 给出函数在映像里的 RVA，用来反汇编与下断点。

### winref 上做 Office 自动化的限制（2026-09-28）

- 以 `new` 选项只启动自己的 Word：机器上一直开着用户的 WINWORD（`/restore` 启动，没有标题）。
- 新建文档会等“云创建引导 / 自动保存新文件？”对话框（它显示账户名与邮箱，读到的内容不保留），回答它会改用户设置，所以改用 `existing`
  打开事先做好的文档。
- 嵌入的 Excel 三次都没能就地激活（0x17B5），Excel 从未建 EXCEL9；Excel 带 .NET/WebView2 与 Acrobat 的加载项，会话锁屏。
- 自动化启动的 WINWORD 退出后常剩一个线程卡在内核里，`taskkill /F` 回答没有该实例，进程却一直列着。

## 文件字节范围锁照 Windows 完成：重叠加锁、完成端口与等待中的锁（wine-src `6057e5f`）

原来 NtLockFile 拒收状态块、APC 和完成端口，只在成功时置事件；重叠句柄上需要等待的锁返回 STATUS_PENDING 之后就没人管，
既不授予也不完成。LockFileEx 不传状态块，OVERLAPPED 从来不写。每个 Office 进程启动时打印一次的
“I/O completion on lock not implemented yet”并不说明调用者绑了完成端口：hEvent 低位为 0 时 kernelbase 总把 OVERLAPPED
当完成上下文传下去，任何一次 LockFileEx 都会打出它。Word 运行时实际持有的锁（`/proc/locks` 里属于 wineserver 的条目按
inode 对到文件）是 ole32 复合文档在 `~WRF{…}.tmp`、`STO*.tmp` 上 0x7FFFFF93 一带的范围锁，和 OfficeClickToRun 遥测
SQLite 库的锁，都在同步句柄上立即授予。

`tools/fileinfoprobe/lockiocp` 在 Windows 上测到的（`lockiocp.win.txt`）：

- 立即授予、立即拒绝（LOCKFILE_FAIL_IMMEDIATELY）都像普通 I/O 一样完成：写状态块（拒绝为 STATUS_LOCK_NOT_GRANTED，
  Information 0），置事件，投递完成包，拒绝也投递；hEvent 低位置 1 时不投递。句柄设了
  FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 时，没挂起的一律不投递，**拒绝也不投递**，比文档字面的“立即成功”更宽。
- 需要等待的返回 ERROR_IO_PENDING，Internal 为 STATUS_PENDING，InternalHigh 不动，事件不置。挡路的锁被解开或其句柄
  关闭时授予（STATUS_SUCCESS）；CancelIoEx 取消为 STATUS_CANCELLED（完成包错误 995）；自己文件对象的最后一个句柄关闭时
  为 STATUS_RANGE_NOT_LOCKED（错误 158）。每种都置事件并投递完成包，不受 SKIP 标志影响。
- 同步句柄上等待的锁阻塞到授予；立即拒绝同样写状态块、置事件。
- NtLockFile 不给状态块时，在加锁之前就返回 STATUS_ACCESS_VIOLATION。

实现：lock_file 请求带上 async 参数；固定部分放下 async 与偏移后只剩空隙放标志，count 进可变数据。服务端把重叠句柄上
需要等待的锁作为请求 async 挂到 inode 的等待队列；inode 上任何锁移除（解锁、句柄关闭、进程退出）时按先后授予不再冲突的，
文件对象最后一个句柄关闭时以 STATUS_RANGE_NOT_LOCKED 完成它的等待锁，取消走通用的 async 取消；被 Wine 之外的 Unix 锁
挡住时每 100 ms 再看一次，与同步句柄客户端的轮询一致。客户端对立即结果用 `file_complete_async`（状态块、事件、APC 或
完成包，SKIP 标志由服务端的 add_fd_completion 过滤）。LockFileEx 把 OVERLAPPED 当状态块并先写 STATUS_PENDING，
LockFile 传本地状态块。

64 位与 32 位（wow64，用 i686 mingw 直接编探针）在 Wine 下的输出都与 Windows 逐行一致；kernel32 `file` 测试新增
`test_LockFile_overlapped` 与无状态块用例，Wine 0 失败，Windows 上只有与锁无关的 3 个既有失败；ntdll `file`、`pipe`
0 失败；Office 回归通过。

- 协议版本变为 1814：部署时先停前缀的 wineserver，wineserver 与 ntdll.so 一起换。
- 测试里 `ov.Internal` 是 ULONG_PTR，要先转成 NTSTATUS 再和负的状态常量比，否则 64 位上符号扩展后永远不等，Windows 上也一样。
- 前缀里被杀掉的 Word 留下的文档，下次以桌面方式启动 Word 时会全部作为恢复文档打开（标题“最后由用户保存”），
  `office-debug.sh close` 只关得掉一个窗口。

## Word 就地激活时换上 Excel 的功能区：跨进程的菜单与菜单栏，以及跨进程呈现的偏移（wine-src `4239112`、`7886337`、`8fd4a53`、`d4b9f9a`）

上一节剩下的问题出在菜单：Excel 建好共享菜单后请 Word 的 `IOleInPlaceFrame::InsertMenus` 放进容器的菜单组，Word 先对它调
`GetMenuInfo`，而 Wine 的菜单只活在建它的进程里。`tools/menuprobe` 在 Windows 上量出（`menuprobe.win.txt`）：另一个进程对菜单
的读、改、插入自己的弹出菜单、删除、销毁全都可以；它可以把别人的菜单设成自己窗口的菜单栏，窗口为它让出位置、排好版，销毁窗口
时连菜单一起销毁；可以跟踪别人的弹出菜单，通知里说的是那个菜单本身；建菜单的进程退出时，别人插进来的弹出菜单随之消失。

1. **对另一个进程的菜单的调用**（`4239112`）：连同要设的或要读的文本打包，作为内部消息 `WM_WINE_MENU_CALL` 发给拥有菜单的线程，
   由它执行并送回结果与错误码；拥有者线程要像处理别的线程对它窗口的调用那样收发消息。之后 Word 用 Excel 的功能区换下了自己的。
2. **把别人的菜单显示成自己的菜单栏、跟踪别人的弹出菜单**（`7886337`）：排版、绘制和跟踪都要菜单项在本进程。现在本进程显示
   另一个进程的菜单时经一份本地副本：菜单项照那边的样子，子菜单句柄照原样，所以应用收到的 WM_INITMENU、WM_INITMENUPOPUP、
   WM_MENUSELECT、WM_MENUCOMMAND、WM_MENUCHAR、GetMenuBarInfo 与 MN_GETHMENU 仍是它认识的菜单。菜单成为窗口菜单或第一次显示时
   建副本，菜单栏排版、DrawMenuBar、以及已通知应用菜单即将显示之后刷新，菜单销毁时丢弃。
   这一步决定了 Excel 的工具栏放在哪里。Word 就地激活的顺序是 GetBorder → SetBorderSpace → SetActiveObject → InsertMenus → SetMenu；
   `SDOF::GetBorder` 给的是框架客户区减去 MWD 记着的四边留白（+0x154～+0x160），Word 只在 `AppWndProc` 处理 WM_SIZE 时调 Excel 的
   `IOleInPlaceActiveObject::ResizeBorder`。`SDOF::SetMenu` 里 `TBS::UIActivate` 经 `TBS::FUpdate` → `OTBSU::HrSetBorderSpace` 更新
   留白，再由 `TBS::SetMenu` 调 `SetMenu(框架, 共享菜单)`：菜单栏出现、框架客户区变矮、WM_SIZE、`ResizeBorder`（顶边为 0），
   Excel 才按新边框重排。菜单栏排不出来时没有这次 WM_SIZE，Excel 按旧边框摆放。
3. **跨进程呈现的偏移**（`8fd4a53`）：菜单栏出现后，工作表只剩最上面约 20 像素。Excel 用 D3D/DirectComposition 画的子窗口内容
   呈现到顶层 X 窗口时，要加上顶层客户区在 X 窗口里的偏移，而这个偏移只存在拥有顶层的进程（Word）的驱动数据里；Excel 进程里
   没有它，内容被放高了一个菜单栏。`get_window_rectangles` 现在也回可见矩形（即 X 窗口的矩形），另一个进程的顶层由此得到偏移，
   `NtUserGetWindowRects` 把任一窗口的三个矩形交给驱动。
4. **菜单栏矩形**（`d4b9f9a`）：窗口为菜单栏让出的位置在菜单项上方有 1 像素的边；Windows 的 GetMenuBarInfo 给的矩形与菜单项等高，
   比让出的位置少 1 像素，Wine 原来整整多算了这一像素（menu 测试里原有的 todo）。

`tools/officeautomationprobe/word-embed.vbs` 就地激活后，Word 的菜单栏是“文件(F) 窗口(W)”，下面是 Excel 的快速访问工具栏、功能区
与编辑栏，文档里的工作表有列标、行号和选中的 A1。user32 `menu` 测试新增子进程把父进程菜单设成菜单栏的用例（Windows 上同样通过），
`msg`、`win` 失败数与基线相同；`menuprobe` 在 Wine 下与 Windows 逐行一致。

还没做的：

- 菜单的排版只存在显示它的进程：第三个进程对这个窗口调 GetMenuBarInfo 拿不到矩形（Windows 由 win32k 统一排版）。
- 拥有菜单的线程必须收发消息，否则对它菜单的调用要等到超时。

## 格式与控制字符不占宽度也不画，DrawText 照 Windows 断行（wine-src `9b5bdea`、`b0d674a`）

就地激活后 Word 的菜单栏显示成“文件□□(F) 窗口□□(W)”：Office 合并菜单组时给菜单名加了零宽空格（U+200B），Wine 按字体的缺省字形
量和画。`tools/textprobe/zwspprobe` 在 Windows 上测了菜单字体（微软雅黑 UI）、Tahoma、Courier New、Wingdings 与 Marlett
（`zwspprobe.win.txt`）：

- 这些字符不管字体有没有字形、字形多宽，文本宽度都不算它们、也不画：C0 里只有 TAB、LF、CR 与 U+001C～U+001F（U+0000、U+0001、
  U+001B、VT、FF 不在内），C1 全部，U+034F，U+061C，U+200B～U+200F，U+2029，U+202A～U+202E，U+2061～U+2064，U+206A～U+206F，
  U+FE00～U+FE0F，U+FEFF。U+2028、U+2060、U+2066～U+2069、U+00AD、U+180E、U+FFF9～U+FFFB 等不在内。GetCharWidth32 不受影响。
- 符号字体（Wingdings）只剩 C0 那几个、U+200B～U+200D、变体选择符与 U+FEFF；Marlett 连 C0 也不算（原因未明，Wine 自带的 Marlett
  只用来画界面符号，没有照做）。
- 设了字符间距（SetTextCharacterExtra）时全部不做，宽度退回每个字符的 GetCharWidth32 加间距；ExtTextOut 带 ETO_IGNORELANGUAGE
  时也不做，照字形画。说明这是 gdi32 的语言处理（LPK，经 Uniscribe）做的，不是 win32k。
- 调用方给 lpDx 时，这些字符的宽度保留（位置空着），唯独 U+200B～U+200D、U+FEFF 和 U+061C 的宽度被丢掉。
- GetTextExtentExPoint 算能放下几个字符时，这些字符只有结束在上限**之前**才算放得下（恰好在上限处不算），U+200B～U+200D、U+FEFF 与
  组合用的 U+034F、变体选择符跟随前一个字符；开头的零宽字符同样按“之前”算，开头的变体选择符跟随后一个字符。

实现分两层，与 Windows 一致：gdi32 做了语言处理（没有 ETO_IGNORELANGUAGE、不是字形索引）时，ExtTextOutW 不再给 win32u 加
ETO_IGNORELANGUAGE，GetTextExtentExPointW 传 `NTGDI_GETTEXTEXTENT_LANGUAGE`；win32u 在没有字符间距时去掉这些字符的前进宽度，
绘制时把它们从串里拿掉、一次交给驱动（给了 delta 的并到前一个字符上）。不用“按宽度逐个定位”的办法，是因为 Wine 里同一字体的
GGO_METRICS 前进宽度和单色位图的前进宽度可以不同（Wine 的 Tahoma 11 像素时 'a' 是 7 与 6），按宽度定位的串会和不含这些字符的
同一串画得不一样。双向文本走 Uniscribe 的路径：Uniscribe 标为零宽的字形同样拿掉、宽度扣掉；符号字体不做双向处理（Windows 上
Wingdings 里的方向控制符照样画出来）；这条路径的 GetTextExtentExPoint 顺带改成不问能放几个时照样填满各字符的范围，簇的宽度按
字符累计分摊。

DrawText 因此暴露了断行的差别：user32 `text` 测试里 “Hello &World!\tThis…” 原来只因制表符有缺省字形那么宽才通过，制表符不占宽度后
断在了 “Hello ” 之后。`tools/textprobe/linebreakprobe` 在 Windows 上测到（`linebreakprobe.win.txt`）：DrawText 在空格、制表符与全角
空格后断行，断处的这些空白被吃掉；不在不间断空格、VT、en space、零宽空格处断，也不在连字符后断；东亚全角字符与相邻字符之间可以断，
但不让“不可行首”的标点开头（它退回前一个字符之前），开括号可以留在行尾，长音符“ー”与小假名可以开头，单书名号 ‹ › 不能结尾。
“不可行首”表按中文（简体）Windows 实测，写在 `dlls/user32/text.c`。Wine 原来用 Unicode 断行算法的空白与软断点，两处都与此不符。

两个探针在 Wine 下与 Windows 逐行一致（Marlett 那一行除外）；gdi32 `font` 与 user32 `text` 新增的测试在 Windows 上通过，gdi32
`font`、`metafile`、`path`、`dib`、`mapping`、`dc`，user32 `text`、`menu`、`edit`、`static`、`listbox`、`combo`，comctl32 的
button、static、edit、listview、tooltips，usp10 与 riched20 `txtsrv` 与基线相同；riched20 `editor` 只多出与桌面共用剪贴板的
粘贴失败，多跑几次时新旧版本互有多少。Office 回归通过。

还没做的：

- 双向文本里调用方给的 lpDx 被整个丢掉（原来就如此），所以方向标记“保留宽度”在这条路径上不成立（测试里的 todo）。
- 非双向路径没有簇：Windows 把组合字符与前一个字符的宽度在两者间分摊（GetTextExtentExPoint 的逐字范围），组合用的浊点
  U+3099/U+309A 在 DrawText 里跟随前一个字符。
- GetCharacterPlacement 的 lpDx 仍取 GetCharWidth32；Windows 对字体里没有的字符给 0，并按簇分配。
- Windows 对字体里没有的字符用字体链接（SystemLink）的字体量宽度，与 Wine 的缺省字形宽度不同（探针里 U+115F、U+17B4 等）。
- 上面说的 GGO_METRICS 与单色位图前进宽度不一致本身：Wine 的 GetTextExtentPoint 与实际画出的宽度在这种字号上不相等。

## 嵌入字体的子集：fontsub 照 Windows 实测实现（wine-src `1bf949b`）

Word 导出一页文字的 PDF 写出了 19.3 MB，存为“嵌入字体”的 DOCX 带着 16 MB 的字体。Office 嵌入字体时调 `fontsub.dll` 的
`CreateFontPackage` 取子集（导出 PDF 用字形列表、平台 3、编码 0xFFFF；嵌入字体用字符列表），Wine 的实现把整个字体原样返回。

测量分两步。`tools/fontsubprobe/fontsubprobe` 在 Windows 上把各种调用方式都试一遍（`fontsubprobe.win.txt`）；然后 `compare.cmd` 在同一台
Windows 上把同一批输入交给系统的 fontsub 和 Wine 的 fontsub（Wine 的 PE 版只导入 kernel32、ntdll、ucrtbase，改名为 `wfontsub.dll`
直接加载），逐表比较（`compare.win.txt`），48 组，包括宋体（含点阵）、MS Gothic、Arial、Times、等线、微软雅黑、SimSun-ExtB、Segoe UI、
Segoe UI Emoji、Tahoma、Cambria、Verdana、Consolas、Calibri、Georgia 与十几种自由字体。微软字体的子集只在那台机器上比较。

Windows 的做法：

- **保留字形号**：没保留的字形只是没有轮廓与字符，其余表照旧引用字形号。除了要求的字形或字符，还保留字形 0–2、JSTF 扩展字形、给了列表时
  '"'、'M'、'd'、'r' 的字形、组合字形的部件、GSUB 能替换出的所有字形、COLR 的图层。
- **glyf/loca**：每个字形补齐到 2 字节，能放下就用短 loca。**hmtx/vmtx**：长度量数取 min(原数, 最后一个保留字形 + 2)，丢弃的字形度量为 0；
  若长度量数不变，整表原样不动（所以保留了高编号汉字时 hmtx 不变小）。**LTSH、hdmx**：丢弃字形的项为 0，hdmx 每个字号的最大宽度按保留字形重算。
- **cmap**：按记录顺序逐子表重建，共用的子表仍共用，子表起点补齐到 2 字节；format 0/4/6/12/13 只留映射到保留字形的字符（映射到字形 0
  的不算），空的 format 4 留一个 [0,0] 段、空的 format 12 留一个 {0,0,0} 组；format 14 原样照抄；重建后比原表大的保留原表。
- **OS/2 的首末字符**：只有当某个平台 3 的子表**自己**被重建（不是与前面平台 0 的记录共用）时才更新，取各重建子表（format 4/6/12/13，
  Mac 子表要在第一个 Unicode 子表之后才算）的最小、最大字符。FontForge 出的字体 (0,3) 与 (3,1) 共用一个子表，所以 Liberation、
  DejaVu 等的范围保持原值——这是用互换表的字体在 Windows 上二分出来的，之前以为与 LTSH/hdmx 有关是错的。
- **EBLC/EBDT**：每个字号按原次序保留含保留字形的索引子表，范围缩到保留字形；format 1/3 都写成 format 3（2 字节偏移），format 2（等宽
  区间）在保留字形连续时仍为 2、否则改为 format 5 列表，4/5 只留保留的项；多个字号共用的点阵仍共用；字号的起止字形取原范围内首末保留字形；
  没有子表的字号去掉；某个 format 3/4 子表的点阵超过 64 KB 时整个字号去掉，但已写进 EBDT 的点阵留着（宋体保留 2000 个汉字时 ppem 16/17
  就是这样）；一个字号都不剩时两表都去掉。原先 Wine 遇到 format 2/5 就整段保留：MS Gothic、uming 的子集几乎带着全部点阵。
- **其他**：maxp 的各最大值按保留字形与 fpgm/prep 重算；kern 只留两边都保留的字形对，没有就去掉；post 变 3.0；name 的每个字符串放进
  **所有记录中最长的**包含它的字符串里（没放过就先放那个），否则找已放过的相同字符串，否则追加；没有 maxp 的字体报 ERR_NO_GLYPHS
  （1009），缺 loca 报 1035；不带 `TTFCFP_FLAGS_SUBSET` 时返回 0、什么也不写；COMPRESS 标志被忽略。
- **表的物理顺序**：Windows 自己安排的表（head hhea maxp OS/2 hmtx LTSH VDMX hdmx cmap fpgm prep cvt glyf loca kern name post gasp PCLT
  vhea vmtx EBLC EBDT）按这个顺序；其余表的顺序看不出规律（Windows 的 fontsub 导入了 msvcrt 的 qsort/bsearch，但按“并列元素交给
  qsort”模拟也对不上），Wine 按原目录顺序、目录以上述表结尾时把第一张挪到最后，这在 48 组里对上 13 组。

结果：48 组中 44 组所有表都与 Windows 相同，其中 12 组逐字节相同，其余只是物理顺序不同。剩下 4 组：name 里有三个字符串 Windows 没放进
现成的容器（DejaVu、Gentium、Segoe UI Emoji 各一处，对齐、距离、长度都排除了，原因未明）；Gentium 的 Mac format 6 子表 Windows 把字符 c
的字形写在下标 c − 2 × firstCode 处（firstCode 为 32 时 32–63 的字符全丢、其余错位），这是 Windows 的缺陷，Wine 不照做。

Word 导出那一页 PDF：19.3 MB → 39 KB，`pdftoppm` 渲染与嵌入完整字体时逐像素相同；嵌入字体的 DOCX：字体 16 MB → 未压缩 780 KB，重新打开
正常。fontsub 的一致性测试 82 项在 Wine 与 Windows 上都通过，其中新测试用构造的 cmap/name 表固定了 OS/2 范围、format 14、长度量数不变与
name 的“最长容器”几条规则。

还没做的：

- `MergeFontPackage` 只会原样复制完整的包；SUBSET1（字形号压紧并加 `dttf` 表）、DELTA 与合并没有实现（`fontsubprobe.win.txt` 里有 Windows
  的结果）。Office 目前只用 SUBSET。
- 其余表的物理顺序与 name 的三处例外。

## 公式、用户活动与稀疏包：Word 与 Excel 启动和编辑时的三处（wine-src `a84f7a5`、`5a8ec28`、`dcf9a14`、`06d5d17`、`4b20d5a`）

**公式是空的（`a84f7a5`）。** `tools/officeautomationprobe/word-math.vbs` 在文档里构建两个公式，Word 数得到、`BuildUp` 也成功，但屏幕上和
导出的 PDF 里一个字形都没有。Word 的默认公式字体是 Cambria Math，只有带 OpenType MATH 表的字体它才排公式；Windows 总有这个字体，
Office 不自带，Wine 也没有。Word 用 GDI 按名字建字体，Wine 给了 Liberation Sans，没有 MATH 表，于是公式全空。把文档的公式字体
换成带 MATH 表的自由字体就出现了。现在没有 Cambria Math 时，已装的第一个自由数学字体顶替这个名字（和注册表
`HKCU\Software\Wine\Fonts\Replacements` 里写的替换同一机制，注册表里写的优先）：DejaVu Math TeX Gyre、STIX Two Math、Latin Modern Math、
TeX Gyre 各款等，TrueType 轮廓的在前。默认设置下公式在屏幕与 PDF 上都正确；gdi32 的 font 测试开关替换时失败项相同。

DirectWrite 原来只读注册表里的替换，认不得 win32u 内置的这一条（`5a8ec28` 补上，注册表里没有 Replacements 键时也生效）。Word 界面
经 DirectWrite 查的族名里只有 Cambria Math 查不到（“Microsoft YaHei UI”“DengXian”在前缀的 `C:\windows\Fonts` 里，都找得到）；导出 PDF 时
Word 向 DirectWrite 要这个字体的 PostScript 名，找不到就拿族名“Cambria Math”去命名嵌入的 DejaVu Math 子集，现在 PDF 里的名字是真实的
“DejaVuMathTeXGyre-Regular”，画面逐像素相同。gdi32 与 dwrite 各加了测试：有 Cambria Math 时它必须带 MATH 表，在 Wine 上装了任一自由数学字体
就必须有 Cambria Math；Windows 上都通过，关掉替换（`CX_TURN_OFF_FONT_REPLACEMENTS`）时 Wine 上各失败 9 次。

选 CFF 轮廓的数学字体（Latin Modern Math，或 Wine 在同名的 .ttf/.otf 中挑了 .otf 的 STIX Two Math）时，Word 导出 PDF 不嵌入它，而是
把每个字形画成与行高等高的一位蒙版图，画面上出现贯穿的竖线。`tools/glyphrasterprobe` 对比了 GDI：字形位图两边基本一致，竖线不是
从 GDI 的位图来的，原因未查清。它另外量到两处与 Windows 不同：`GGO_BEZIER` 对 CFF 字体，Windows 给原来的三次曲线（每条记录 3 点），
Wine 把三次控制点当二次处理后再转回三次（每条记录 6 点）；`GGO_METRICS` 的返回值 Windows 是正的大小，Wine 是 1。

**CFF 轮廓与 GetGlyphOutline（`dcf9a14`）。** `tools/outlineprobe` 在 Windows 上把两款 CFF 字体从 16 到 4000 像素逐点量了一遍：`GGO_BEZIER`
把每段三次曲线原样给成一条 3 点记录；`GGO_NATIVE` 把它给成一条 $n$ 段的二次样条记录，三阶差分 $d=p_3-3p_2+3p_1-p_0$ 两个方向都不超过
1 像素时 $n=1$，否则取满足 $\max(|d_x|,|d_y|)\le 10n^3$ 的最小 $n\ge2$，按参数等分，每段控制点是 $\frac{3(a_1+a_2)-a_0-a_3}{4}$——1240 段曲线全部吻合。
Wine 原来把三次控制点当二次控制点，两种格式的形状都不对（GDI 路径里的文字也走 `GGO_NATIVE`），现在照这条规则给；段数取决于设备上的大小，
所以求所需大小时也先做变换。`GGO_METRICS` 返回字形 GLYPHBITS 的大小 $\mathrm{align}_4(16+\lceil w/8\rceil h)$（252 个实测值全部吻合）。
顺带查出：`FIXED` 转 `FT_Fixed` 经过 `unsigned int`，64 位的 unix 侧零扩展，传给 `GetGlyphOutline` 的矩阵里任何负元素都变成约 65536——
转半圈时字形黑框宽 52428000 像素；原有测试只查 16 位的 `gmCellIncY`，按 65536 取模恰好蒙对。gdi32 新测试带一个自制的小 CFF 字体，
Windows 上通过，旧代码失败 40 处。还不同的是黑框：Windows 贴着曲线（或黑像素），Wine 用含控制点的控制框，旋转后差得更多。

**每个窗口的用户活动请求管理器（`06d5d17`）。** Word 打开文档时向 `UserActivityRequestManager` 的工厂要
`IUserActivityRequestManagerInterop`，用 `GetForWindow` 取文档窗口的管理器，注册一个请求处理器。`tools/useractivityprobe` 在 Windows 上量到：
本进程的窗口（子窗口也算）得到管理器，同一窗口持有期间是同一个，另一窗口是另一个；没有窗口或已销毁给
`ERROR_INVALID_WINDOW_HANDLE`，别的进程的窗口（包括桌面）给 `E_ACCESSDENIED`；管理器不是 agile 的；处理器的令牌就是它的地址；
桌面程序调 `GetForCurrentView` 得 `ERROR_NOT_FOUND`（Wine 原来会造一个）。现在逐项相同，新测试在 Wine 与 Windows 上都通过。

**Office 的 AI 稀疏包（`4b20d5a`）。** Excel 每次启动先找包家族 `aimgr_8wekyb3d8bbwe`，找不到就用 `IPackageManager9::AddPackageByUriAsync`
注册安装目录里的 `root\Integration\Addons\aimgr.msix`，外部位置是 `Office16\AI`——给 Office 的 AI 组件包身份的稀疏包。Wine 原来没有
`IPackageManager9`，Excel 拿到 `E_NOINTERFACE` 后抛 C++/WinRT 异常。现在接口在，部署照这个客户端的其他部署一样以 `ERROR_NOT_SUPPORTED`
的操作结束（Wine 没有包仓库），跟踪里写出包和外部位置。`AddPackageAsync` 按 Windows 拒绝开发模式；application model 测试原来没跟上
“包查询回空列表”的实现，一并改好。

顺带确认的两件：`office-paste.vbs` 里 `PasteSpecial` 作为对象不给位置时，Word 把工作表粘成浮动的 Shape（类型 7），给 `wdInLine` 时是
InlineShape（类型 1），都是 `Excel.Sheet.12`——此前以为粘不上，是只数了 InlineShapes；`word-type.vbs` 用会话内 SendInput 键入中文，
Word 正确收下，9 月初记录的“键入中文就崩溃”不再出现（经 XIM 的组字路径未测）。

## 中文输入法：经 XIM 的组字、候选窗与输入上下文（wine-src `149ce1d`、`b7bd4c8`，以及 `a4c1a79`、`dcf9a14`）

此前只用 `SendInput` 验证过 Word 收中文，那不经过 X 的输入法。`tools/imetest` 在一个 X 显示上单独起 fcitx5 或 ibus（独立的 D-Bus 与配置，
不碰桌面上的输入法），用 XTEST 发真实按键，再读回文档：两种输入法都能正确上屏，Word 用 `ImmGetCompositionString` 取结果。fcitx5 的 XIM 只有
over-the-spot，预编辑与候选都由它画在 `XNSpotLocation` 处；ibus 有 on-the-spot（预编辑回调），这是 Wine 请求的样式。

发现并修好的三处：

- **候选窗的位置（`149ce1d`）。** 插入符位置只在 over-the-spot 时、并且只在输入上下文已经存在时才告诉输入法，而上下文要等窗口第一次
  获得焦点或按键才建，那时 Word 早已放好插入符：ibus 的候选面板停在屏幕左上角，fcitx 在插入符第一次移动前也画在左上角。现在驱动记住
  窗口最后的插入符矩形，建上下文时给出、获焦时再设一次（ibus 只转发已获焦上下文的位置，fcitx 只认 set 而不认 create 时给的），回调样式
  也设；位置取插入符底部（XIM 规定 spot 在基线上），候选窗出现在插入行下方而不是盖住它。原生 Xlib 客户端 `ximtest` 证明 ibus 在回调样式下
  也按 spot 放面板。
- **子窗口的输入上下文（`149ce1d`）。** `NotifyIMEStatus` 拿焦点窗口（Word 文档是没有 X 窗口的子窗口）去取输入上下文，结果建了一个客户窗口
  为 0 的上下文：预编辑状态和关闭时的重置都落在它上面，键盘代码还会把它当顶层窗口的上下文去查字符。现在用顶层窗口的，没有 X 窗口的
  窗口不再有自己的上下文。
- **组字窗口（`b7bd4c8`）。** Word 把组字窗口留给 IME 放（`CFS_DEFAULT`），自己不画组字串；内置 IME 原来把它放在窗口左下角之下，ibus 打的
  组字出现在状态栏下面。线程有插入符时，现在放在插入符处，大小正好容下字符串。

同一轮普查（三个应用各存一次文件，FIXME 与 ERR 按消息归并）另修了 `GetLongPathNameW` 对 `\\?\C:\…` 原样返回的问题（`a4c1a79`，C2R 服务
每次启动问五个这样的目录；原来 1024 字符的栈缓冲在长路径下还会越界），并确认 ClickToRun 目录里的 `msoxmlmf.dll` 在 winref 上同样不存在。
OfficePLUS（中国版的模板与美化加载项）是 VSTO 加载项，服务也是 .NET 程序，需要 .NET Framework 4.x 与 VSTO 运行时，前缀里都没有，尚未处理。

## Word 功能普查：单文件网页、图表、XML 文档与 AI 组件的线程池（wine-src `d9f12b3`、`ef0a28b`、`aaa4324`、`7d3052f`，外层 `87b2ff0`）

`tools/officeautomationprobe/word-sweep.vbs` 逐项跑一遍依赖 Windows 组件的 Word 功能：标题样式与目录、脚注尾注、批注、修订、表格样式、
目录里每种格式的图片、图表、SmartArt、艺术字、文本框、超链接、页码、通配符查找、Word 自己能写的每种格式（docx、doc、rtf、txt、htm、mht、
odt、dotx、xml）、导出 PDF 与 XPS、加密保存后再用密码打开（错误密码被拒）、比较文档、限制编辑。每项记下结果或错误，一项失败不影响
后面的。第一次跑有三项失败，修好后现在只剩 WebP 图片（windowscodecs 没有 WebP 解码器）。

- **单文件网页（`d9f12b3`）。** 存 MHT 之前 Word 先在注册表里找 `CLSID_IMimePropertySchema`，找不到就提示去装 Outlook Express 或 Windows Mail——
  这个消息框 `DisplayAlerts` 挡不住，文件没存，自动化的保存一直等着。Windows 给 inetcomm.dll 注册了 31 个类；其中 Wine 实现了对象却既没注册
  也没有类工厂的四个（属性模式、国际化、属性集即正文、消息树）现在都有了。类工厂造的国际化对象是新的一个，不是 `MimeOleGetInternat` 给的那个，
  和 Windows 一样。
- **图表（外层 `87b2ff0`）。** Word 插图表时要 Excel 的图表数据宿主，两个类在 Excel 的 App-V 注册表里只有 AppID（本地服务器是 EXCEL.EXE 的那个类的
  AppID）。Windows 上由 App-V 的 COM 集成为它们启动 Excel；普通 COM 找不到服务器，`AddChart2` 失败（0x1066）。导出注册表时现在按 AppID 给这类
  类补上本地服务器，Excel 以 `-Embedding` 启动后注册两个类对象，图表插入成功。
- **XML 文档（`ef0a28b`）。** 存成 Word XML 文档（`wdFormatFlatXML`）时，Word 用 SAX 读取器逐个解析包里的部件，内容处理器是 `MXXMLWriter`。写出器的
  `putDocumentLocator` 返回 `E_NOTIMPL`，读取器按致命错误处理，第一个部件就停了，Word 说文件写不了。现在返回 `S_OK`（它的 `IVBSAXContentHandler` 本来如此）。

**AI 组件的线程池与纤程存储（`aaa4324`、`7d3052f`）。** Office 的 AI 组件（`Office16\AI` 下的 `aitrx.dll`、`ai.dll`）从 kernel32 导入
`SetThreadpoolTimerEx`，又在 kernelbase 里查 `FlsGetValue2`，两个 Wine 都没有。Ex 版的定时器与等待照原版设置对象，返回设置前它是否还在等：
一次性定时器触发后是 FALSE，周期定时器是 TRUE，等到了对象的等待是 FALSE；`TlsGetValue2`、`FlsGetValue2`（Windows 11 24H2）取值同原版但不动
最后错误。新测试在 Windows 与 Wine 上都通过，Word 的日志里这两处缺失不再出现，三个应用的回归照常通过。

同一份日志里还有几处导入 Wine 没有、这几轮都没被调用到的函数（调用会以“unimplemented function”终止，日志里没有）：App-V 子系统
（`AppVIsvSubsystems64.dll`）要 `RtlIsNameInExpression` 与 AVL 通用表的枚举、判空；WinAppSDK 的 `CoreMessagingXP.dll`（Word 启动时经 React Native
宿主激活 `Microsoft.UI.Dispatching.DispatcherQueue` 时加载）要 ALPC、等待完成包（`NtCreateWaitCompletionPacket` 一族）与 shcore 的功能用量记录；
`mso30win32client.dll` 要 msvcp140 里宽字符的 `moneypunct` 与 `money_put` 的 `id`（Wine 的 msvcp 没有货币类 facet）。

## App-V 子系统的 AVL 表与名字匹配（wine-src `c92e5b9`）

Word 等应用都加载 Office 自带的 `AppvIsvSubsystems64.dll`（C2R 的进程内虚拟化层，经 `Office16` 里的重解析点指向 ClickToRun 目录的那份），
它的表是 ntdll 的 AVL 通用表，名字用 `RtlIsNameInExpression` 匹配。Wine 的 AVL 函数全是空操作——初始化不初始化，插入不分配也不返回，
查找永远为空——枚举、删除与 `RtlIsNameInExpression` 或缺或一调就异常。`tools/avlprobe` 在 Windows 上量了树形与平衡因子（BalancedRoot 的
Balance 是再平衡的哨兵）、删除时谁来顶替、游标与 RestartKey 在插入删除后的去向、`LikeADirectory` 的规则，以及 2160 组通配符匹配（含 DOS 的
`<`、`>`、`"` 与 NT 算法的怪癖），实现后探针输出与 Windows 逐行相同，ntoskrnl 原来永远返回 FALSE 的 `FsRtlIsNameInExpression` 也用它。

App-V 子系统启动时仍抛两次（都被接住）`windows_exception_impl<0>`：它唯一的抛出点是注册表类 `reg_key`，打不开的是
`HKLM\SOFTWARE\Microsoft\AppV\Subsystem\ComExclusions` 和 `HKLM\SOFTWARE\WOW6432Node\Microsoft\AppV`——Windows 自带的 App-V 客户端
（`Enabled=0`）的配置，前缀里没有；它们只是 COM 与对象名的排除表，未补。

## WebP 图片：Office 先找 WebP 图像扩展的 MF 变换，再用 WIC 解码（wine-src `c6061ac`、`2330bd8`、`3adedf3`）

Word 普查里最后一项失败是插入 WebP：`AddPicture` 不报错，但文档里什么也没有，取形状属性得 E_FAIL。Windows 11 build 29671 的 WIC 自带
“Microsoft Webp Decoder”（还有 HEIF、Raw、JPEG XL 解码器），Wine 没有；可补上解码器后 Word 仍然什么都不插，而且根本没加载 windowscodecs。
跟踪到 Word 先调 `MFTEnumEx(视频解码器, 输入 Video/{7693e886-…})`——子类型就是 WIC WebP 解码器的 CLSID——没有结果就放弃。Windows 上的
结果是 Store 的 WebP 图像扩展包登记的 “WebpImageExtension”：可以激活，除 `GetAttributes` 外每个方法都返回 `E_NOTIMPL`，只是个标记；
WIC 解码器报的是 MF 错误码，背后也是这个扩展包（`tools/mftwebpprobe`）。

- `libs/webp` 捆绑 libwebp 1.6.0 的解码部分。Windows 的解码结果与 libwebp 逐字节相同：静态图是 `WebPDecodeRGBA`，动画帧是
  `WebPAnimDecoder` 合成的整幅画布，一律非预乘 32bppRGBA、72 DPI（`tools/wicwebpprobe`，7 个样本逐项吻合）。
- 解码器照 Windows 的怪癖：静态图只要头能读出尺寸就创建成功，解码时才报 `MF_E_INVALID_FILE_FORMAT`；动画须完整，否则创建时 `E_UNEXPECTED`；
  `GetFrame` 越界也成功，取像素得 `MF_E_INVALIDINDEX`。动画的循环次数与每帧时长是元数据，由 ANIM、ANMF 两个读取器给出（有组件信息、
  不能 COM 创建，与 Windows 一样）。
- windowscodecs 登记同样的标记变换；mfplat 的 `MFTEnumEx` 原来不给注册表里的变换名字、只给过滤那一侧的类型，照 Windows 补全。
- 顺带补齐 WIC 组件信息：各编解码器的四个 `DoesSupport*`、元数据处理器的 `DoesRequireFixedSize`、签名状态（一律 Signed）、SpecVersion
  原来是桩或缺失；DDS 编码器登记进编码器列表；几个元数据读取器/写入器的名字与标志改成 Windows 的。

结果：Word 普查 43 项全部通过，WebP（有损、带透明、动画）都插得进去，文档里存成 PNG，与 libwebp 的解码逐像素相同（带透明的差 ≤2，
是 Word 自己的预乘舍入）。三个应用的回归照常通过。还缺的 WIC 组件：CUR、DNG、HEIF、Raw、JPEG XL 解码器与 HEIF、JPEG XL 编码器，
以及大量像素格式、转换器和元数据处理器（Windows 89/5/45/43，Wine 30/1/16/7）。

## Excel 功能普查：动态数组让工作簿“损坏”——msxml 的属性按名查找（wine-src `2dbd95f`）

`tools/officeautomationprobe/excel-sweep.vbs` 仿照 Word 的普查逐项跑 Excel：新旧公式（动态数组、XLOOKUP、LET、LAMBDA、具名 LAMBDA）、
数字格式、条件格式、经典与新式图表、迷你图、表、数据透视表、排序筛选、数据验证、批注、超链接、目录里每种格式的图片、形状与 SmartArt、
查找替换、分列、单变量求解、Excel 能写的每种格式（xlsx、xlsm、xlsb、xls、csv、txt、xml、ods、htm、mht、xltx）、加密保存后用密码打开
（错误密码被拒）、再打开存下的工作簿、保护。第一次跑时，凡是存过的工作簿都打不开（1004），加密的那份也一样。

- **根因。** 最小复现是一个 `=SEQUENCE(3)` 的工作簿：存下再开，Excel 报“发现……中的部分内容有问题”，修复记录是 `/xl/metadata.xml`
  部分的元数据。Excel 用 msxml6 的 SAX 读取器读包里的部件；读 `<ext uri="{bdbb8cdc-…}">` 时调 `getValueFromName(NULL, 0, L"uri", 3)`
  取无命名空间的属性。Windows 的 msxml6 把“没给命名空间、长度 0”当作无命名空间，找得到；Wine 返回 `E_POINTER`，Excel 跳过了动态数组
  的扩展，把整个部件当坏的。
- **Windows 的行为（Windows 11 build 29671，msxml3 与 msxml6 分别量）。** msxml3 对缺命名空间或本地名一律 `E_INVALIDARG`，只有缺
  index 才 `E_POINTER`；msxml6 接受 `(NULL, 0)`。msxml6 在检查任何东西之前先把输出清零（index 为 0，字符串 NULL、长度 0），失败也一样；
  msxml3 不动。无命名空间的属性报空串而不是 NULL。`getType*` 报 CDATA 或 DTD 声明的类型（关键字；枚举为 `(a|b)`，记法为
  `NOTATION (a|b)`；同一属性以第一次声明为准）——Wine 原来这三个函数都是 `E_NOTIMPL` 桩。msxml3 把命名空间声明按开始标签里的位置
  夹在其他属性之间，msxml6 放在最后；两者都把 DTD 补上的默认属性排在指定的属性之后。取值、取类型的查找先查输出指针。
- **实现。** 读取器接上 libxml2 的 `attributeDecl` 回调记下声明的类型；libxml2 把命名空间声明与属性分开交给回调，msxml3 的次序从刚解析
  过的开始标签原文里读回（属性值里不会有 `<`，开始标签就从解析位置往前最近的 `<` 起）。`xml:` 前缀属性的命名空间原来每个属性分配一次、
  从不释放，现在每次解析只分配一次。新测试（msxml3 与 msxml6 各一份）在 Windows 与 Wine 上都通过；原来因次序不同而标 todo 的两处
  msxml3 属性序列测试现在直接通过。

修好后普查 80 项全部通过，加密保存再打开、错误密码被拒、重新打开存下的工作簿都正常。原先仅剩的“失败”是瀑布图、树状图、旭日图、直方图、
箱形图、漏斗图这类新式图表的 `SetSourceData` 报 445（“对象不支持此操作”）：在 winref 上用原生 Excel（build 20522）跑同样的步骤，结果完全
一样，而 `FullSeriesCollection` 两边都给出 1 个系列——这是 Excel 本身的行为，普查已改为对新式图表只问系列数。

顺带看到的差异：同一个 445 错误，Wine 的 vbscript 中文文本是“对象不支持此动作”，Windows 是“对象不支持此操作”。

**同一读取器的相邻缺口（wine-src `7525a31`）。** 读取器接受声明处理器、DTD 处理器却从不调用它们，也不认识 DTD 声明的任何实体：
第一处对内部实体或参数实体的引用就是致命错误（“Entity 'i' not defined”“PEReference: %pe; not found”）。现在用解析器自己的一个文档存
libxml2 的实体，并按 Windows 实测报告：`elementDecl` 的内容模型取声明原文去掉空白（libxml2 的树分不出 `(a)*` 与 `(a*)`、`(a,(b,c))`
与 `(a,b,c)`）；`attributeDecl`、`internal/externalEntityDecl`（替换文本里字符引用已换、实体引用保留，参数实体名带 `%`）、
`notationDecl`、`unparsedEntityDecl`；内容里内部实体的替换前后报 `startEntity`/`endEntity`（嵌套照嵌套），DTD 里参数实体包住它带来的
声明；属性值里的实体直接替换、不报边界；不读的外部子集报 `skippedEntity("[dtd]")`，外部实体与“外部子集本可能声明”的未声明实体也
报 `skippedEntity` 而不是出错。msxml3 把缺省的公共/系统标识报成 NULL，msxml6 报空串，并把系统标识按基准 URL（或当前目录）解析成绝对
URL；`getBaseURL`/`getSecureBaseURL` 与对应的 put 原来都是桩。两份新测试（msxml3、msxml6）在 Windows 与 Wine 上都通过；Office
的包里没有 DTD，改后两个普查与回归照常通过。

## VBScript：移植上游、补齐错误表与译文，以及对象参数、Null 与流名（wine-src `33bb4986`…`ffa8703d`）

起因是 Excel 普查里同一个 445 错误，Wine 的中文描述是“对象不支持此动作”，Windows 是“对象不支持此操作”。顺着查下去，Wine 的 vbscript
缺的远不止译文：字符串表 35 条（Windows 130 条），`Eval/Execute/ExecuteGlobal/GetRef`、`DateDiff/DatePart/DateValue/TimeValue`、`Filter`、
`Escape/Unescape`、`Get/SetLocale`、`LenB` 一族、`InputBox`、`LoadPicture` 全是 E_NOTIMPL 桩。`tools/vbscriptprobe` 的五个脚本在 Windows 与
Wine 上逐项对照。

- **移植上游。** 上游 Wine 自 11.0 起有 180 个 vbscript 提交（绝大多数是 Francis De Brabandere 的），补上了上面多数函数、编译错误号与大量
  解析细节。CrossOver 没改过 vbscript，除一个全局的版本资源提交外 179 个都干净地套上，结果与上游 master 逐文件相同（只差版本资源）。
  其中 And/Imp 与 Null 的测试依赖同期的 oleaut32 修正，于是把上游 oleaut32 自 11.0 起的 25 个提交一并移植（VarAnd/VarImp 的 Null、
  VarDecRound、VariantTimeToSystemTime 的毫秒溢出、格式化函数的几处越界等），oleaut32 与 vbscript 的全部测试通过。之前本地实现的 ChrW/AscW
  与一版错误号修正先撤回，由上游版本取代。
- **错误表与译文（`33bb4986`）。** 在上游 88 条之外补齐到 Windows 的 130 条（只缺 30000，其中文是“ZH”，英文原文无从核实）。中文取自
  Windows 自己的 vbscript.dll 与 jscript.dll 字符串表（`tools/stringtableprobe`；两者共享的条目用词一致），59 条已有译文改成 Windows 的说法，
  新增 79 条；vbscript 的 “File not found”（Windows 是“文件未找到”）与 comdlg32 的（“找不到文件”）用 msgctxt 分开。
- **行为（`62b2634e`）。** 内建函数拿到对象参数时先取其默认属性值、取不到即报那个错（Dictionary → 450，RegExp → 438），只有
  `IsObject/IsArray/IsDate/IsEmpty/IsNull/IsNumeric/TypeName/VarType/Array` 按对象本身接收，`VarType` 返回默认值的类型（取不到才是 9）；
  `TypeName` 先问 `IProvideClassInfo`（Dictionary 是 “Dictionary” 而不是 “IDictionary”）；Chr/Sqr/String/StrReverse/Round/Rnd 对 Null 报 94，
  Trim 系列返回 Null；Exp 溢出报 6；`CreateObject("")`/`GetObject("")` 报 5；Split 计数 0 得空数组；非 Option Explicit 下调用未声明的名字报 13；
  `LoadPicture` 实现为 Picture 对象（找不到 432、路径不存在或空名 76、非图片 481）；引擎版本报 10.8.16384、文件版本 10.0.29671.1000，与这版
  Windows 相同。
- **oleaut32（`fcc0548e`、`3622d349`）。** 中文（简、繁）的 `VARIANT_LOCALBOOL` 布尔串在 Windows 上仍是 True/False、俄语 True 是 Истина
  （`tools/boolstrprobe`），Wine 原来是“真/假”——VBScript 的 `CStr(True)`、字符串拼接都受影响。`OleLoadPicture` 对非图片数据报
  `CTL_E_INVALIDPICTURE`（无 placeable 头的元文件仍是 E_FAIL），`OleLoadPictureFile("")` 得空图片、目录不存在报 `CTL_E_PATHNOTFOUND`。
- **scrrun（`9beb1290`）。** `GetFile` 对目录不存在或指向文件夹一律“文件未找到”，`GetFile/GetFolder("")` 是无效参数，`DeleteFolder` 不存在的
  文件夹是“路径未找到”。
- **ntdll 的流名（`ffa8703d`）。** 冒号只在路径最后一段表示流：中间一段带冒号（`C:\Windows:x\y`、`file:C:\x`、`win.ini:bad\name`）在 Windows
  上是 `ERROR_INVALID_NAME`，Wine 原来当成目录名去找、报“路径未找到”；`文件::$DATA` 是文件自己的数据流，Windows 能打开，Wine 原来找不到。
  unix 命名空间里的名字不受影响；kernel32 里原本标 todo 的 `CreateFileA("c:c:\\windows")` 现在直接通过。

对照结果：`expressions.vbs`、`runtime.vbs`、`objects.vbs`、`files.vbs` 与 Windows 逐行相同，`errors.vbs` 只差 30000。新测试在 Windows 与 Wine 上
都通过（vbscript 的 run 测试在中文 Windows 上另有 26 处失败，全是上游用例拿英文描述比较），三个应用的回归与 Word、Excel 普查照常通过。

## msxml3：上游写出器的修复，SAX 读取器的属性与两个上限，以及 libxml2 自己的限制（wine-src `afad9ace`…`010cc274`）

动手前先查上游：自 wine-11.0 起上游 msxml3 有 232 个提交，Nikolay Sivov 把 SAX 解析器、DOM 与 XPath 都改成了不依赖 libxml2 的自有实现
（`bd3176206a`、`21735ee834`、`c23cbae37a`，到 9 月还在修崩溃），XSLT 仍走 libxslt。这与这里基于 libxml2 的十几个本地提交（ProhibitDTD、
MaxElementDepth、文档流、属性查找、DTD 报告……）改的是同一批函数；整体换过去要把这些 Windows 实测语义在新解析器上重做一遍，而对 Office
没有直接收益，风险却不小。所以暂不整体移植，只取与解析器无关、能独立套上的部分：

- **写出器（MXXMLWriter）。** 上游 7 个提交：`putDocumentLocator` 返回 S_OK（与本地 `ef0a28b` 同一修正，改用上游写法）、换行统一写成
  CRLF、属性值里的换行 4.0 起写成 `&#xA;`、CDATA 里的换行也规范化、开了缩进时第一个元素前不加换行、`endElement` 不缩进（会改变文本内容）。
  上游的相应测试依赖一批测试文件重构，改写成本地测试文件的写法并入；4.0 写属性值换行的期望单列（上游把 4.0 的行挪到了 msxml4 测试里）。
- **读取器与 DOM 的旧解析器提交。** VB 接口的 `startElement/endElement` 不再收到 NULL 的 uri，`createNode` 接受类型名，设置处理器的 trace，
  以及 SAX 错误消息表 `msxml.mc`（查消息改为先查本模块）。上游的 VB 内容处理器测试与本地一个测试同名，本地那个改名。

**读取器的属性（`3d006c71`）。** 上游测试里 VB 接口取处理器属性的类型是 todo；顺着看，读取器的 `getProperty` 除两个处理器与 `xmldecl-version`
之外全是 `E_NOTIMPL`，`max-xml-size`、`max-element-depth` 设非零值也是 `E_NOTIMPL`。`tools/saxpropprobe` 在 winref 上量了 3.0 与 6.0 的全部
属性（见其 README）：处理器经 VB 接口是 `VT_DISPATCH`；XML 声明三项 6.0 保留上一个文档的、3.0 永远 NULL；各属性的默认值与 3.0/6.0 的
差别；不认识的名字是 `E_INVALIDARG`。两个上限照实测实现：大小以 KB 计，流按字节、字符串 6.0 按字节而 3.0 按字符；超限时 3.0 先开始文档
再以 `E_ABORT`（消息“System error: MaxXMLSize.”）停在 1:1，6.0 不开始文档、以 0xc00cee91 停在 1:0；嵌套超限停在那个开始标签、不报它的
`startElement`，3.0 的位置在名字开头、6.0 在名字之后，错误是 `E_ABORT` 或 0xc00cee92。消息加进 `msxml.mc`（6.0 的英文原文取自微软文档的
max-xml-size 示例）。新测试（msxml3 测 3.0、msxml6 测 6.0）在 Windows 与 Wine 上都通过。

**libxml2 自己的限制（`010cc274`）。** 量上限时发现 libxml2 另有两道限制：元素嵌套到 256 层就报错，文本节点超过一千万字符就截断——而且截断后
文档照常“加载成功”。DOM 只在 MaxElementDepth 大于 256 时才放开（`XML_PARSE_HUGE`），于是 DOMDocument60（默认 256）载入 11 MB 文本只剩
9,999,997 个字符，Windows 上完整；SAX 读取器从不放开，3.0 在 257 层就停，而 MSXML 3 默认允许 5000 层。现在两处都始终放开，只由 MSXML 自己的
属性限制；libxml2 的实体膨胀保护与这个选项无关，仍然有效。两处都有新测试，在 Windows 上通过；去掉修正时 SAX 的 300 层用例失败。

顺带量了 Office 对 MSXML 对象反复 QueryInterface 的两个未公开接口（`{e19c7100-9709-4db7-9373-e7b518b47086}` 一次普查八百多次、
`{c970c32d-9ffd-45e5-bf20-c3cbaab26222}`）：原生 MSXML 也不实现，Wine 日志里的对应 ERR/FIXME 只是噪声。仍然不同的是解析错误的错误码——
libxml2 报的错一律是 `E_FAIL`，Windows 各有代码（如文档不完整时 3.0 是 0xc00ce553、6.0 是 0xc00cee01）；消息语言则随系统界面。

部署后三个应用的回归、Word 普查（42 项，含经 SAX 读取器与写出器写扁平 XML）与 Excel 普查（80 项，含动态数组工作簿的往返）全部通过。

## PowerPoint 功能普查：3D 模型、带特效形状的导出与 WMF（wine-src `91a75d97`…`94ff49cf`）

`tools/officeautomationprobe/powerpoint-sweep.vbs` 仿照 Word、Excel 的普查逐项跑 PowerPoint（见其 README）。第一次跑到“插入 3D 模型”
PowerPoint 就崩溃了，之后各步都是 462。

- **3D 模型：桌面没有 DACL（`bfd4e06c`，另移植上游 `253b2d25e1`）。** 崩溃点是 ntdll 的 `RtlQueryInformationAcl` 读空指针。临时在它和
  `GetAclInformation` 里记下返回地址，爬到 mso.dll：Office 为 3D 导入器建 Chromium 式的沙箱（受限令牌、作业对象、改默认 DACL、设完整性
  级别、模拟），代理用 `GetSecurityInfo(桌面, SE_KERNEL_OBJECT, DACL)` 取线程桌面的 DACL 以加入沙箱令牌的 SID，拿到后不查 NULL 就交给
  `GetAclInformation`。wineserver 建窗口站和桌面时不带安全描述符，DACL“不存在”。`tools/desktopsdprobe` 在 winref 的服务会话与交互会话里
  量了原生的描述符：新建桌面得到窗口站里带 OBJECT_INHERIT 的 ACE（去掉继承标志、通用权限映射为桌面权限），后建的窗口站复制调用进程所在
  窗口站的 DACL，WinSta0 给登录 SID、受限代码、SYSTEM 全部权限，给 Administrators 与两个应用包 SID 较少权限，各一条给自己、一条留给桌面
  继承。服务端照此给出默认描述符，user32 的 `Get/SetUserObjectSecurity` 取上游实现。上游只在客户端传入描述符时才有，NULL 时仍然没有。
- **带映像与棱台的形状导出失败：D2D 设备不共享（`32ea29df`）。** 导出 PNG/JPG、另存为 PNG 报“储存此文件时发生错误”，单项排查定位到同一
  形状同时有映像和棱台。oart.dll 抛 `Art::CommandListNotImplementedException`，d2d 日志是“Pixel shader … was never loaded”：Office 在某个
  DXGI 表面渲染目标的设备上另建上下文、创建效果与输入，再画进之后新建的表面渲染目标。d2d1 测试在 Windows 上量得：同一工厂在同一 DXGI
  设备上的表面渲染目标共用一个设备（与 `CreateDevice` 得到的都不同），全部释放后才换新；跨设备画效果是 `D2DERR_WRONG_RESOURCE_DOMAIN`。
  Wine 每个渲染目标新建一个设备；现在工厂按 DXGI 设备弱引用地共享。导出图里棱台与映像都画出来了。
- **WMF 导出：`GdipEmfToWmfBits`（`94ff49cf`，此前先移植了上游 gdiplus 自 11.0 起的 57 个提交）。** PowerPoint 以
  `GdipEmfToWmfBits(emf, 0, NULL, MM_ANISOTROPIC, 0)` 取大小，桩返回的状态码 6 被当成 6 字节。`tools/emftowmfprobe` 量得原生结果就是
  `GetWinMetaFileBits` 的 GDI 记录，按标志去掉嵌入的 EMF、加可放置头；照此实现后纯 GDI 元文件的输出与 Windows 逐项一致。但 Wine 下导出的
  WMF 只有 130 字节（Windows 13 MB）：Wine 的 GDI+ 录制双格式元文件时只写 EMF+ 记录，不写 GDI 回退记录，PowerPoint 导出的 EMF 里 99 条
  全是 EMF+——不认 EMF+ 的程序看到的是空白。这一处尚未实现。

普查其余各项（表格、图表、SmartArt、艺术字、各种特效、WebP 与 SVG 图片、超链接、备注、批注、平滑切换、动画、节、查找替换、各种格式保存、
密码、重新打开）都通过；SVG 导出在原生 PowerPoint 上同样不支持。三项改动之后 Word（42 项）、Excel（80 项）普查与回归照常通过。

### 双格式元文件的 GDI 记录、windowscodecs 移植与动画 GIF（wine-src `2718e9e6`、`de4daa4b`…`fbdf7e82`）

- **双格式元文件（`2718e9e6`）。** 以 `EmfTypeEmfPlusDual` 录制的元文件按规范要有两套完整记录：认 EMF+ 的程序读 EMF+，不认的（GDI 本身、
  `GetWinMetaFileBits`）读 GDI 记录。Windows 为每次调用写矢量 GDI 记录；这里在录制结束时用 gdiplus 自己的 EMF+ 回放把整张图画进位图，
  以两条 StretchDIBits 追加在最后（先 SRCAND 掩码、后 SRCPAINT 颜色，未画到处保持透明）。EMF+ 读者按规范忽略没有 GetDC 引导的 GDI
  记录，Wine 的回放也是。纯 GDI 回放一个双格式矩形的测试在 Windows（矢量）与 Wine（栅格）下都通过；PowerPoint 导出的 WMF 从 130 字节
  变为 2.9 MB，用 libwmf 独立渲染，SmartArt、艺术字与带特效的形状都在。与 Windows 的差别是回退为栅格、边缘可差一像素。
- **windowscodecs。** 上游自 11.0 起的 21 个提交（像素格式转换补全、GIF 按需解码与 LZW 表满的修正、BMP/DDS 溢出检查等），外加 BMP
  解码器要用的 `include/intsafe.h`（上游 `cc68bd9087`）。上游同期把各模块的 `WIN32_NO_STATUS` 去掉的全局头文件改动（`9e03f3324c`）
  没有移植，windowscodecs 里对应的删除也就不取。新增的像素格式转换器要重新注册 windowscodecs（`regsvr32 windowscodecs.dll`）才能找到。
- **动画 GIF 仍不可用。** 导出时后台失败，PowerPoint 弹出“储存此文件时发生错误”，自动化此后一直被拒（RPC_E_CALL_REJECTED）。原因
  不在媒体基础：`GifEncoder_GetMetadataQueryWriter` 是桩，而再往下，查询写入器的 `SetMetadataByName` 对所有格式都是返回 S_OK 的桩，GIF
  各块（GCE、APE、注释）只有读取器，编码器提交时不写任何扩展——上游 master 同样如此。只接上编码器的查询写入器会让 PowerPoint“成功”
  写出一个没有帧延时与循环的 GIF，属于假成功，所以留待把整条元数据写入链实现之后。原生 PowerPoint 经自动化 `SaveCopyAs` 同样没有写出
  GIF，但之后照常响应。
- **暂未移植。** msado15（48 个提交）、jscript（23 个）、oledb32（3 个）本地与 CrossOver 都没改过，按路径套上后测试不过（msado15 的测试
  需要上游新增的 `msdshape.h`，jscript 两组、oledb32 的类型转换各有失败），需要连同依赖一起移植，已撤回。

## 迁到上游 master 之后：新树 altars-up 的状态（2026-09-29）

新树 `wine-src-up`（分支 `altars-up`）以纯 Wine master 为底（09-29 晚同步到 `abc6aebd55b`），不带 CrossOver 的改动（含用户名 hack），
Office 在新配置文件 `~/.wine-c2r-up` 里重新激活；旧树的 606 个提交逐一核对后按上游现状取舍，此后的修复都按 winref
（Windows 11 build 29671）实测来做。CrossOver 26.3 的差异审计见 `docs/crossover-delta-audit.md`。

**现状。** `scripts/office-regress.sh` 的 Word、Excel、PowerPoint 保存回归，与 `word-embed.vbs`（Word 里就地激活 Excel）
都通过；功能普查 Word 34 项、Excel 72 项全部通过，PowerPoint 45 项通过，余下的 SVG 导出在原生 PowerPoint 上同样
“转换器未安装”，动画 GIF 之后的视频一项因 PowerPoint 仍在后台忙于 GIF 而被拒（旧树已查明：WIC 的元数据写入链是桩，
上游至今只补了帧级查询写入器，`SetMetadataByName` 与 GIF 各块的写入器仍缺）。

**今天的修复（altars-up）。**
- Office 启动卡死：Mesa 共享上下文的竞态（`patches/mesa` 补丁待定是否提交上游），本地以 `88ea33779d2` 关掉 Mesa 的
  线程化上下文规避；dcomp 先建默认设备再锁（`7bd9beccfdd`）。
- dxgi：客户区缺一维时按 8 像素、flip 模型最小化照常呈现（`a32fc1b75c9`，`tools/dxgizeroprobe`）。
- ole32/combase/rpcrt4：LockServer 与 ResizeBorder 的代理存根、代理 QI 返回调用本身的错误、RPC 服务线程句柄竞态
  （`dbbc1723168`、`6b563643516`、`fe9092ea2ef`）。
- vbscript：宿主不是服务提供者时照样给对象站点，Wine 的 cscript 里终于能建 MSXML 6 文档（`9cc85e31966`，
  `tools/scriptsiteprobe`）。
- mlang 的 RFC 1766 表（`d0e66a45375`）；msxml3 的扩展函数、SAX 读取器、解析设置、属性查询、命名空间管理器、
  克隆保留前缀、脚本文本、XSLT 两个开关、MXXMLWriter 写入文档（`4435c78926a`…`a37e3bfb752`，探针 `msxslfuncprobe`、
  `msxmlpropprobe`、`xsltsecprobe`、`xmlprobe`）。其中 domdoc、saxreader 的新测试在 Windows 上 0 失败。
- 旧树诊断代码（`altars-` 追踪、写 /tmp 的纹理转储、热路径上的 getenv）清掉（`eaea18eb825`）。

**实测推翻的旧结论。** 旧树曾让 DOM 保留未声明的命名空间前缀并在插入时绑定——Windows 的 MSXML 3 与 6 对这种片段一律
报 0xc00ce01d（`tools/xmlprobe`），新树不移植；CrossOver 在 `OleCreate` 失败后改用默认处理器——Windows 同样返回
REGDB_E_CLASSNOTREG（`tools/olecreateprobe`），不做。

**仍然不同、已记下的。** MSXML 3 默认的 DTD 校验（validateOnParse）新解析器没有；MSXML 对象不提供类信息
（`TypeName` 给接口名）；d3d9 在空客户区上比 Windows 宽松；UniversalApiContract 报 10（winref 为 20），只在相应 API
实现后再提高。（MSXML 的 `parseError` 行列与原因文本已在 `837741d585d` 补上。）

### HKEY_CLASSES_ROOT 合并用户的类，以及它的代价（`7d0dd7e27c7`、`691f1d81c14`）

- **做了什么。** Windows 的 HKCR 是 `HKCU\Software\Classes` 叠在 `HKLM\Software\Classes` 之上的合并视图，每次调用重新
  判断用哪一侧；经 HKCR 打开的句柄带标记（`(h & 3) == 2`），值与子键先找用户侧，两边都没有的键建在 HKLM，删除先删用户的，
  枚举先列用户的、再列只有机器有的。规格是 advapi32 自带、在 Windows 上测过的 `test_classesroot*`，原先停在第一个
  todo_wine 上，现在全部通过。COM 自己用私有句柄查 HKLM 的类（不受 `RegOverridePredefKey` 影响），所以 combase/ole32
  另外先查用户的类；`tools/olecreateprobe` 注册在 HKCU 的类因此与 Windows 逐行一致（原先全部 REGDB_E_CLASSNOTREG）。
- **第一版的代价。** 每次操作都 `NtQueryKey` 取键名、再按路径把用户侧和机器侧各开一遍，`RegQueryInfoKey` 把机器侧每个子键
  拿到用户侧查一遍：读一个值从 15 µs 到 73 µs，数 CLSID 的子键从 30 µs 到 14.6 ms（`tools/regbench`，本机一次 wineserver
  往返约 15 µs）。Office 回归冷启动三轮成对比较慢了 17～29 秒，分应用计时后落在 Excel 上：它启动时 Click-to-Run 服务会
  跑一遍 `integrator.exe`，后者经 HKCR 带 `KEY_WOW64_32KEY` 打开约两万个键。64 位进程要 32 位视图时，重定向只能在
  kernelbase 做（ntdll 测试表明 Windows 内核对 64 位调用者的 32KEY 也不重定向，wineserver 只替 WoW64 进程做），
  kernelbase 逐级打开并检查每层 `Wow6432Node`，合并前就要 51 万次 `NtOpenKeyEx`、约 18 秒；第一版在用户侧再完整走一遍，
  变成 101 万次、28 秒。
- **现在。** 每个标记句柄的路径与所在侧只查一次、保存到 `RegCloseKey`；机器侧直接用句柄本身；只有“用户侧有没有”每次
  重查（多一次服务器调用）；用户侧的 Wow6432Node 遍历先看路径第一段在不在用户的 Classes 或其 Wow6432Node 下；计数只
  遍历两侧中较小的一侧。回归耗时与合并前一致（旧 75/74/69 秒，新 74/77/75 秒），功能普查 Word 34、Excel 72 项照旧全过。
  HKCR 被 `RegOverridePredefKey` 覆盖时当作普通键（与合并前相同；Windows 上的行为在 `tools/hkcrprobe` 里待测）。
- **测量中的坑。** 用 `cp -al` 做的对比安装在主安装之后按换 inode 方式更新过的 DLL 上会停留在旧版：一次对比里 wined3d
  还是早上的旧版，PowerPoint 在 wined3d 的命令流线程崩溃，看上去像注册表改动引起的。对比前重建硬链接副本、只换要测的
  DLL，并用 `cmp` 列出差异文件核对。

### Office 前缀里 CrossOver 留下的状态（2026-09-29 清理）

`~/.wine-c2r-up` 是从 CrossOver 瓶子迁来的：`HKCU\Environment` 的 TEMP/TMP、`Shell Folders` 里 7 个文件夹缓存、
HKLM 的 `Common Favorites` 仍指向 `C:\users\crossover`，还有 CrossOver 专有的 `Uninstall\CXHTML`（“CrossOver HTML
engine”），Office 的 `UserInfo` 用户名与缩写也还是 crossover。按纯 Wine master 新建前缀的值改回 `C:\users\user`，
删去 CXHTML 与 UserInfo 的两个值（Office 下次启动时自己重建）。剩下 Office 自己的缓存路径（模板与 DTS 图片缓存、
PowerPoint 的个人模板位置、自定义词典）仍指向旧目录，属于应用状态，暂不动。另外前缀的 DllOverrides 里 `mscoree` 被禁用，
Office 附带装的 OfficePLUS 服务（.NET 程序）因此起不来；`riched20`、`msvcp100/120`、`concrt140` 设为 native，也是瓶子带来的。

### Excel 每次启动都让 Click-to-Run“修复”集成（已解决：旧 msxml3 写坏的 App-V 合并清单）

临时打开 Click-to-Run 的详细日志（`HKLM\SOFTWARE\Microsoft\ClickToRun\OverRide` 的 `LogLevel`=3，看完即删）可见：
Excel 启动后问服务 `DetermineIsRepairRequiredEx`，每次都得到 True，服务于是 `TaskIntegrateRepair::DoRepairForApp
{'AppID':'Excel'}`：重新集成虚拟注册表、删建 5 个计划任务、导入 ETW 清单、跑 msiexec 注册 MSI 存根，前后约 20 秒 CPU，
Excel 自动化探针要 20–27 秒。PowerPoint 也一样，Word 不触发。

- **谁在问。** excel.pdb 公开：调用者是 EXCEL.EXE 的 `CheckForC2RRepair()`，过三道前置判断（`Mso20Win32Client!#16665(6)`、
  Excel 内部一个函数、`Mso30Win32Client!#46109()`）后用 `GetProcAddress` 取 C2R64 的 `IsRepairRequiredEx`，以
  `(L"Excel", &result, 0x10)` 询问服务；结果非 0 才修复。Word 的前置判断不通过，从不询问。
- **服务凭什么答 True。** 服务没有公开符号。只给服务进程开注册表跟踪（`WINEDEBUG=-all,officeclicktorun.exe:+reg,...`）：
  决策线程在回答之前唯一的判据是打开 `HKLM\Software\Classes\Excel.Sheet.12\shell\open\command`，不存在即答 True。
  这个键应当由 App-V 集成 Excel 的文件关联时写入，而修复之后它仍不存在。
- **为什么集成不出来。** App-V 用的合并清单 `Microsoft Office\AppXManifest.xml`（以及它在
  `ProgramData\Microsoft\ClickToRun\MachineData\Catalog\Packages\{9AC08E99-…}\{…}` 的副本 `Manifest.xml` 与 App-V 派生的
  `UserManifest.xml`）里，从 `PackageManifests\AppXManifest.90160000-0016-…xml`（Excel）等产品清单合并进来的 2342 个扩展点
  全部写成了没有前缀的 `<Extension>`，落在文档默认的 appx 命名空间；只有 common 清单的 631 个保留 `appv:`。App-V 只认
  appv 命名空间的扩展点，于是 Excel、Word、PowerPoint、Outlook 的文件关联与 COM 类从来没有集成，而修复用的是同一份清单，
  永远修不好。
- **为什么写坏。** Click-to-Run 用 MSXML 把产品清单的扩展点克隆进 common 清单。这个前缀是 9 月 20 日安装时写的，当时
  Wine 的 msxml3 在 `cloneNode` 时丢掉节点前缀（altars-up `e7313aa729c` 已修）。现在的 msxml3 用 DOM 移动或克隆重做同样的
  合并，前缀正确（`tools/manifestmergeprobe`）。Click-to-Run 只在安装、更新、刷新语言时重建合并清单，快速修复与每个应用的
  修复都不重建；`scenario=CULTUREREFRESH` 在 Wine 下停在 `TaskGetUserCulture`（“Unable to get user-preferred UI
  culture”，按用户界面语言与已装文化比对后失败，本意是装语言包，可能触发下载），没有走下去。
- **修复前缀。** `scripts/fix-c2r-merged-manifest.py`：对每个错位的扩展点，按忽略命名空间的结构签名（源里的
  `!(loc.…)` 占位符匹配合并时本地化出的文本）在产品清单里找到它的来源，把它与后代的命名空间还原成来源的（App-V 的
  UserManifest 统一用其 Extensions 的 appv 2014 命名空间）；全部匹配才写，原文件先复制到指定的备份目录，根元素的全部
  命名空间声明保留（`IgnorableNamespaces` 按前缀引用 appv1.1/appv1.2）。修后 Excel 冷启动修复一次即写出
  `Excel.Sheet.12\shell\open\command`，再冷启动服务答 False、不再修复，Excel 探针 27 秒降到 11 秒；再跑一次快速修复
  （`OfficeClickToRun.exe scenario=Repair platform=x64 culture=zh-cn RepairType=QuickRepair DisplayLevel=False`）把 Word、
  PowerPoint 的关联也集成（`.docx` → `Word.Document.12`，`WINWORD.EXE /n "%1" /o "%u"`）。用旧 Wine 装的前缀都要跑一次：
  Wine 修好之后，它当年写坏的持久数据不会自愈。

排查中看到 Excel 启动时约 113 次按原生路径 `\Registry\User\<sid>_Classes\...` 查用户的类（App-V 层合并 Office 虚拟
注册表时两侧都查），全部失败：Windows 上用户的类是单独的 hive `HKEY_USERS\<sid>_Classes`，`HKCU\Software\Classes`
是指向它的链接，Wine 没有这个名字。wineserver 现在在载入 user.reg 后把它建成指向 `Software\Classes` 的易失链接
（altars-up `a3345e01f89`，advapi32 测试 `test_user_classes_hive`；方向与 Windows 相反，所以 `HKCU\Software\Classes`
的内核名仍是 `...\<sid>\Software\Classes`，Windows 上是 `...\<sid>_Classes`）。它与修复循环无关。

### Word 打开 PDF：PDFREFLOW 的两处缺口（altars-up `6fbec143386`、`83989262b3a`）

Word 打开 PDF 时由 `PDFREFLOW.EXE`（本地 COM 服务器，Word 先后启动两次）把 PDF 转成一个 docx 包交回 Word。
普查里导出的 `sweep.pdf`（有图片、图表、SmartArt、超链接、脚注）打开失败，Word 只说 1401“在试图打开文件时遇到错误”。

- **失败链从 Word 自己的遥测里读。** `%LOCALAPPDATA%\Temp\Diagnostics\WINWORD\Primary*.log` 每行一个事件；
  `Office.Word.FileOpen.UserInitiatedOpen` 的 `Activity.Result.Code/Tag` 与 `Data.IntermediateResults`（按时间的
  `{Code, Type, Tag}` 数组）给出整条链。Tag 是 WWLIB.DLL 里紧跟失败调用的立即数，搜 `.text` 得 RVA，用 wwlib 的公开 PDB
  （OMAP，`scripts/pdb-addr2sym.py`）命名：`HrReadMetroFromPistm`（读 OOXML 包）返回 0x80CB9101；这个码只由 Mso30 的
  OPC 加载器（`CContentTypesLoader`、`CRelationshipsLoader` 等的 `startElement`）产生，即包结构不合法。
- **第一处：WIC 的 8bpp Alpha。** PDFREFLOW 要 `GUID_WICPixelFormat8bppAlpha` 的组件信息（Direct2D 的纯 alpha 位图、
  PDF 软蒙版），Wine 没注册，`CreateComponentInfo` 失败后它抛 `CException`。按 Windows 的数据注册（8 位、1 通道、
  掩码 0xff、无符号整数、支持透明；测试 `test_pixelformat_info_8bppAlpha`，待 winref 实测）。
- **第二处：opcservices 丢了 TargetMode。** PDFREFLOW 用系统的 OPC API（`IOpcFactory`、
  `IOpcRelationshipSet::CreateRelationship(..., OPC_URI_TARGET_MODE_EXTERNAL)`）生成 docx。Wine 的
  `opc_relationship_create` 从不保存目标模式（`GetTargetMode` 永远是内部），写关系部件时也从不写 `TargetMode`，
  超链接 `https://example.com/` 因而成了非法的内部部件名。对照：只差 `TargetMode="External"` 的两个手工 docx，
  Word 只拒前者（“文件可能已经损坏”）。修后这份 PDF 8 秒打开（24 段、9 个形状，图片、超链接、脚注尾注、文本框都在），
  `word-sweep.vbs` 加上“重新打开导出的 PDF”一步后 Word 普查全部通过。
- **opcservices 补齐（`e417eb13825`、`a4ada818b49`）。** 写包时“不压缩”的部件原来先过 deflate 0 级再标成 stored，条目比
  部件长 5 字节、任何读取方都当损坏（PDFREFLOW 写图片正用这一档），ZIP64 的几处记录也写错了；现在照 ZIP 规范写。
  `ReadPackageFromStream` 原是桩，现在读 ZIP32/ZIP64、stored 与 deflate（校验长度和 CRC）、内容类型（Override 先于
  Default，不分大小写）、各部件的压缩档位和全部关系（含 TargetMode）；删部件与关系、按类型枚举、关系部件内容流、
  流的其余方法、部件 URI 的比较、相对化与规范化也都补上，原有测试的 todo_wine 全部通过（871 项 0 失败）。
  这部分由 Claudex 按任务卡施工，审查后改了三处（同一部件的相对引用应为空，它给出 `../`；文件流 `Stat` 的
  `grfMode` 不凭空定为只读；恢复一处注释）。非法包的错误码、LockRegion、CopyTo 到尾、内容流的 grfMode 等未经
  Windows 实测，`tools/opcprobe` 与新测试都在 winref 批次里。
- **排查中的弯路。** Mso30 自己也含 “TargetMode”，先怀疑是它的写入器；opcservices 的调试通道叫 `msopc`，
  用 `+opcservices` 什么也看不到。

**用遥测找隐藏故障。** 同样的日志按“事件名 + 结果码 + 标签”汇总所有 `Activity.Success=false` 的事件（只取这些字段，
日志里有账户相关内容，不外传），能一次列出三个应用里所有悄悄失败的活动。今天这样找到了 Excel 每次启动约 34 次的
`Office.AirSpace.Backend.CompositionErrorActivity`（0x88980801，DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED）：AirSpace
的渲染线程对一个表面 BeginDraw、SuspendDraw、再对同一表面 BeginDraw，UI 线程稍后才 EndDraw；Wine 拒绝挂起之后的
BeginDraw，与 DirectComposition 文档一致，Windows 为何不报错待 `tools/drawsurfprobe` 在 winref 上实测。

**测试显示的教训。** 本机已没有 X 显示 :78；winex11 连不上时 explorer 改选 winewayland，而 `WAYLAND_DISPLAY` 未设时
libwayland 连 `wayland-0`——用户真实的 GNOME 会话。:2（无头 mutter 的 Xwayland）也要它自己 `-auth` 文件里的 cookie，
`scripts/office-debug.sh` 会设，直接运行时要自己设 `XAUTHORITY`，并设 `WAYLAND_DISPLAY=wayland-1` 兜底。在 Wayland 驱动下
Excel 启动后死锁（一个线程持加载器锁空转），X11 下正常，待查。

### VBA 宏，以及读过 VBProject 的 Excel 不退出（altars-up `53a55969585`）

`tools/officeautomationprobe/office-vba.vbs` 在 Word、Excel、PowerPoint 里经 VBA 工程对象模型加模块并用
`Application.Run` 运行：函数、`Format`、`CreateObject`、`On Error`、`Declare PtrSafe` 调 kernel32、改写正文的宏、
Excel 单元格里的自定义函数。三个应用 33 项全部通过（AMSI 经 clamd 扫描，宏照常运行）。

同时发现：自动化启动的 Excel 只要读过一次 `book.VBProject`，`Quit` 并放掉全部引用之后就永远不退出。二分到最小
触发之后开 `+ole`：cscript 的 `RemRelease` 都发了，Excel 存根的外部引用也归零了，主线程空闲在消息循环里；默认的 err
通道里每次都有 `stub_manager_delete Got page fault when releasing stub!`，发生在 RPC 线程上。原因：跨进程调用 STA
对象时，`dispatch_rpc` 持有存根管理器与存根缓冲区，等 STA 线程执行完调用、发出信号之后才在 RPC 线程上释放；Excel 读
`VBProject` 时恰好紧接着对 VBA 工程的一个对象 `CoLockObjectExternal` 解锁并 `CoDisconnectObject`，COM 最后的几次
`Release` 于是晚到、而且跑在对象套间之外，碰上已不存在的对象。现在由 STA 线程在调用执行完、通知 RPC 线程之前自己
释放。ole32 新测试 `test_disconnect_after_call` 用一个子进程里的 STA 服务器（对象在方法里断开自己）记录每次 `Release`
所在的线程：旧代码稳定地有 3 次在别的线程上，修后为 0。修后 Excel 在 `Quit` 后 3 秒退出。

### icu.dll：C++ 时区数据库与 Office 的 React Native 引擎（altars-up `6ef87791215`）

新树 Word 启动时的 FIXME 普查里有 `__std_tzdb_get_current_zone returning Windows time zone name`。追下去：Office 自带的
`msvcp140_atomic_wait.dll`（微软 STL 原版）、React Native 的 JS 引擎 `hermes.dll`、`react-native-win32.dll`、
`WritingAssistant.exe`、WinAppSDK 的 `Microsoft.Windows.Search.dll` 都会从 system32 加载 `icu.dll`（Windows 10 起系统自带
的 ICU），拿时区、日期与数字格式化（JS 的 `Intl`）；Outlook 的 `EMSMDB32.DLL` 还直接导入 `__std_tzdb_get_sys_info`。
其中 `react-native-win32.dll` 与 `WritingAssistant.exe`（Word 的“编辑器”拼写语法服务）是**延迟导入** `icu.dll` 的分词函数
（`ubrk_open/following/preceding/getRuleStatus`、`u_isalnum`、`u_strFromWCS`）：按词移动光标、双击选词、分句的第一次
调用就会因加载不了 `icu.dll` 抛出延迟加载异常。
而 Wine 这边：

- **构建里根本没有 `icu.dll`。** 上游已经带了用 ICU 源码编成 PE 的 `icu.dll`，但它（连同自带的 libc++、dmsynth、
  fluidsynth）要 C++17 的 PE 交叉编译器；本机只装了 mingw 的 gcc、没有 g++（缺 `cc1plus`），configure 于是**静默**把
  它们放进 `DISABLED_SUBDIRS`。不动系统的做法：`apt download g++-mingw-w64-{x86-64,i686}-win32`（与已装的
  gcc-mingw-w64 同版本 13.2.0），`dpkg -x` 到 `~/.local/opt/mingw-w64-gxx-13-win32`，configure 时给
  `x86_64_CXX="x86_64-w64-mingw32-gcc -B…/13-win32/"`、`i386_CXX=…`（C++17 检测体不引用标准头，Wine 编 C++ 用自带的
  libc++ 8.0.1 头文件，所以只缺 cc1plus）。`scripts/build-wine.sh` 现在自己找这份编译器，找不到会提示。更干净的做法是
  系统里装 `g++-mingw-w64`，之后普通的 configure 就会带上 C++。
- **Wine 内置的 `msvcp140_atomic_wait` 给的是 Windows 时区名，`__std_tzdb_get_sys_info` 是桩。** Wine 的这些运行库没有
  标 prefer-native，Office 自带的那份并不会被加载，所以内置版必须做对。现在它照 Windows 从 `icu.dll` 取：按序的 IANA
  名与链接、ICU 数据版本、ICU 的默认时区，以及某时区某时刻的偏移、夏令时量、前后两次切换（一侧没有切换时为
  ±DBL_MAX）与短名（转不成代码页时退成 `+HH`/`+HHMM`）；名字后那个字节选“只要偏移”或“偏移加范围”；闰秒读
  `HKLM\SYSTEM\CurrentControlSet\Control\LeapSecondInformation`。没有 `icu.dll` 时名字照旧给 Windows 的，换算返回“缺库”
  错误而不是让进程中止。

验证：`tools/tzdbprobe` 在同一个 Wine 的 `icu.dll` 上，用 Wine 的实现与用 Office 自带的微软原版逐行相同（634 个时区、
157 个链接、各时区的偏移、切换与短名、选项字节）；`msvcp140_atomic_wait` 测试 748 项 0 失败；装上 `icu.dll` 后 Word、
Excel、PowerPoint 保存回归照常通过，Word 运行近三分钟（加载了新的 `msvcp140_atomic_wait`、`icu.dll` 与
`react-native-win32.dll`）没有新的错误，启动 FIXME 从 213 条降到 199 条。Windows 上的对照（`tzdbprobe`、`msvcp140_atomic_wait` 测试）待 winref。

### 新树上的启动普查：还剩什么（2026-09-29 晚）

在同步到上游 `abc6aebd55b`、装上 `icu.dll` 的构建上，Word、Excel、PowerPoint 各在 :2 上启动并运行两三分钟：没有崩溃，
ERR 只剩已知的三类（C2R 服务故意加载不了的 `msoxmlmf.dll`、这个前缀禁用 `mscoree` 导致起不来的 OfficePLUS 服务、
WinUIEdit 找的一个未注册类，winref 上查注册表待定）。FIXME 按条数排，多数是半桩或记录用途（WER、ETW 会话、
netprofm 的网络变化通知、webservices 的 error 参数），有实际影响、待 Windows 实测后补的：

- `BitmapScaler_Initialize unsupported mode 2`：WIC 缩放器只会最近邻，Excel、PowerPoint 每次启动用三次插值缩放几次
  （`tools/wicscalerprobe`）。
- `OaBuildVersion` 不认识 Windows 10（`tools/oaversionprobe`）；`RegQueryInfoKeyW` 不给安全描述符长度（`tools/regsecprobe`）。

再用 `WINEDEBUG=warn+module` 看 Office **按名字找而找不到**的导出（这类缺口不打 FIXME，Office 默默走退路）：
user32 `InheritWindowMonitor`（OART、写作助手）、userenv `DeriveAppContainerSidFromAppContainerName` 与
`GetAppContainerFolderPath`（加载项框架 OSF 与 Web 沙箱）、ntdll `NtQueryDirectoryFileEx`（App-V 层探测后挂钩），以及加载
不了的 `isolatedwindowsenvironmentutils.dll`（msoadfsb）。`tools/win10apiprobe` 量 Windows 的回答，之后再定补不补——
只补 AppContainer 的配置文件函数而没有 AppContainer 令牌，可能把 Office 从现在能用的退路引到走不通的路上。

**C2R 服务的上万次 RPC 不是空转。** 每个应用运行时服务打印约一万条 `FSCTL_PIPE_IMPERSONATE`。追下去：几乎全是
`\\.\pipe\lrpc\AppV-ISV-…APPV-VREG_SERVER` 上同一个过程；按公开符号它是 `vreg_server_IsDuplicatedKey`，唯一调用者是
`vreg_hooks::LookupKey`——App-V 的注册表钩子每查找一个虚拟化的键就问一次服务。Windows 上同样如此（走 ALPC）；Wine 的
ncalrpc 是经 wineserver 的命名管道，每次往返更贵，一万次大约多出一两秒启动时间。

### WinINet：`max-age` 少算了十倍（altars-up `e67210564e9`）

`Cache-Control: max-age=N` 被按 `N × 10⁶` 个 100 纳秒记为过期时间（注释写的是秒），一小时的缓存只算 6 分钟。
`tools/wininetexpprobe` 顺带看出另两处与 Windows 可能不同、待 winref 实测：`Expires: -1` 等无效日期（RFC 7234 要求当作
已过期）落到默认的十分钟有效期上；普通请求从不读缓存，连 2038 年才过期的条目也再去服务器。

### 按上游测试补 COM 与 OLE 嵌入：CurVer、EMF 呈现、嵌入对象的几处（altars-up `2198761ed5f`…`73b8a3cb0e7`）

上游测试里的 `todo_wine` 是 Windows 上测过、Wine 还没做到的行为，不需要 winref 就能动手。挑与 Office 直接相关的：

- **ProgID 的 `CurVer`**（`2198761ed5f`）：版本无关的 ProgID 常常自己没有 `CLSID`，只有 `CurVer` 指向当前版本；Windows 顺着
  链找到有类的那一个，Wine 一律 `CO_E_CLASSSTRING`。Office 前缀里 331 个带 `CurVer` 的 ProgID 有 97 个只能这样解析，其中有
  VBA 窗体按名字建的 MSCOMCTL 控件（`MSComctlLib.ListViewCtrl`、`ImageListCtrl`、`ProgCtrl`、`SbarCtrl`）。成环为
  `REGDB_E_INVALIDVALUE`（`CLSIDFromProgID` 报 `CO_E_CLASSSTRING` 并清零），`CLSIDFromString` 对“已注册但解析不了”的
  ProgID 不动输出。compobj 测试去掉 11 个 todo。
- **数据缓存画 EMF**（`eb2f1404eb6`）：缓存加载得了增强图元文件呈现（OlePres 流里存的是 WMF，加载时转回 EMF），
  `IViewObject::Draw` 却只会画图元文件图片和 DIB，未运行的嵌入对象（容器显示它最后的图）一片空白。
- **`Freeze`/`Unfreeze`**（`b7d98406600`）：原是 `E_NOTIMPL`；冻结的方面在运行中对象数据变化时保持原样。
- **嵌入辅助对象的引用泄漏**（`b904190ada5`）：进程内服务器对象没有 `IDataObject` 时不算运行，但处理器留着另两个
  引用，销毁时只撤销“运行”，引用就漏了——嵌入的服务器因此可能退不出去。
- **默认处理器的 `Update` 与 `GetUserType`**（`582bf139289`）：未运行时 `Update` 先运行对象（未注册类得
  `REGDB_E_CLASSNOTREG`）；类未注册时（文档里嵌了本机没装的程序的对象）`GetUserType` 退到存储记下的类型名，再退到
  “Unknown”。中文 Windows 上这个字符串是否翻译，第七批的 ole2 测试会告诉我们。
- **无格式的 `GetData`**（`73b8a3cb0e7`）：视图缓存已填上某种格式后，再按“无格式”取数据答 `DV_E_CLIPFORMAT`。

ole2 测试 3007 项 0 失败，todo 从 32 降到 22；Office 保存回归照常通过。

### 加载器锁与 CET：AuxUlib 的 `PrivIsDllSynchronizationHeld`，以及 exe 的 TLS 回调在退出时（altars-up `8907ce29605`、altars-up `7b2d832a374`）

启动普查里 Word 找了 31 次、PowerPoint 13 次 kernelbase 没有的 `PrivIsDllSynchronizationHeld`。来源是 16 个 Office 二进制
静态链接的 AuxUlib（`aux_ulib.lib`）；反汇编最小的 `SDXHelper.exe` 看出：它 `GetModuleHandleW(L"api-ms-win-core-libraryloader-l1-1-0.dll")`
后按名查找，按 `BOOL (WINAPI *)(BOOL *held)` 调用；查不到就自己比较 `PEB->LoaderLock`（PEB+0x110）的 `OwningThread` 与当前
线程号。补上的实现答的就是这个（`RtlIsCriticalSectionLockedByThread`），`tools/dllsyncprobe` 在 TLS 回调、DllMain、DLL 通知
回调、主线程持锁与否、他线程等状态下逐项对照：与 AuxUlib 的后备检查完全一致，所以 Office 的行为不变，只是每次启动少了
这些查找失败。kernel32 的 loader 测试加了主线程、嵌套持锁、他线程与 DllMain 里的检查；Windows 的回答（包括 Windows 8
以后加载器的 LoadOwner 标志会不会让它在别的情形答“持有”）在第七批里测。

同一个探针顺带看出一个真缺口：**Wine 在进程退出时不以 `DLL_PROCESS_DETACH` 调用 exe 自己的 TLS 回调。** 上游 2020 年给
exe 补了进程附加、线程附加与分离时的调用（bug 48971），唯独没有进程分离；`LdrShutdownProcess` 只遍历 DLL 的初始化顺序表。
MSVC 运行库的 `__dyn_tls_dtor` 靠这次调用在退出时析构主线程的 `thread_local` 对象，mingw 的 `__mingw_TLScallback` 同理。
退出时 exe 的 TLS 回调与各 DLL 的 `DllMain` 谁先谁后、回调收到的 `reserved` 是什么，公开资料说不清，等 winref 的结果再改 ntdll。

**CET。** GFX.DLL 与 MSO.DLL（后者挨着受保护视图 LPAC 沙箱的配置名）经 kernel32 查 `IsUserCetAvailableInEnvironment`。
Wine 不维护影子栈，进程与飞地都如实答 FALSE（altars-up `7b2d832a374`）；非法环境值时 Windows 设不设最后错误，由 `tools/win10apiprobe` 测。

**探针自己的一个坑。** `printf("...", f(), GetLastError())` 的实参求值顺序是未定义的，mingw GCC 在 x86-64 上**先读
`GetLastError()` 和输出缓冲区的字段，再调用 `f`**：win10apiprobe 因此把 `GetProcessMitigationPolicy` 显示成“返回 1 却没写
缓冲区”。用脚本扫了全部探针（按括号配对，跨行也算）：win10apiprobe 的 `InheritWindowMonitor` 六行、wsprobe 一行都有这个
问题，都还没在 Windows 上跑过；uilangfilter 一行的已编译代码恰好先读了 TEB 的 `LastErrorValue`，它的 Windows 结果仍然有效。
现在一律先把错误码存进变量再打印。

### 加载时没解析上的导入：msvcp 的货币 facet、WinAppSDK 要的等待完成包与功能分级（altars-up `3e9f06e9cfd`、`5f352215387`…`bedde6b091f`、`e3ebc193e0b`、`41ed67b1664`）

`WINEDEBUG=warn+module` 除了按名查找失败，还会打印 **加载时就没解析上的导入**（`import_dll No implementation for …`，
loader 把它们指向一个一调用就中止进程的桩）。Word 启动时有三组：

- **`mso30win32client.dll` 从 msvcp140 导入 `moneypunct<wchar_t,false/true>::id` 与 `money_put<wchar_t>::id`**（`3e9f06e9cfd`）。
  Wine 的 msvcp 从没实现货币类 facet，spec 里这 18 个数据导出一直注释着。MSVC 的 STL 头文件让客户程序把 facet 代码内联
  进自己，只从库里取 `id`：缺了导出，`use_facet()` 会把桩地址上的代码字节当成 facet 编号，越界或写到只读页。补上
  `money_get`/`money_put`/`moneypunct`（char、wchar_t、unsigned short 三种）的 `id`、`moneypunct::intl` 常量，以及同样缺着的
  `time_get<unsigned short>::id`，msvcp60 到 msvcp140、msvcp120_app、msvcp_win 一起（`make_specfiles` 同步）。msvcp140 测试加了
  导出存在、互不相同、`intl` 取值的检查。
- **`WinAppSDK\CoreMessagingXP.dll` 从 ntdll 导入 `NtCreate/Associate/CancelWaitCompletionPacket`**：Windows 8 起的
  “等待完成包”，目标对象一变为有信号就往完成端口投一个完成包，线程池和 WinUI 的消息调度用它。上游有 CodeWeavers 的草稿
  MR 6911（为 React Native 做，2026-08 关闭未合并，CrossOver 26.3 也没有），连同 4 个共 872 行的 Windows 实测测试移植过来
  （`5f352215387`…`d4275307fa0`）。MR 基于 2025 年 10 月的上游，此后 wineserver 的对象模型改成指定初始化的操作表、
  `object_params` 与 `init` 钩子，另起一个提交改写（`bedde6b091f`：同步对象一律经 `get_obj_sync()` 取，没有 `signaled` 的
  同步对象当作无信号，唤醒时用安全迭代器）；MR 没带生成文件，`make_requests`/`make_specfiles` 重新生成（`3df7d54402f`，
  中间几个提交单独编不过）。ntdll 的 file 测试 3334 项 0 失败，其中等待完成包 284 项；om 测试 0 失败；Office 保存回归通过。
  互斥体、键控事件、完成端口作为目标会被拒（`STATUS_INVALID_PARAMETER_3`，MR 的测试按 Windows 这样测），所以不会走到
  互斥体回调里要等待线程的那条路。
- **CoreMessagingXP 还导入 `NtAlpcQueryInformation` 和 shcore 的 `UnsubscribeFeatureStateChangeNotification`。** 前者补成与
  上游其他 ALPC 桩一样返回 `STATUS_NOT_IMPLEMENTED` 的系统调用（`41ed67b1664`）。后者属于功能分级（feature staging）那组
  函数，WIL 的功能开关全靠它们：shcore 原来的订阅是连输出句柄都不写的空桩，退订、`GetFeatureVariant`、`RecordFeatureError`
  都没有（`e3ebc193e0b`）。Wine 里没有功能会改变状态，所以订阅只需交回一个之后能释放的句柄，回调永不触发。
  `tools/featurestagingprobe` 量 Windows 的回答。

同一次普查确认上一节补的 `PrivIsDllSynchronizationHeld` 已不再出现；还剩 `InheritWindowMonitor`、
`isolatedwindowsenvironmentutils.dll`、`mscoree`（前缀禁用 .NET），以及 Office 自己组件之间的版本差（`c2r64.dll` 缺
`SaveRecording` 等、`OutlookServicing.dll` 缺序号 89），后两类 Windows 上也一样。

### 命名管道服务器模拟的是客户端（altars-up `d996b6e3bc9`、`731d6d71d81`）

C2R 服务每查一次虚拟注册表就 `RpcImpersonateClient` 一次（Word 启动约 1600 次），Wine 的 ncalrpc 走命名管道，
`ImpersonateNamedPipeClient` 一直给服务器**它自己的进程令牌**（ntdll 打一条 “impersonating self” 的 FIXME）。
kernel32 的管道测试把 Windows（NPFS）的语义写得很清楚，按它实现：

- 客户端的安全服务质量（SQOS）随 `open_file_object` 请求带进服务器（kernelbase 早已把 `SECURITY_SQOS_PRESENT` 等
  转成 `OBJECT_ATTRIBUTES.SecurityQualityOfService`，只是没人往下传）。
- 服务器端保存客户端的安全上下文：静态跟踪在连接时复制一份；动态跟踪让客户端写入的每条消息带上写入线程当时的
  有效令牌，服务器读到哪条就换成哪条的。没有指定 SQOS 的客户端按动态跟踪、只算已启用的特权（effective-only）。
- 模拟时按客户端允许的级别复制，effective-only 时去掉被禁用的特权；还没有客户端、或服务器已断开它时返回
  `STATUS_CANNOT_IMPERSONATE`；客户端关掉之后服务器仍可按最后的上下文模拟（测试要求）。

顺带发现：令牌没有任何特权时，`NtQueryInformationToken(TokenPrivileges)` 在零长度缓冲区上报成功（计数本身要 4 字节），
一到三字节的缓冲区还会越界写计数，一并修正。管道测试 38464 项 0 失败，7 个模拟相关的 todo 通过；Word、Excel、
PowerPoint 启动与保存回归照常，错误日志没有新增。组（group）在 effective-only 下是否也去掉禁用项，测试没有覆盖，暂不动。
rpcrt4 在绑定的 QOS 为 `RPC_C_IMP_LEVEL_DEFAULT` 时会请求匿名级（原有的 FIXME）；Wine 的访问检查不看模拟级别，所以
暂不影响，要按级别强制时得先改这里。

### 文件属性：覆盖时取新属性、设时间不再清掉隐藏、只读文件能改属性（altars-up `170955e4ab0`、`35f14cf9910`、`55fe4631a90`）

按上游测试里 Windows 实测的 todo 补了一串文件属性的缺口，其中一处会让用户看得见：

- **`SetFileTime` 会让隐藏文件现形。** `FILE_BASIC_INFORMATION.FileAttributes` 为 0 表示“不改属性”，`SetFileTime` 正是这样
  发的；Wine 却照样拿 0 去设属性，清掉隐藏、系统位，还给只读文件加回写权限（本地实测：隐藏文件设了时间变成 0x20）。
  现在 0 不动属性；设基本信息也要求句柄有 `FILE_WRITE_ATTRIBUTES`（原先完全不查：fd 缓存只存了读写数据三位，现把类型字段
  用不到的一位拿来存它）。
- **只读文件打不开写属性权限。** 服务器把 `FILE_WRITE_ATTRIBUTES` 算作要 Unix 写权限，以写模式打开只读文件就 EACCES；
  而 `fchmod`/`futimens` 用只读描述符就够。现在只有截断（覆盖）才要写模式，覆盖只读文件仍按 Windows 被拒；
  `SetFileAttributesW` 也照 Windows 以 `FILE_WRITE_ATTRIBUTES` 打开（原先只要 `SYNCHRONIZE`，靠的正是不查权限）。
- **覆盖、替换已存在的文件取新属性**（`FILE_OVERWRITE(_IF)` 带只读、替换只读文件等，`test_NtCreateFile` 的表）。
- `CreateFile(TRUNCATE_EXISTING)` 必须带 `GENERIC_WRITE` 位，否则 `ERROR_INVALID_PARAMETER`。
- **删除用 POSIX 语义**（altars-up `88ce1d33585`）：Windows 10 1809 起 `DeleteFileW`/`RemoveDirectoryW` 设
  `FileDispositionInformationEx(DELETE|POSIX_SEMANTICS)`，名字立即消失、仍打开的句柄照用，文件系统不支持时退回旧方式；
  Wine 原先要等最后一个句柄关掉，其间还能再打开。服务器已支持 POSIX 语义，改 kernelbase 即可；对字符设备（`nul`）设删除
  信息按设备驱动的回答给 `STATUS_INVALID_DEVICE_REQUEST`。ntdll 文件测试又去掉 9 个 todo；三件套功能普查（Word 42、
  Excel 79、PowerPoint 55 项）前后结果一致。

kernel32 文件测试 57 万项 0 失败、todo 从 243 降到 218；ntdll 的 file/directory/info 测试 0 失败，另加了“只设时间不动属性”
“无写属性权限被拒”“只读文件可写属性打开”三组断言（随第七批在 Windows 上验证）；Office 保存回归照常。覆盖一个隐藏或
系统文件而不带同样属性时 Windows 拒绝——`CreateFile` 文档与 [MS-FSA] 2.1.5.1.2 都这么写，已照做（altars-up `97d0f78ea33`，
只看文件存的属性，点文件不算；Office 保存回归与三件套普查照常）；Wine 自己的 profile 写入原先用 `CREATE_ALWAYS` 覆盖整个
文件，会冲掉 desktop.ini 的隐藏、系统属性，现改为就地写再截断（`e40b7d311c0`）。`tools/overwriteattrprobe` 在 winref 上复核，
并量 profile 写入、`CopyFile`、替换式移动遇到隐藏目标时 Windows 怎么做。
设为 `FILE_ATTRIBUTE_NORMAL` 后 Windows 查询返回 0x80（Wine 返回 ARCHIVE）需要记录存档位，暂未做。

### 设置文件信息：状态块、缓冲区检查、分配大小、改名、删除标记（altars-up `de83556c28c`…`7456896871e`）

依据是上游测试里 Windows 实测的 todo 和 [MS-FSA] 2.1.5.15（Windows 文件系统行为的公开规范，各信息类的检查顺序、返回码
都写明了，附录 A 注明 NTFS 与 FAT 的差别）：

- **失败的请求不写状态块。** `NtSetInformationFile` 以错误状态失败时调用方的 `IO_STATUS_BLOCK` 保持原样（测试检查
  `io.Status` 仍是 0xdeadbeef，16 处 todo 转为通过）；wow64 转接自己判定的错误原先从一个没初始化的 64 位块拷回，32 位
  调用方拿到的 Information 是个栈地址。`tools/setinfoprobe`（64、32 位两个版本）把同样的问题问到测试没覆盖的类。
- **缓冲区过短是 `STATUS_INFO_LENGTH_MISMATCH`**（Wine 原为 `STATUS_INVALID_PARAMETER_3`；邮槽超时原先不查长度）。
  改名的新名字长度为 0、奇数或超出缓冲区是 `STATUS_INVALID_PARAMETER`，链接的名字也要在缓冲区内——两者原先都会读过缓冲区
  末尾；wow64 按 `FileName[len/2]` 分配 64 位缓冲区，奇数长度时拷贝多写一个字节。
- **实现 `FileAllocationInformation`**（原先 `STATUS_NOT_IMPLEMENTED`，`SetFileInformationByHandle(FileAllocationInfo)` 是
  `ERROR_CALL_NOT_IMPLEMENTED`）：按簇（Wine 报 4096）向上取整；大于文件长度用 `fallocate(FALLOC_FL_KEEP_SIZE)` 预留，
  不动文件长度，文件系统不支持时只当提示；小于文件长度时截断到对齐后的值（NTFS 如此，FAT 截断到原值）。设分配大小和
  文件长度都要 `FILE_WRITE_DATA`、对目录是 `STATUS_INVALID_PARAMETER`——服务器原先对设文件长度不查权限，只读句柄靠
  `ftruncate` 失败才被拒，还报成 `STATUS_INVALID_HANDLE`。缩小已有预留（仍不低于文件长度）时 Windows 会收回到对齐值，
  Wine 保留预留：Linux 上释放预留会改掉修改时间，而 Windows 只更新 ChangeTime。
- `SetFileInformationByHandle` 对只能查询的类一律 `ERROR_INVALID_PARAMETER`（原先十一个类走 FIXME、`ERROR_CALL_NOT_IMPLEMENTED`，
  另四个已是 `ERROR_INVALID_PARAMETER` 且有测试）。
- **改名后同一文件的其他句柄跟着新名字。** Windows 上名字属于文件，另一句柄查 `FileNameInformation`、
  `GetFinalPathNameByHandle` 得到新名字（两处测试）。服务器原先只改发起改名的 fd，其他 fd 仍记旧路径——它们关闭时的
  删除（delete on close、POSIX 删除）也会去 unlink 旧路径，若其间有人在旧路径建了新文件，删掉的就是那个。现在按旧名字
  打开的其他 fd 和挂在该名字上的待删除记录一并改到新名字；经另一个硬链接打开的保持原名。
- **目录下有打开的文件时拒绝改名目录**（`STATUS_ACCESS_DENIED`，[MS-FSA] 2.1.4.2，任意深度；NTFS 还会先打断机会锁再
  重试，Wine 没有机会锁）。服务器遍历所有打开的 fd 按路径前缀判断；设备哈希表的桶是用到才初始化的，遍历要跳过没初始化
  的桶（第一次没跳，wineserver 在 `next == NULL` 上崩溃）。

- **删除标记属于名字**（altars-up `43302900126`，[MS-FSA] 2.1.5.15.3 的 `Open.Link.IsDeleted`）：经一个句柄设删除，同名打开的
  所有句柄都算标记删除；经另一个句柄取消，文件就保留——Wine 原先把标记放在各自的 fd 上，谁设的谁关闭时照删，别的句柄
  取消不了（两组测试，文件最后应还在）。取消也会收回“删除时关闭”的句柄关闭后留下的待删除；“立即移除名字”（POSIX 语义）
  仍只在设它的那个句柄关闭时发生（测试：另一个句柄先关，文件还在）。已标记删除的名字不能再打开（`STATUS_DELETE_PENDING`，
  `CreateFile` 得 `ERROR_ACCESS_DENIED`）、不能改名或建链接。`FileStandardInformation.DeletePending` 在文件已无名字时（POSIX 删除
  之后，`st_nlink == 0`）为真；只是标记了、名字还在的情形仍报假：每次查询都要问服务器，实测本地查询 2.6 µs、一次服务器往返
  10.3 µs，`GetFileSizeEx` 等都走这个类，不值得。
- 以挂载点句柄为根、空名、不带 `FILE_OPEN_REPARSE_POINT` 打开，Windows 返回 `STATUS_IO_REPARSE_DATA_INVALID`（`40a6b38ac6f`）；
  Wine 原先打开了挂载点本身，测试接着把它标记删除——以前关闭后连挂载点目录都删掉了。
- 命名管道的 `DeletePending` 按 NPFS 恒为 1（`7456896871e`）。

ntdll file/directory/pipe、kernel32 file/pipe 测试 0 失败（ntdll 文件测试 todo 71→49、管道 35→25，kernel32 218→214），新加分配大小、
只读句柄设长度、目录、改名名字长度的断言，随下一批在 Windows 上验证；Office 保存回归照常，三件套功能普查（Word 42、Excel 79、
PowerPoint 55 项）前后一致。查询失败时 Windows 是否也不写
状态块、不能设置的类的确切返回码（Wine 现为 `STATUS_NOT_IMPLEMENTED`）、`FileCaseSensitiveInformation` 的设置、
`FileModeInformation` 的设置（Wine 都未实现）等 `setinfoprobe` 在 winref 上的结果再定。

### Office 启动时撞到的几处 API（altars-up `304db160e6c`、`1b717d6b839`、`cf340eae246`）

用默认调试通道起 Word、Excel、PowerPoint 各输入一次再关闭，统计它们实际撞到的 FIXME，挑出能凭测试或文档定下来的：

- **propsys 转字符串**：Excel、PowerPoint 启动时调 `VariantToString`，Wine 只认 BSTR 和 I4。测试（Windows 实测）定下了其余
  写法——布尔按数值（TRUE 为 "1"）、浮点照写、FILETIME 与 DATE 写作 `yyyy/mm/dd:hh:mm:ss.mmm`（DATE 只到秒）、向量元素间用
  "; "、空 VARIANT 为空串——`PropVariantToStringAlloc` 与 `VariantToString` 改为共用一个逐值格式化。Office 实际传的是字节
  SAFEARRAY，Windows 怎么写没有依据，仍返回 `E_NOTIMPL`，由 `tools/propvarstrprobe` 在 winref 上量。
- **`GetFinalPathNameByHandle` 的规范化名字**：默认的 FILE_NAME_NORMALIZED 应给出磁盘上的大小写（打开 "test.dat"、磁盘上是
  "Test.Dat" 时返回后者，两处测试），Wine 两种标志都给打开时的写法。`FileNormalizedNameInformation` 改为取服务器存的
  realpath，最终路径的卷之后部分用它；FILE_NAME_OPENED 本来就对，去掉那条 FIXME。
- **`CopyFileEx`/`CopyFile2` 的进度回调与取消**：原先一律忽略。测试给出取消的语义（`ERROR_REQUEST_ABORTED`、能删时删掉副本——
  别的句柄不共享删除时副本留下，可见 Windows 先以 `GENERIC_WRITE|DELETE` 打开目标、冲突再退回），其余照文档：每块复制完
  `CALLBACK_CHUNK_FINISHED`，`PROGRESS_STOP` 留下部分副本，`PROGRESS_QUIET` 不再回调，取消标志等同 `PROGRESS_CANCEL`，
  `CopyFile2` 发流与块的消息。块大小、空文件是否也报一块等由 `tools/copyprogressprobe` 在 winref 上校准。

其余多为无害的 FIXME（`LOAD_LIBRARY_REQUIRE_SIGNED_TARGET` 不验签、跳转列表桩、MSAA 读界面时的 `LresultFromObject` wParam）；
`Windows.UI.Composition` 一系的激活是已知的大缺口（见上文 AirSpace 一节），WIC 三次插值等 `tools/wicscalerprobe` 的数据。
Office 保存回归照常。

### kernel32 文件测试剩下的 todo（altars-up `0a72bf1e68c`…`614ab18456b`）

逐个看 kernel32 文件测试里还标着 todo 的检查（都是 Windows 实测），凡是 Office 可能碰到的都补上：

- **只改大小写的改名**（`0a72bf1e68c`）：“report.docx” 改成 “Report.docx” 原先什么也不做——大小写不敏感的查找找到的就是
  文件本身，服务器见是同一文件直接返回成功。Windows 只在完全同名时才直接返回（[MS-FSA] 2.1.5.15.12）。现在目标就是被改名的
  文件时按请求的大小写改名；目标是另一个文件且要求替换时，先替换，再把结果改成请求的大小写（否则替换后留下旧的大小写）。
  经 8.3 短名匹配到的不算。文件与目录都适用（两处测试）。
- 改名要求句柄有 DELETE 权限（`d1f912a450e`；建硬链接不要求）。
- `FindFirstFile("不存在的目录\")` 得 `ERROR_PATH_NOT_FOUND`（`93e9121743b`；原先一律 `ERROR_FILE_NOT_FOUND`）。
- `CreateFile(已有目录, CREATE_NEW)` 得 `ERROR_ACCESS_DENIED`（`07a232b3cc7`）：要的是文件，“是目录”先于“名字已存在”。
- 以 `FILE_OPEN_IF` 加删除时关闭打开已有的只读文件得 `STATUS_CANNOT_DELETE`（`f8df7cf92a5`）；原先只对不创建的处置检查，
  这种打开会在关闭时把只读文件删掉。
- `OpenFile` 失败时不填 `cBytes`（`614ab18456b`）。

余下的是原始卷读写（`\\.\C:`、卷 GUID 路径）、`SetFileValidData`、符号链接、`GetFileType` 对 section 句柄等，与 Office 无关，
暂不处理。

### PowerPoint 导出动画 GIF 之后不再响应

功能普查里 PowerPoint 45 项通过，SVG 导出与原生一样“转换器未安装”；`SaveCopyAs … 40`（动画 GIF）之后 `CreateVideo`、
`Close`、`Quit` 全被拒（RPC_E_CALL_REJECTED），而原生 PowerPoint 之后照常响应、5 秒生成视频。`+wincodecs` 跟踪：
PowerPoint 建 GIF 编码器、`Initialize`，随即 `GetMetadataQueryWriter`，得到 E_NOTIMPL 就放弃，一帧都不建，后台导出从此
不结束。上游的元数据写入链是空的：所有编码器（GIF 与 PNG/JPEG/TIFF 的公共实现）的帧块写入器都是 E_NOTIMPL，
`SetMetadataByName` 对所有格式返回 S_OK 却什么也不写，GIF 的 LSD/IMD/GCE/APE/注释只有读取器，编码器提交时不写任何扩展。

altars-up `01c911684cf` 补上了这条链：编码器与每一帧各有一张元数据写入器列表（IWICMetadataBlockWriter），编码器的
查询写入器建在它上面；`SetMetadataByName` 在已有的写入器上设值，路径上的块不存在时按其格式建写入器挂到父级；五种 GIF
块都有写入器；提交时写出设置过的描述符字段、全局颜色表之后的应用扩展与注释、每帧图像之前的图形控制扩展。PowerPoint 2 秒
写出两帧、无限循环、带每页延时的 GIF，之后照常响应，普查从 45 项升到 48 项。winref 连不上，空块列表、没设 /grctlext
时不写 GCE、几种错误码、`/appext/Data` 末尾零字节的处理暂未实测（`tools/gifencprobe` 待跑）。注意：PNG/JPEG/TIFF 的帧块
写入器仍是桩，过去它们的 `SetMetadataByName` 假装成功，现在如实返回错误；普查里 PNG、JPG 导出不受影响。

普查的下一项随之暴露：`CreateVideo` 2 秒后以状态 4（失败）结束、0 字节（原生 5 秒生成）。跟踪止于
`sink_writer_Finalize` 的 FIXME：mfreadwrite 的 SinkWriter 把未编码的样本直接交给接收器（编码一步是 FIXME），
`Finalize` 未实现，上游与 CrossOver 都缺；上游测试 `test_sink_writer_sample_process` 已有 Windows 上测过的规格。

altars-up `37fb1028bf0`、`1763983021d`、`67c84e5b7dc` 补上：SinkWriter 让每个样本依次经过转换器与编码器、把输出按接收器的
请求交付，`Finalize` 排空变换、交付余下样本、经 IMFFinalizableMediaSink 收尾并关闭接收器（同步返回，或按
MF_SINK_WRITER_ASYNC_CALLBACK 回调 OnFinalize）；winegstreamer 的 MP4 接收器实现了 PlaceMarker 并在开始与处理后请求
样本；mfplat 文件字节流的 `Close` 原是 E_NOTIMPL，文件要到最后一次释放才关，`Finalize` 之后不能独占打开（Windows 上可以）。
mfreadwrite 测试去掉 6 个 todo_wine、0 失败。PowerPoint `CreateVideo` 4 秒写出幻灯片的 H.264 视频（gst-discoverer：
640×360、15 fps，逐帧内容正确），普查 48 项里只剩 SVG 导出，与原生相同。

### 可警报等待先跑排队的用户 APC，以及被 APC 打断的同步 I/O（altars-up `4202b7f67c3`…`252c1da7ece`）

kernel32 的 overlapped 测试（Windows 实测）：线程已有排队的用户 APC 时，对已触发的事件做可警报等待返回
`WAIT_IO_COMPLETION`，事件保持触发。上游 wineserver 的 `check_wait` 先看对象、后看 APC，记作 todo。`97e8e89bd27`
把 APC 的检查移到对象之前；ntsync 路径在进内核等待前先读线程的警报事件（本机没有 `/dev/ntsync`，未实测）。user32 的
msg 测试另定：`MsgWaitForMultipleObjectsEx` 带 `MWMO_ALERTABLE`、已有消息在等时返回队列、APC 留在队列——Windows
在等待之前先查队列（`b44ac8f23de`，目前只在可警报的 WaitAny 里这样做；有事件又有新消息、flags 为 0 时 Windows 返回哪一个，
`tools/waitorderprobe` 待 winref 实测，若也返回队列，条件里的 `MWMO_ALERTABLE` 应去掉）。

**坑：Wine 的 unix 侧内部等待一旦投递用户 APC，就让整个外层系统调用以 `STATUS_USER_APC` 返回**（APC 在返回用户态途中
执行，调用者拿到的就是这个状态）。APC 优先之后，这种内部可警报等待更容易被打断，暴露出两处原本就与 Windows 不符的地方：

- `NtCancelIoFile` 等取消完成时用了可警报等待，被取消请求自己的完成 APC 就在里面跑了，ntdll 的 pipe 测试要求它留到下一次
  可警报等待（“IOAPC ran too early”）。改为不可警报（`801eaf498d4`）。
- 以 `FILE_SYNCHRONOUS_IO_ALERT` 打开的文件上的同步请求（管道 listen/读写、套接字、串口）被 APC 打断时，Wine 返回
  `STATUS_USER_APC` 而请求仍在后台挂着。Windows 的 I/O 管理器（IopSynchronousServiceTail）是：取消该请求、不可警报地
  等它结束、返回它的最终状态，APC 在返回途中执行。`2148b25e194` 照此实现 `wait_async`；pipe 测试两处 todo（APC 打断同步
  listen 应返回 `STATUS_CANCELLED`）去掉。
- 与之配套的服务器规则：Windows 10/11 上，同步文件上先挂起后失败的请求也写 IOSB（ntoskrnl 测试里非 broken 的期望，
  老 Windows 10 不写）；同步请求失败时不排完成 APC，只置事件（`4202b7f67c3`、`4a68c5b65b0`，ntoskrnl 测试四处 todo 去掉）。

`CopyFile` 覆盖隐藏/系统文件按 Windows 规则失败之后（`97d0f78ea33`），wintrust 的 `CryptCATAdminAddCatalog` 第二次加同名
编目就失败（编目文件被它自己设成系统文件），setupapi 的 `SetupDiInstallDevice` 接着对未赋值的 `filepart` 解引用而崩溃。
编目先清属性再覆盖（`ad2d2455667`），INF 复制失败时安装设备如实失败（`252c1da7ece`）。顺带：`FileAllInformation` 的
`AccessFlags` 照 [MS-FSA] 填本句柄的授予权限（`9bfbfe7830e`）。

测试构建脚本 `scripts/build-winetest.sh` 现在也编 `#pragma makedep testdll` 的辅助模块并以 TESTDLL 资源嵌入（外层
`7fabb32`）：ntoskrnl 的驱动测试能跑了（29192 项，只剩 driver.c 里一处与本机 24 核有关的亲和性掩码），user32 msg 测试
因缺 helper 的那批失败也消失。验证：ntdll file/pipe/sync、kernel32 file/pipe/sync、ws2_32 sock/afd、ntoskrnl、user32 msg
0 失败（msg 另有一处 :77 无窗口管理器导致的“todo 里通过”）；Office 回归三件通过；普查 Word 42、Excel 79、PowerPoint 55
（唯一 FAIL 是与原生相同的 SVG 导出）；APC 顺序改动之前观察到的一次 Excel 普查中途崩溃，其后 10 遍 Word→Excel 与多遍全量
普查均未再现。驱动测试要在 scratchpad 的干净前缀里跑：它装测试驱动、建设备实例，中途崩溃会在前缀里留下陈旧的驱动库条目，
干扰下一次运行。

普查脚本里“错误密码被拒”原先只看有无错误，应用崩溃（462）也记成通过；现在只接受各应用的拒绝码（Word 5408、Excel 1004、
PowerPoint E_FAIL）。

### 普查里撞到的桩：命令行、WMI、圆角连接、JPEG 颜色配置、方向模糊与精灵批（altars-up `44814a09f7a`…`8045a4c38c5`）

让三件套普查在 `fixme+all,err+all` 下跑（会话里第一个进程带着 WINEDEBUG 启动，Office 各进程继承），按调用次数汇总
Office 真实使用功能时撞到的桩，挑不必等 Windows 实测、影响可见结果的补上：

- `NtQueryInformationProcess(ProcessCommandLineInformation)`（类 60）原本没有；wineserver 新请求 `get_process_cmdline`
  从目标进程 PEB 指向的参数块读命令行（进程改过的也读得到，未启动时取创建者给的），只要
  `PROCESS_QUERY_LIMITED_INFORMATION`，排布照 `ProcessImageFileName`（`44814a09f7a`；协议版本随之 +1，装的时候 wineserver
  与所有发请求的模块要一起换）。WMI 的 `Win32_Process.CommandLine` 因此对别的进程也有值（`43748d328a7`），并补了
  `Win32_Process.Terminate`（Reason 即退出码，`5319ed2a6fa`）；方法参数原先不收 VT_I4 形式的 uint32（`1e928173d0a`）。
  细节（MaximumLength、拒绝访问时的 ReturnLength、WoW64 读哪份 PEB）待 `tools/cmdlineprobe` 在 winref 实测。
- gdiplus 的 `GdipWidenPath` 没有圆角连接（105 次/普查），画成斜切角；现在外侧用不超过 90° 的贝塞尔圆弧（`5e95082afa9`）。
  Windows 的点列待 `tools/gdipwidenprobe` 实测后再写精确测试。
- `IGlobalOptions::Set` 原先只打 FIXME；Office 设异常处理为 `COMGLB_EXCEPTION_DONOT_HANDLE_ANY`、STA 模态循环的输入处理，
  现在保存下来供 Query 读回（`ec037b6cbe7`，行为本身尚未跟随）。
- WIC 的 JPEG 解码器声明不支持颜色上下文，Office 每解一张 JPEG 就问一次；现在把 APP2 里分段的 ICC 配置按序号拼起来作为帧的
  颜色上下文（`b39b45e3a79`），无配置时与 PNG 一样 S_OK、0 个。
- Direct2D 的 DirectionalBlur 效果没有变换图，Excel 画它的地方什么都没有；现在复用高斯模糊的着色器，沿角度（x 轴起逆时针、
  y 向下）做一遍（`314be4b0d12`，d2d1 测试加了只沿角度外溢的像素检查；去掉实现时这些检查全失败）。
- `DrawSpriteBatch` 在位图目标上什么都不画（PowerPoint 25 次/普查）；现在逐个精灵经“精灵变换×当前变换”把源矩形画进目标矩形，
  颜色的 alpha 作不透明度（`8045a4c38c5`；非白色的着色与钳到源矩形尚未做）。

`CreateSwapChainForComposition` 每次普查四五千到六千多次，全来自 Excel（先前按进程号误认成 PowerPoint）：给普查每一步留下带
时间戳的标记后看到，它们集中在表格、数据透视表、排序、筛选、数据验证、批注几步，一个线程两秒内调几千次——表格网格的动画
每帧都试着建组合交换链，失败就回退。组合交换链（dxgi 与合成器的 `CreateCompositionSurfaceForSwapChain`）原是桩，结果是
这些动画不显示、白耗一些 CPU；普查结果不受影响。

现在两者都已实现（altars-up `5e5c6ad5e7a` dxgi、`67a3e1bb2b0` dcomp）。组合交换链是挂在隐藏窗口上的普通交换链，窗口由 dxgi
自己的一个线程所有（哪个线程释放交换链都能销毁它）；Present 不去窗口，而是把整个缓冲（脏矩形已合成进去）复制进设备上的一张
纹理，再调用登记过的回调。合成器的交换链表面在目标的 Direct2D 上下文里画这张纹理，设备不同就先经暂存纹理带过去（与绘制表面
的瓦片共用这段代码）；与 Windows 一样不需要提交：每次 Present 只在合成器线程上排一次重画，按当前状态画各目标。顺带修了部分
呈现的合成原先不拿 wined3d 锁（`fe57596d01b`），组合交换链的呈现在 Excel 的渲染线程上、合成器同时在用同一设备。

实现时撞到的坑：创建一成功，AirSpace 马上调 `GetFrameStatistics`，桩回的 E_NOTIMPL 让它 fail-fast——Mso20win32client.dll
RVA 0x415900 是带标签的崩溃函数（向地址 0 写 1，再 `int 0x29`），+seh 里是紧跟在那次 FIXME 之后、同一线程上的写 0 访问违例，
Excel 在数据透视表一步崩掉，普查 37 ok/41 FAIL。组合交换链的 `GetFrameStatistics` 在首次 Present 前回
`DXGI_ERROR_FRAME_STATISTICS_DISJOINT`、之后按 60 Hz 计数，只改这一处就回到 79 ok。原来“创建失败就回退”的路径反倒安全：
新打通的路径上，后面每个桩都可能把回退变成崩溃。

同一普查、同样调试通道的对照：改动前一轮 733 次创建桩，另有 1466 个另一类 C++ 异常与 745 次重抛；改动后组合交换链只建 24 次，
那两类异常消失。`AirSpace::DeviceError`（Mso40UIwin32client.dll）改动前 777 个、58 串，改动后 648 个、46 串——这是另一个
原有问题，后来查明并修掉（见下）。

`AirSpace::DeviceError` 的来源：新建工作簿填数据这样的小场景全跟踪，抛出前最后一个失败的调用是同一绘制表面先
`SuspendDraw`、再 `BeginDraw`（换一个更新矩形），我们的 dcomp 回 `DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED`，AirSpace
连调两次 `GetDeviceRemovedReason`，抛 DeviceError，放弃这次绘制。可见的后果是 Excel 的工作表标签栏：标签位置只剩一块
浅色矩形，没有“Sheet1”、没有绿色下划线、没有左右箭头，开始时连“+”都没有（在 :77 上截屏；:2 是 rootless Xwayland，
截不了根窗口）。换回组合交换链之前的 dxgi/dcomp、或去掉截帧类，都一样缺，说明与今天的其它改动无关。让挂起中的那次
绘制在同一表面再 BeginDraw 时先按 EndDraw 的方式结束（两次画的都落地），标签栏完整出现，DeviceError 归零，普查仍
79/0（altars-up `001fbed3d99`）。drawsurfprobe 在 Wine 下的新输出已更新；Windows 上这些序列的确切 HRESULT 与哪些更新
落地，以 winref 的 drawsurfprobe 结果为准再对齐。

显示验证：`tools/compswapchainprobe` 在 200x200 窗口上用交换链表面画一个 100x100 精灵，Wine 下先后呈现红、绿、预乘的半透明红，
都不提交，屏幕读到 0000ff、00ff00、000080，精灵外不变。创建时的描述校验、无窗口交换链上 GetDesc 的 OutputWindow、
全屏切换、ResizeBuffers(0x0)、表面的运行时类名等细节是按文档与推断写的，待这个探针在 winref 实测后对齐。

同一批网格动画还要截一个合成视觉：AirSpace（Mso40UIwin32client.dll）用 `GraphicsCaptureItem.CreateFromVisual` →
读 `Size` → `Direct3D11CaptureFramePool.CreateFreeThreaded` → 会话 → 取帧纹理，沿途几乎每个意外的 HRESULT 都 fail-fast
（标签 0x248864d、0x248864e、0x2488651…），只有开头激活失败时平稳退回；上游 Wine 没有这个类（每轮普查 17 次“找不到库”），
d3d11 也没有 WinRT 互操作导出。现在都实现了：d3d11 的 `CreateDirect3D11DeviceFromDXGIDevice`/
`CreateDirect3D11SurfaceFromDXGISurface`（altars-up `a806e88c397`）；dcomp 的视觉经私有接口 `IWineCompositionVisualCapture`
在合成器每次绘制后把自己画进截帧缓冲（`305be6fb019`）；graphicscapture 的项、帧池（自由线程的 FrameArrived 走线程池，
否则走创建线程的 DispatcherQueue）、帧、会话（`18b9c4b2c67`，测试端到端取帧读像素）。窗口/显示器截取还没有，
`IsSupported` 仍答 FALSE。Excel 一轮普查建 6 个帧池（5 个自由线程，1 个 1072x1995）取 5 帧，三应用普查与之前相同
（Word 42/0、Excel 79/0、PowerPoint 55/1）。`tools/capturevisualprobe` 在 Wine 下的输出在其 results/，Windows 的待 winref。

已有前缀要补登记两个新类（新建前缀由 wineboot 自动登记）：`wine reg add
"HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId\Windows.Graphics.Capture.GraphicsCaptureItem" /v DllPath
/t REG_SZ /d C:\windows\system32\graphicscapture.dll /f`，`Direct3D11CaptureFramePool` 同样，`/reg:32` 再各一次；
或者 `wineboot -u`。

另：普查运行器若用 `env -i` 清掉 LANG，C 区域的字符集是 ASCII，PowerPoint“另存为 PNG”建不了中文名的“幻灯片1.PNG”而失败。
altars-up `5870f57b0a7` 让 C/POSIX 区域的 Unix 名按 UTF-8 处理（ASCII 是其子集，原来能用的不变）。d2d1 测试里 2D 仿射变换效果第 2 组（4×4 图按 0.75×2.5 缩放）的覆盖轮廓对不上，
本次改动之前就是如此。

另查明一个残留进程：PowerPoint 插入图表时以 `CREATE_SUSPENDED` 启动 `EXCEL.EXE /Automation -Embedding /K`，14 毫秒后就
`CoCreateInstanceEx(ChartDataSourceFactory, CLSCTX_LOCAL_SERVER)`；这时类还没注册，Wine 的 combase 立刻按 LocalServer32
另起 `EXCEL.EXE /automation -Embedding`，它只注册 Excel.Application、没有客户，PowerPoint 退出后一直不走（主线程空闲在
消息循环里）。Windows 上是否也会多起一个、多起的会不会自己退出，由 `tools/officeautomationprobe/powerpoint-chartexcel.vbs`
在 winref 实测后再定修法。

### PowerPoint 放映：切换、传感器监视器与墨迹

放映切换是好的：先前以为“切换不播动画”，是测试选的 3844 其实是 `ppEffectAppear`（瞬间出现，本无动画；常量从 MSPPT.OLB
的类型库读出）。换成 3849（平滑淡出）与 3863（涡流，3D 粒子）后，:77 上每 0.23 秒一张截图，13 张各不相同，从第一张逐帧
过渡到第二张，画面正确。

但放映中 PowerPoint 会崩：从 `mfsensorgroup.dll` 延迟加载 `MFCreateSensorActivityMonitor`（Office 里只有 PPCORE 用它，也
只用这一个），Wine 没有这个 DLL，0xc06d007e 没人接，主线程未处理异常。这里是放映开始约二十秒、用过指针之后；崩溃栈里有
PPCORE 的延迟导入槽 RVA 0x2210648。altars-up `b5ae900caa9` 新增 mfsensorgroup：监视器 Start 后在线程池上给一份报告，
其中没有传感器（Wine 没有帧服务器，不知道哪个进程在用摄像头），Stop 后不再报告。PowerPoint 现在建监视器、在另一线程
Start、读到空报告，放映照常进行。已有前缀要 `wineboot -u` 才有这个新 DLL 的占位文件（没有占位时加载器只在引导前缀时
才去 Wine 目录找内置 DLL）。参数错误、越界与未知设备名的返回值、Start 后是否立即报告等由 `tools/sensoractivityprobe`
在 winref 实测后对齐。

墨迹：放映里 `View.PointerType = 2`（笔）之后用 SendInput 拖动，原来单击照样推进放映、不留笔迹，`View.DrawLine` 画完
也看不到。一层层查下来（altars-up `05c47d34a0b`、`fe12ccdf2b4`、`dc20c7dbc59`）：

1. PowerPoint 放映时先 `CoCreateInstance(CLSID_InkRenderer)`，失败就不再管墨迹；inkobj 原是空壳。现在实现了 InkDisp（笔画、
   集合、命名集合、删除/提取/克隆/命中测试）、InkRenderer（对象变换→视图变换→按 DC 分辨率换像素，GDI+ 画、非 CopyPen 用
   ROP2）、InkDrawingAttributes（文档默认值）、InkTransform、InkRectangle，带类型库供 IDispatch。只注册这五个类，
   InkCollector 等未实现的保持未注册。顺带修了 msinkaut.idl：IInkRectangle 缺基类（虚表整体错位）、hDC/hWnd 应为
   LONG_PTR、DISPID、`ID` 的名字；以及 widl 编这个类型库时在 STGMEDIUM 的空 union 分支上崩溃（`0bede15c74f`）。
2. 设笔时 Office Art（OART.DLL）的墨迹工具建 RealTimeStylus、`AddStylusSyncPlugin`，再建 StrokeBuilder 作为异步插件；
   rtscom 原是存根（全部 E_NOTIMPL），StrokeBuilder 也未注册，工具抛出、整个墨迹输入被丢弃（跟踪里 RTS 刚建好就释放）。
   现在 RTS 用窗口线程上的鼠标钩子把按下/移动/抬起变成 StylusDown/Packets/StylusUp（HIMETRIC、相对客户区），同步插件就地
   调用、异步插件经该线程队列；StrokeBuilder 既能直接建笔画，也作为插件把每次按下建成一笔（通过 inkobj 笔画的私有接口
   `IWineInkStroke::AppendPackets` 边画边长）。

现在拖动时红色笔迹实时出现、抬起后保留，单击不再推进放映，`DrawLine` 的线也画出来（`tools/officeautomationprobe/powerpoint-pen.vbs`，
截图里拖动中 859、抬起后 3410 个红色像素）。PowerPoint 自己画笔迹（读笔画的包），不调用 InkRenderer 的 Draw。
Office 还在墨迹对象上查询两个 SDK 里没有的接口 {49e015bc-…}（ink）和 {4a145a90-…}（stroke），不支持也能工作。

放映结束时“是否保留墨迹注释?”选“保留”，PowerPoint 要把笔画变成墨迹形状，先读每笔 X、Y 的包属性度量；这个方法原是桩，
形状出不来（`73d3d0557ee`、`07bc82cd52c` 让笔画保存度量，StrokeBuilder 把 RTS 给的度量交给笔画）。现在幻灯片上有了
“墨迹 1”（p14:contentPart，位置尺寸都在），但写出的 ppt/ink/ink1.xml 是空的 `<inkml:ink/>`。

空 InkML 的来龙去脉（OART、PPCORE、MSO、Mso40UI 的公开符号 + bptrace/bpread，2026-09-30）：

1. {49e015bc-…} 是 Office 自己的 `OInk::IInkDispAdaptor`（OART 的 `CQIPtr<IInkDispAdaptor, _GUID_49e015bc…>`），
   {4a145a90-…}、{c673d14b-…} 是它在笔画、绘制属性上的同族接口：Office 用它们认出自己的墨迹对象，Windows 的 InkDisp
   也不会有，不必实现。
2. “保留”这一路本身是通的：PPCORE `InkManager::SSInkToShape` → `SaveInkStrokesForSlide` → OART `IInkDispData::Create`
   → `InkDispData::SetInkDisp` → `ConvertInkDispToInkDisp2`（`LoadInkFromInkDisp` 逐包读我们的笔画、度量、时间戳
   扩展属性 {8A54CF58-…}、笔刷，再 `CreateInkDispFromInk` 建成 Office 自己的墨迹，笔画都在）→ `InsertInkCommand`
   建 `InkDrawingElement`。之后那个新建的 InkDisp 只是交给 StrokeBuilder 重新开始（`putref_Ink`）。
3. 保存时 `InkContentPartBase::BeforeSave` 只写内容部件里现成的 InkData，没人填过：`SetInk` 从未调用，保存时也没有
   `LoadInkFromInkDisp`。什么时候现生成、什么时候拷贝元素里存的 InkData，由 `Dr::FInkObjAvailable()`（MSO
   `MsoFInkObjAvailable`）决定：它读 `HKCR\CLSID\{3EE60F5C-9BAD-4CD8-8E21-AD2D001D06EB}\InprocServer32`，文件存在才算
   “墨迹平台可用”。OART 里有 27 处看它（`Art::FInkIsPossible`、`Dr::FTabletFeaturesEnabled`、
   `InkInputTextureUser::FIsInkPossible`、保存转换等）。Wine 没注册这个类，于是 Office 以为没有墨迹平台，而 RTS 照样能画——
   这种组合 Office 没预料到：新墨迹元素的 InkData 为空，就写出空文件。
4. 试着在开发前缀里临时注册它（指向 inkobj.dll，试完删掉）：放映笔反而完全画不出来。原因是平台“可用”时
   `MsoHrCoCreateInkInstance` 第一次建墨迹对象前要 `CGuidCollector::FInit` → `FCreateInkWithAllWispGuids`：建一个
   InkDisp，把 MSO 内嵌的 303 字节 ISF（HIMETRIC 尺寸、4 个 Office GUID 的 GUID 表、墨迹空间矩形、含全部预定义与 4 个
   自定义绘制属性的属性块、一个 0 点的笔画）交给 `IInkDisp::Load`；inkobj 的 Load 是桩，失败后所有墨迹对象都建不出来
   （bpread：返回 0x8000FFFF），InkRenderer 没有，墨迹表面也就没有。
5. 所以与 Windows 一致的做法是：实现 ISF 的 Load/Save（微软公开的 ISF 规范；标签数值与默认哈夫曼表取自 MIT 许可的
   WPF 实现），再按 winref 上的实测注册 {3EE60F5C-…} 那个类。`tools/isfprobe` 在 Windows 上产出各格式、各压缩模式的
   真实 ISF 字节作解码器的测试向量；`tools/inkprobe` 与批次里的 `reg query` 回答那个 CLSID 是什么、在哪个 DLL。

同一轮里，Word 的“绘图”选项卡在临时注册后仍被隐藏，开关另有所在。

ISF（altars-up `e3d8909b28e`）：Load 收原始 ISF、`base64:` 文本和 GIF 注释扩展里的 ISF；Save 以三种压缩模式写原始 ISF 与
base64（GIF 输出未做）。解码覆盖 GUID 表、墨迹空间矩形、绘制属性（笔宽高的小数部分、自定义属性）、笔画描述（按钮、
笔画属性列表）、度量、单/双精度变换表、包数据（默认哈夫曼表、带或不带 delta-delta 的比特打包）、属性数据（比特打包、
LZ，带嵌入的 VARIANT 类型）、点属性块，未知标签按长度跳过。实现由 Claudex 按任务卡完成，我审查时改了三处：
DrawingFlags 的位（WPF 的 `DrawingFlags.cs`：拟合曲线 0x1、忽略压力 0x4、抗锯齿 0x10，原先反了）、0 点笔画不写包数据
（原先写了，自己都读不回）、Load 把属性拷进墨迹原有的扩展属性对象。inkobj 测试 633 项全过；MSO 那段 303 字节的 ISF
载入得 1 笔 0 点、颜色 0x32、宽 50、4 个自定义属性，再 Save/Load 一致。

在开发前缀里临时注册 {3EE60F5C-…}（指向 inkobj.dll，跑完删掉）后整条链路都通：放映里笔画出笔迹，保留得 1 个墨迹形状，
保存的 `ppt/ink/ink1.xml` 有完整的 InkML（上下文、X/Y 通道与分辨率、时间戳、红色笔刷、一条 trace）。正式注册要等 winref
说清那是什么类、在哪个 DLL；Word 的“绘图”选项卡还要 `AllowDevicesSansTouchSupport` 功能门或 SM_TABLETPC 非 0
（`Mso::InkUI::IsDrawTabActive`；`MsoFInkingEnabled` 即 Mso20Win32Client 序号 50906 = 缓存的 `GetSystemMetrics(SM_TABLETPC)`）。

仍未做：ISF 保存/载入、剪贴板、按矩形裁剪、压力改变笔宽、平板与光标对象、真正的笔和触摸输入；Word 的“绘图”选项卡在功能区
模型里存在但被隐藏（`SM_TABLETPC`/`SM_DIGITIZER` 改成非 0 也不出现，开关另有所在）；AirSpace 的 InkDesktopHost/InkD2DRenderer
也没有。各默认值、组合顺序、错误码等未实测的细节列在各提交说明里，`tools/inkprobe` 与 inkobj/rtscom 的测试在 winref 上跑后对齐。

### ncalrpc 往返、ntsync 下的等待包与互斥体、邮槽的文件类型

**ncalrpc（altars-up `8caf6adef4e`）。** Wine 的 ncalrpc 走消息模式的命名管道。原来收一个包要读三次管道：先读 0 字节等
数据，再读公共头，再读其余；每次读都是 wineserver 往返（`read` 返回 PENDING，经 APC 与 `get_async_result` 取回）。现在
连接上挂一个 `RPC_MAX_PACKET_SIZE` 的缓冲区，整条消息一次读入，头和体都从缓冲区取，仍在消息边界停下。`tools/rpcbench`
在同一棵树、同一前缀里新旧交替测：两进程间 48 字节的一次调用，从 22 个请求、约 280 µs 降到 14 个、约 170 µs。
Click-to-Run 的 App-V 注册表钩子每次启动 Office 约调用一万次，这就是它的意义。rpcrt4 的六组测试与 ole32 的
marshal/compobj/moniker 全部 0 失败，普查不变。`scripts/build-winetest.sh` 现在照 makedep 读取 `EXTRADEFS`、
`EXTRAIDLFLAGS`，并生成 server/proxy 桩和 `dlldata.c`，rpcrt4 的测试才构建得出来。

**ntsync 下的等待完成包（`f179f4513e2`）。** 装上 `/dev/ntsync` 后，事件和信号量由客户端直接在内核里触发，服务端看不见；
服务端自己设置的内部同步对象（定时器、进程、线程）也不经 `wake_up()`。昨天移植的 wait completion packet（Zhiyi Zhang
为 React Native 写的合并请求，上游 master 与 Proton 都没有）因此一个都不投递，ntdll 的 file 测试失败 81 次。Office 自带的
`WinAppSDK\CoreMessagingXP.dll`（DispatcherQueue）导入了 `NtAssociateWaitCompletionPacket`；一次 Word 会话会加载它，
但实测 2.5 分钟内没有调用。

现在的做法分两类：
- 服务端自己触发的对象：设置后唤醒挂在上面的包，关联时用不阻塞的 ntsync 等待判断并消耗信号。
- 客户端触发的事件和信号量：由做关联的进程起一个“包监视”线程（不是 Windows 线程，屏蔽全部信号），用
  `NTSYNC_IOC_WAIT_ANY` 等目标，把等到的编号写进服务端轮询的 socket，由服务端投递。包已被取消时，服务端把消耗掉的信号
  还回去。
- 时序与 Windows 一致：关联要等监视线程真正开始等才返回，否则 PulseEvent 会丢；零超时地查看完成端口之前、以及取消之前，
  先等监视线程把已触发的都报上来。

ntdll file 测试 4110 项，连跑 5 次都是 0 失败；新加的 100 个包的测试会用到两个监视线程。ws2_32 sock（267 万项）、afd、
ntdll 的 om/pipe/sync 均 0 失败。

**ntsync 下被放弃的互斥体（`4c776a3db42`）。** 被拥有的互斥体的最后一个句柄一关，服务端就关了它的 ntsync 对象，拥有者
线程死掉时无法再标记“已放弃”，靠自己的 fd 还在等它的线程永远等下去（kernel32 sync 测试 417、421 行）。现在这样的互斥体
留到没人拥有为止，线程死亡时照样被放弃。kernel32 sync 从 2 个失败变为 0。

**邮槽的 GetFileType（`e22e7cc11c7`、`4b9e903c985`）。** 服务端给邮槽两端补上卷信息查询，设备类型是 FILE_DEVICE_MAILSLOT；
GetFileType 对它返回 FILE_TYPE_UNKNOWN，并把错误码清为 NO_ERROR（文档写明的“类型未知但调用成功”）。原先返回的是
ERROR_INVALID_HANDLE。邮槽的 Characteristics 暂为 0，待 winref 实测；ntdll 测试会打印这个值。节句柄上的 GetFileType
仍是 todo：ntdll 映射视图时要从节句柄取底层文件的 fd，服务端分不出两种用途，改它需要改协议，价值不大。

**winref 的待机。** 它是 S0 低电量待机（现代待机，联网）。用户要求后，登录时由 `WIN_PRELUDE` 把交流、电池两种情况的睡眠与
休眠超时都设成“从不”（原值：交流本来就是从不，电池 3600 秒后睡眠）。现代待机中，Wi-Fi 唤醒只让 SoC 短暂醒来、不会退出
待机，所以已经在待机里的机器要有人让它完全醒来一次；之后它就不会因闲置而睡。`scripts/winbatch.sh` 的连接能撑过约
15 分钟的小睡，主连接断了就停下，不会为后续步骤另开登录。

### 缩小图片的画质：WIC 缩放器与 Direct2D 的非锯齿模式

用默认调试级别（err 与 fixme 全开）跑三个应用的普查，把 Office 实际碰到的 FIXME/ERR 按消息归并计数
（`scratchpad` 里的 `fixmesweep.sh` 与 `fixmesum.py`），按命中次数找缺口。其中两项影响每张缩小显示的图片：

- **WIC 缩放器（altars-up `032e3caf78f`）。** PowerPoint 用 Fant 模式调 `IWICBitmapScaler` 29 次，而 Wine 除最近邻外的模式都
  退化成最近邻。现在对 8 位、16 位和浮点通道的格式，按目标像素中心做可分离重采样：
  - 线性、三次（Keys，a=-1/2）分别取最近 2 个、4 个源像素；
  - 高质量三次按缩小倍数加宽三次核；
  - Fant 缩小时按覆盖面积平均，放大时线性；
  - 索引与打包格式仍取最近邻。

  `wincodec.idl` 补上 `WICBitmapInterpolationModeHighQualityCubic`。新测试只断言任何插值都成立的性质：常数图保持常数、阶跃放大后
  中间像素是插值值、棋盘格缩小一半接近灰。旧实现在这些断言上全部失败。`tools/wicscaleprobe` 在各格式、各模式下打印像素，
  等 winref 实测后对齐像素中心、边缘、舍入的细节。
- **Direct2D（`1f5346321a6`）。** PowerPoint 以高质量三次模式画位图，Wine 除最近邻外一律每像素双线性采样一次，缩小时和线性一样
  出锯齿。现在多重采样线性、各向异性、高质量三次这三种模式由像素着色器按纹理坐标的导数算出像素在位图上的覆盖范围，
  在其中每个方向最多取 8 个双线性样本取平均；放大不变。新测试把 30 个纹素宽的黑白相间列缩到 8 像素，三种模式都应接近灰
  （0x60–0xa0）；旧实现得到 0xdf、0x20 这样的锯齿值。

两项都没有改变普查结果（Word 42/0、Excel 79/0、PowerPoint 55/1），PowerPoint 的缩放 FIXME 已消失。同一次归并里的其余高频项：
- msxml 等对象上查询 `{e19c7100-9709-4db7-9373-e7b518b47086}`：出现在 80 个 Office 模块里，像是 Office 内部接口，待 winref 的
  `reg query` 确认；
- 另一个 Office 进程创建未注册的 `{94269c4e-071a-4116-90e6-52e557067e4e}`：同样待 winref；
- `RoGetActivationFactory` 的 FIXME 每次调用都会打印，逐个核对后只缺 WAM 与 WinAppSDK 自己的 `Microsoft.UI.Dispatching.DispatcherQueue`
  （后者由 Office 自带的 CoreMessagingXP 提供）；
- msctf 的文本存储通知桩不影响输入法（Word 走 IMM）；
- WebP 帧的颜色上下文不支持，待查 Windows 的行为。

### 4K 屏上 Office 只有一半大：宿主的 Xft.dpi、显示器 DPI 与系统 DPI（altars-up `91f3d2a63b4`…`c3b58df782d`）

用户的 4K 屏（物理 3840x2160，GNOME 缩放 200%）上，Word 的字体和界面只有应有大小的一半。逐层量过（`tools/dpiprobe`，
以及读 `:0` 的 `xrdb -query`、`xrandr`、mutter 的显示配置）：

- **宿主。** GNOME 50 的 Xwayland 默认原生缩放：X 客户端拿到显示器的物理像素，缩放比例只经根窗口 `RESOURCE_MANAGER`
  里的 `Xft.dpi` 告诉客户端（200% 时 192）。GTK、Qt 都按它放大。
- **Wine。** 系统 DPI 来自 `HKCU\Control Panel\Desktop\LogPixels`，没有就取 `HKCC\Software\Fonts\LogPixels`（wine.inf 写 96）；
  win32u 早有按显示器的 DPI（`add_source` 的 dpi 参数），但每个驱动都传系统 DPI，也从不读 `Xft.dpi`。于是 Wine 以为这是
  96 DPI 的 3840x2160 屏。
- **Office。** 清单里不声明 DPI 感知，运行时把线程设为按显示器感知（v1，进程本身不感知），按 `GetDpiForWindow` 排版——96，
  1 倍画在 4K 的物理像素上。
- **Windows 上同样的屏。** 显示器有效 DPI 192；系统 DPI 自 1803 起按进程，取进程启动时主显示器的 DPI
  （所以有 `GetSystemDpiForProcess`），也是 192；不感知的程序看到 1920x1080，由系统放大。

改动：

1. **winex11 读宿主的 DPI（`c3b58df782d`、`9523a0de38b`）。** 显示器的 DPI 取根窗口资源库的 `Xft.dpi`（用户在 winecfg 设过
   LogPixels 时以用户为准），没有 `Xft.dpi` 时按 X 客户端的惯例当作 96；桌面线程监听根窗口的 `RESOURCE_MANAGER`：宿主换缩放
   时重新枚举显示器。
2. **系统 DPI（同一提交）。** 读到的宿主 DPI 写进 `HKCC\Software\Fonts\LogPixels`（Windows 存当前硬件配置 DPI 的地方），之后启动的进程以它
   为系统 DPI。会话的第一个进程（冷启动的 Word）在桌面进程读到宿主 DPI 之前就定下了系统 DPI，要到下一次启动才对上：
   这一次 Word 的样式库预览按 96 画、偏小，状态栏多出“显示器设置”按钮（Office 发现系统 DPI 与显示器 DPI 不一致才显示它），
   下一次都没有了。
3. **DPI 变化的通知（win32u，`6e81bdfe414`、`a38d3f0e7db`）。** 显示器的 DPI 变了，按显示器感知的顶层窗口收到 `WM_DPICHANGED`，建议矩形：最大化的不变，
   其余保持左上角、尺寸按新旧 DPI 之比缩放；v2 窗口的子窗口树先自底向上收到 `WM_DPICHANGED_BEFOREPARENT`，事后自顶向下
   收到 `WM_DPICHANGED_AFTERPARENT`。窗口在消息发出前已经是新 DPI。`WM_DPICHANGED` 的 lParam 是 RECT 指针，发往别的
   进程时原先没有打包。
4. **子窗口的 DPI（wineserver，`0625d004a5d`）。** 子窗口只在创建与换父窗口时复制一次父窗口的 DPI，顶层窗口换了 DPI 它们仍是旧值：Word
   切到 96 后 `MsoWorkPane`、`NetUIHWND` 等子窗口仍报 192。现在顶层窗口的 DPI 一变就更新整棵子窗口树（换父窗口时被移动
   窗口的子树也一起），显示器变化时服务器先更新各顶层窗口的 DPI。
5. **`scale_dpi`（wineserver，`91f3d2a63b4`）。** 自 2018 年（`c5a69256769`）起把负坐标乘以无符号的比值：192 DPI 上最大化窗口的 (-4,-4) 给
   96 DPI 的调用者换算成 22369618。改为有符号 64 位。

验证：Xvfb `:79`（3840x2160，`-noreset`），`xrdb -merge` 切 `Xft.dpi`。192 下冷启动 Word 是清晰的 2 倍界面；动态切到 96 再切回
192，顶层与全部子窗口的 DPI 一起变（`dpiprobe`），画面稳定后（6–20 秒）排版一致；之后启动的进程系统 DPI 为 192，
不感知的视图看到 1920x1080。user32 的 monitor、sysparams、win、msg、input 与 win32u 测试前后失败清单逐条相同（Xvfb `:77`，没有 `Xft.dpi`；基线 win 的两条偶发失败新构建没有出现）。

陷阱：
- **画面比 DPI 值慢。** Word 在 4K 下重排要 6–20 秒；第一轮在两次探针结果一致时就截图，看到的是半途的画面，误以为功能区
  “慢一步”。截图要等连续两张相同。
- **Xvfb 在最后一个客户端断开时重置**，`RESOURCE_MANAGER` 随之消失，`Xft.dpi` 就没了；测试用的 Xvfb 要加 `-noreset`。
- **用户日常的 `/opt/wine-altars` 与 `~/.wine-c2r-test` 当时是旧分支**，没有这些改动；那边的权宜办法是在 winecfg 把 DPI 设成 192
  （HKCU 的 LogPixels），代价是宿主换缩放时不跟随。10 月 1 日起 `/opt` 已换成 altars-up，菜单也改开 `~/.wine-c2r-up`。
- **宿主没有 `Xft.dpi` 时沿用旧值（已修，`9523a0de38b`）。** 起初没有 `Xft.dpi` 就退回系统 DPI，而系统 DPI 来自上一个宿主写进
  HKCC 的值：在 `:79`（192）上测过之后，`:2`（没有 `Xft.dpi`）上的 Outlook 也成了 2 倍。现在没有 `Xft.dpi` 就是 96，并同样写进 HKCC。
- 没做的：`DisplayConfigGetDeviceInfo`/`SetDeviceInfo` 的 `SOURCE_DPI_SCALE`（Windows 设置改缩放用的接口，Wine 未实现，
  所以 monitor 测试在 Wine 里改不了缩放）；winewayland 仍把系统 DPI 当显示器 DPI（上游 MR !11101 做分数缩放）；
  `WM_GETDPISCALEDSIZE`。

### .NET Framework 4.8、Power Query、OfficePLUS，以及装了 .NET 之后卡死的前缀更新（altars-up `286ab3b4ad6`、`05a55eebc00`）

开发前缀原来没有任何 .NET（连 Wine Mono 也没有），DllOverrides 里还有 CrossOver 瓶子带来的 `mscoree=""`（禁用）。

- **安装。** `winetricks -q dotnet48`（dist-up 的 Wine，`:77`）：.NET 4.8（`Release` 0x80eb1），64 位与 32 位的 C# 程序都能编译运行
  （CLR 4.0.30319.42000，4.8.3761）。winetricks 会把 Windows 版本改成 Windows 7 SP1 且**不改回来**：装完照装前备份逐值还原
  （64 位视图 26100/UBR 6899，32 位视图原本就是 22000/UBR 588，`Control\Windows\CSDVersion` 0），并删掉禁用 `mscoree` 的那条，
  只留 winetricks 写的 `*mscoree=native`。
- **Power Query 可用。** `tools/officeautomationprobe/excel-powerquery.vbs`：M 公式（`#table` 再 `Table.AddColumn`）经
  `Microsoft.Mashup.OleDb.1` 加载到表并同步刷新，读回 `a,b,c | 1,2,12 | 3,4,34`，与预期一致；求值在 .NET 容器进程里。
- **装了 .NET 之后，每次前缀更新都卡死（Wine bug 47144）。** wine.inf 对 `RegisterDllsSection` 里的每个 DLL 调 `DllRegisterServer`；
  `mscoree` 被覆盖成 native 后调到的是 .NET 的垫片，它去找 2.0 运行时，弹出“This application could not be started. Do you want
  to view information about this issue?”并一直等人回答；`wineboot` 与此间启动的所有程序（Office 也在内）都跟着等。前缀每装一次
  新构建就会更新一次，所以这是必然发生的。修法（`286ab3b4ad6`）：setupapi 新增 Wine 专用指令 `WineRegisterDlls`（与
  `WineFakeDlls` 同理），注册方式同 `RegisterDlls`，但跳过实际加载到的不是 Wine 内置模块（DOS 头后没有 “Wine builtin DLL”）的
  DLL/EXE，即被安装程序替换过的那些，它们由各自的安装程序注册；wine.inf 的两处改用它，第三方 INF 的 `RegisterDlls` 不变。
  `+setupapi` 跟踪：quartz、itss 照常 `calling DllRegisterServer`，非内置的测试 DLL 是 `not registering`；开发前缀
  `wineboot -u` 56 秒走完、不再弹框，之后 .NET 64/32 位照常。setupapi 新测试在 Windows 上同样成立（它不认识这个指令）。
- **OfficePLUS（已解决，见下一节）。** 起初三个 VSTO 加载项（LoadBehavior 3，`Manifest` 指向 `MSOP*Addin.vsto|vstolocal`）Excel
  根本不尝试加载：诊断日志 `%TEMP%\Diagnostics\EXCEL\Primary*.log` 里只有 TCSCConv 的 `Office.Programmability.Addins.InternalSetConnect`。
  Click-to-Run 只带了 VSTO 的加载器部分（`vstoee.dll`、`VSTO\10.0\VSTOLoader.dll`），没有 `VSTO Runtime Setup` 键，也没有
  `Microsoft.Office.Tools.*` 程序集；OfficePLUS 的官方安装包会连同 VSTO 运行时一起装，这里的 OfficePLUS 是随 Click-to-Run 装进来的。
  装上微软的 VSTO 运行时（`vstor_redist.exe` 10.0.60910，静默安装，退出码 0）之后：用自动化把 `COMAddIns("MSOfficePLUS").Connect`
  设为 True 能连上（2.2 秒），功能区出现 OfficePLUS 选项卡，说明运行时与加载项本身在 Wine 下可用；但正常启动 Excel 时时载时不载。
  在 MSO.DLL 的 `HrDoBootConnections`、`FCheckLicensed`、`HrInternalSetConnect`、`HrCreateManagedAddIn` 上下断点（bptrace，
  公开符号）确认启动时确实走到了创建托管加载项；没载上的那几次，日志里 RpcSs 没能启动。原因有二，都已修：
  1. **RpcSs 被服务管理器的启动锁挡住（`1fac1c633e7`、`113e7f217ff`、`21ca1bcf4de`）。** services.exe 自动启动时持一把全局启动锁，
     逐个启动、逐个等；OfficePLUS 的服务等满超时（当时 10 秒）报 1053 期间，Excel 第一次用 COM 时 combase 的
     `StartService(RpcSs)` 等这把锁 3 秒就得到 `ERROR_SERVICE_DATABASE_LOCKED`，随即放弃，COM 与 VSTO 加载一起失败。
     Windows 上 RpcSs 是“COM Infrastructure”组里的自动启动服务，按 `ServiceGroupOrder` 排在第三方服务前面，Wine 的排序既不看组、
     RpcSs 也是按需启动。现在自动启动按 `ServiceGroupOrder` 的组序排（无组或列表里没有的组排最后，组内按 tag），wine.inf 把
     RpcSs 设为“COM Infrastructure”组的自动启动并补全组序列表；顺带把等服务连上的默认超时改成 Windows 的 30 秒，
     `ServicesPipeTimeout` 按 Windows 的 REG_DWORD 读（仍兼容以前的字符串）。之后 RpcSs 启动失败归零。
  2. **OfficePLUS 的服务本身报 1053：Wine 的服务不在会话 0。** 见下一节。
  另有一个测试方法造成的假象：强杀 Excel 后下次启动会弹“安全模式”询问，加载项自然不载；改为模拟关闭按钮正常退出后，
  连续 4 次都在启动时加载。
- **VBScript 的 `GetObject` 带类名（`05a55eebc00`）。** 检查 OfficePLUS 时发现 `GetObject(, "Excel.Application")` 报 445：Wine 只支持
  “一个路径名”的形式。现在按文档：省略路径取运行中的实例（`GetActiveObject`），空路径新建实例，给了文件则新建实例后
  `IPersistFile::Load`；找不到时 429。附带修了名字对象路径里 `obj_unk` 的引用泄漏。vbscript 的 createobj、run 测试 0 失败，
  Windows 对照已加进 winref 批次。注意 Office 要等窗口第一次失去焦点才登记到运行对象表，所以刚启动时取不到是正常的。

### 服务在会话 0（altars-up `21c0210f89f`…`8bf1076d1ef`）

OfficePLUS 的服务 `MSOfficePLUSService.exe`（.NET Framework 4.7.2，用 Microsoft.Extensions.Hosting.WindowsServices）每次都报 1053。
它的 `IsWindowsService()` 判断自己是不是服务的办法是：父进程叫 `services` **且在会话 0**。Windows 从 Vista 起服务都在会话 0，用户在
会话 1；Wine 里所有进程都在会话 1（上游 kernel32 测试 `test_services_exe` 一直是 `todo_wine`，Wine bug 52726）。判断不成立，它就当
控制台程序跑，从不调 `StartServiceCtrlDispatcher`，服务管理器等满超时报 1053。改成和 Windows 一样，services.exe 及其服务在会话 0：

- **令牌的会话（`21c0210f89f`）。** `NtSetInformationToken(TokenSessionId)` 原来是假装成功的桩。现在照 Windows：句柄要
  `TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID`，调用方要启用 SeTcbPrivilege，用这个令牌建的进程就在那个会话。
- **控制台会话（`ad9dfc143f4`）。** `WTSGetActiveConsoleSessionId` 原来返回调用方自己的会话，服务调用时就成了 0。现在照 Windows 读
  `KUSER_SHARED_DATA.ActiveConsoleId`，由 wineserver 填成桌面所在的会话。
- **窗口站按会话（`5f725403701`）。** `EnumWindowStations` 在 Windows 上只列调用方会话的窗口站，Wine 原来列全部，会话 0 一旦也有
  `WinSta0` 就会列出两个同名的、用名字各自打开的却是自己会话的那个；跨会话新建的进程也不再沿用父进程的窗口站与桌面，而是按名字
  连自己会话的（服务端原来查窗口站名时根目录为空，从来查不到）。
- **会话 0 没有显示（`e334938c23d`）。** 会话 0 的 `WinSta0` 不设可见（服务端把第一个可见窗口站当作屏幕上的，硬件输入送给它），
  会话 0 的进程按服务处理：只有服务的虚拟显示器，不起自己的 explorer 桌面进程，也不用没有的驱动去读写显示配置注册表。
- **跨会话广播（`ec83b06df26`、`45584e84dc9`）。** mountmgr 的盘符变化、plugplay 的设备接口变化原来靠
  `BroadcastSystemMessage`，在会话 0 里只到得了会话 0 的桌面。Windows 上会话 0 的服务用 winsta.dll 的
  `WinStationBroadcastSystemMessage` 广播到各会话；现在实现了它（以及 `WinStationEnumerateW`，会话即 `\Sessions` 下的编号目录），
  两处改用它广播到所有会话。winsta.h 移进 include。
- **WTS（`f0fcc7643ff`）。** `WTSEnumerateSessions` 列出会话 0“Services”（Disconnected）与控制台会话“Console”（Active），去掉了上游
  测试的 `todo_wine`；`WTSQuerySessionInformation` 按所问的会话回答；`WTSQueryUserToken` 原来不管问什么都返回调用方的令牌，现在照
  文档只给启用了 SeTcbPrivilege 的调用方（SYSTEM），返回所问会话里、不带该特权的主令牌，会话 0 没有用户（`ERROR_NO_TOKEN`）。
  服务就是这样在用户会话里启动用户看得见的程序。
- **wineboot（`eb949e4008f`、`5bd714aa2cc`、`8bf1076d1ef`）。** services.exe 用会话 0、启用 SeTcbPrivilege 的令牌启动（它代表的
  SYSTEM 就是这样，服务继承下去才能调 `WTSQueryUserToken`）；它与 wineboot 握手的事件改成 `Global\` 名字。顺带修了一个旧问题：
  新前缀里以 `wineboot --init` 为命令时初始化会跑两次（第一个进程先自动建前缀，然后它自己就是 `wineboot --init`），各起一个
  services.exe，两个服务管理器服务同一个管道、启动同样的服务；旧的 /opt 构建同样如此。现在已有服务管理器在跑就不再起第二个。

验证：tasklist 里 services.exe、rpcss、plugplay、winedevice、OfficeClickToRun 与 `MSOfficePLUSService.exe` 都在会话 0“Services”，
explorer 与程序在会话 1“Console”；`sc query "OfficePLUS Service"` 为 RUNNING；Excel 冷、热启动各一次都在 12 秒内加载 OfficePLUS
并正常退出。新加或去掉 todo 的测试（advapi32 security 的令牌会话、kernel32 process 的服务会话与控制台会话、user32 winstation 的
窗口站不重复、wtsapi32 的会话列表与用户令牌）0 失败；advapi32 service、user32 broadcast、ntdll om、ole32 compobj/marshal/moniker、
rpcrt4 rpc/server、kernel32 sync 也都 0 失败；全新前缀两种建法都只有一个 services.exe。这些新测试在 Windows 上的对照
（SSH 下是会话 0、桌面上是会话 1）已加进 winref 待跑批次：这次登录后它又进了现代待机，要有人让它完全醒来一次。

### Outlook 日志里顺带补的几处（altars-up `e60d2d5c988`、`007d67a7f2f`…`e9910d08dfc`）

- **`Expires: -1` 被缓存了十分钟（`e60d2d5c988`）。** HTTP 规定无效的 `Expires` 当作过去的时间；`-1` 是“不要缓存”的惯用写法，
  Exchange 给 Outlook 的应答里就有。WinINet 解析不了它，落到“没有过期信息，默认十分钟”，于是这样的应答会从缓存里再拿十分钟。
  现在无效日期即已过期；网上常见的坏日期从 ERR 降为 WARN。wininet 的 http 测试加了这一例（Windows 对照已加进 winref 批次）。
- **crypt32 每个进程一串 ERR（`007d67a7f2f`）。** 宿主有一张验证不过的根证书，它就永远是“新的”，每个进程都把缓存里的证书检查
  一遍，Wine 自带的微软根证书等没有“来自宿主”的标记，逐个报 `CERT_FIRST_USER_PROP_ID property absent`。这是预期情形，静默跳过。
- **机器级状态的锁改用全局名字（`d12e09a4a9a`、`5af3095bcac`、`8bc8505e381`、`e9910d08dfc`）。** crypt32 的根证书导入、fusion 与
  sxs 的程序集缓存、winspool 的打印机初始化，保护的都是全机状态；服务挪到会话 0 后，不带 `Global\` 的名字在两个会话里是两把锁。
  chain 测试的 2 个失败改前就有（测试证书的日期），与此无关。
- **还缺的。** Wine 没有 Windows Search（SearchIndexer 服务、`searchapi`），Outlook 请求它的类 `{9E175B8B-F52A-11D8-B9A5-505054503030}`
  （MSPST32 里紧挨着目录名 “SystemIndex”）失败，即时搜索只能用 Outlook 的内置搜索；`IInternetProtocol` 的封送失败，Wine 与 SDK 的
  idl 里它都是 `local` 接口，Windows 上是否注册了代理待 winref 核实；WinRT 的 `Windows.UI.Composition.CompositionPath` 与
  `Windows.Foundation.Diagnostics.AsyncCausalityTracer` 还没有。

### WinVerifyTrust 的验证时间（10 月 2 日已改，见文末“签名与时间戳”）

`tools/wintrustprobe` 量的：Wine 的 wintrust 只认旧式副署（`1.2.840.113549.1.9.6`）里的签名时间，RFC 3161 时间戳
（`1.3.6.1.4.1.311.3.3.1`，如今微软的签名几乎都是它）完全不读；找不到时间时用的“系统时间”其实是**文件的创建时间**（2007 年
上游的做法）。于是 VSTO 运行时的 `VSTOInstaller.exe`（证书 2024-03 已过期，创建时间 2002 年）照样验证通过，按的是一个比证书
生效还早的时间。Windows 按时间戳的时间验证，没有有效时间戳就按当前时间，过期即 `CERT_E_EXPIRED`。Wine 这样比 Windows 宽，
眼下不挡 Office 的任何功能；要改成一样，得先实现 RFC 3161（时间戳令牌的签名、TSA 证书链、消息摘要与签名值的对应），否则会把
大批带时间戳的合法文件判成过期。探针已加进 winref 批次对照。

### Outlook：添加帐户成功；同步时 OST 损坏、崩溃（10 月 1 日用新构建重新同步未复现）

- **登录开关是按应用的。** 只给 `word` 设了 `IsWebView2ForOneAuthEnabled` 与 `DisableBrokerForOneAuth` 时，Outlook 添加帐户走
  mshtml（`BasicEmbeddedBrowser`），微软登录页报 Tag `4wp0s`、Code `-2146893805`（0x80090013，`NTE_FAIL`）。
  `scripts/enable-native-signin.sh` 改为给 word、excel、powerpoint、outlook 都设上；之后点“连接”，Outlook 直接用前缀里现有的
  Office 身份添加了 Outlook.com 帐户（“已成功添加帐户”），没有弹登录窗。完成页要先取消“在我的手机上也设置 Outlook Mobile”，
  否则会经 winebrowser 打开宿主的浏览器。
- **同步开始后崩溃。** 主窗口出现、OST 写到约 16.8 MB 后，Outlook 在 `MSPST32.DLL` 的 `BTH_Lookup+0xfde` 读空指针偏移 0xB2
  （`TC_SetRowWithPropCache → BTH_Insert → BTH_Lookup`，旁有 `NDB::XBAccess`、`ReadWriteLock::Lock`；崩溃前同一线程调了两次
  mlang 的 `SetMimeDBSource` 桩）。再次启动 20 秒就崩在 `ScSaveMessage+0x553`，栈上有
  `ScPstTraceScFn(DATAID_OLK_PSTCORRUPTED_VALUES)`：Outlook 自己判定 OST 已损坏。符号来自微软符号服务器的
  `mspst32.pdb`（`scripts/pe-pdb-id.py` + `scripts/pdb-addr2sym.py`）。字节范围锁的修复已经在新树里（`fdd5482020b`），排除。
  下一步：跟踪这个较新的 Tardis PST 引擎在 OST 上用到的文件操作（重叠 I/O、映射视图、分配大小与稀疏区、刷新），找出与 Windows
  语义不同的那一处。损坏的 OST 留在开发前缀里（邮箱的本地缓存），复现时需要先删掉它重新同步。
- 9 月 20 日那次首次启动同样以崩溃告终，所以本次一开始会问是否进入安全模式（选“否”）。
- **10 月 1 日重新同步：没有复现。** 删掉损坏的 OST，用当天的 dist-up（服务在会话 0、最终路径修复等都在内）以帐户的配置文件
  启动：OST 同样长到 16818176 字节后不再变（就是整个邮箱），状态栏“所有文件夹都是最新的。已连接到 Microsoft Exchange”，
  15 分钟没有崩溃；正常退出再启动，状态相同，也没有再判定 OST 损坏（上次重启约 20 秒就崩在 `ScSaveMessage`）。哪一处改动起了
  作用说不清：OST 在 AppData 下，与下面“文档”符号链接的那处无关，所以只能记为“未复现、根因未定”。
- **新 PST 的路径无效（altars-up `6cd0a144199`、`5f2aa5c05e9`）。** 用 `/PIM` 建只有 PST 的配置文件时 Outlook 报
  “C:\home\<用户>\Documents\Outlook 文件\…pst 文件的路径无效”。Wine 里用户的“文档”是指向宿主家目录的符号链接，
  `FileNormalizedNameInformation` 取的是服务端 `realpath()` 出来的名字，跟着符号链接走到了 Z: 上，而
  `GetFinalPathNameByHandle` 又把它接在打开时的 C: 后面，得到一个不存在的 `\\?\C:\home\<用户>\Documents`（`tools/finalpathprobe`
  逐项列出）。对 Windows 程序来说这种链接就是普通目录，不是重解析点：现在从打开时的名字重新逐段查找（它已经走过真正的重解析点），
  每段取磁盘上的大小写，不跟随宿主的符号链接；盘符照 Windows 用大写。kernel32 file、ntdll file 测试 0 失败。
  那次失败的尝试已在用户真实的 `~/Documents/Outlook 文件` 里建了 PST，已移走；测试时把 `ForcePSTPath` 设到前缀里的
  `C:\WineAltarsTest`。
- **PST 引擎加压（`tools/officeautomationprobe/outlook-pst.vbs`）。** 只用编造的数据：1000 条张贴条目 19.4 秒写完，PST 37 MB，
  全部能读回，没有损坏。有两处还要在 Windows 上对照：读回的正文每行末尾多一个空格（条目是 HTML 格式，纯文本转 HTML 再转回来时
  多出来的，可能是 Outlook 自己的转换）；新建邮件条目不论在哪个文件夹建，存盘都进了“草稿”。
- **对象模型防护提示（已解决，altars-up `7f4b3330a77`）。** 脚本读条目正文时 Outlook 弹“有一个程序正试图访问存储在 Outlook 中的
  电子邮件地址信息”，调用一直挂着等人回答。Windows 上只要 Windows 安全中心报告防病毒软件正常（Defender 一直在），Outlook 就不
  提示；Wine 原来没有 `wscapi.dll`，所以总是提示。现在 Wine 的安全中心报告宿主的 ClamAV（就是 AMSI 交给它扫描的那个
  clamd，见上文“安全中心”一节）：clamd 在应答、特征库不超过一周时，Outlook 不再提示；clamd 停了或特征库过期，提示照旧出现，
  这正是 Windows 上没有可用杀毒软件时的行为，不靠关策略。
- **自动化客户端退出时偶发崩溃（未解决）。** cscript 用完 Outlook 对象、脚本结束释放全局变量时，有时崩在 combase 的
  `ifproxy_release_public_refs`：代理管理器里的某个接口代理早已被释放，内存被字符串复用。只在 Outlook 刚做完一批写入、还忙的
  时候出现（一度 10 次 5 次），加了只记引用计数的跟踪后就不再出现，跟踪里的引用计数也完全正常；STA 的调用由
  `rpc_sendreceive_thread` 收发、主线程在模态循环里泵消息，疑为这期间的重入。不影响 Outlook 本身。

## 按 winref 实测对齐：安全中心、COM、媒体基础、墨迹与合成表面（2026-10-02，altars-up `23fa620648c`…`b9a41218b1b`）

10 月 1 日夜里的第 11 批（89 条命令）和今天的第 12、13 批，把前几天按文档写的几处逐项放到 Windows 11（build 29671）上对照，
凡是测到的都照 Windows 改，测试也改成断言测到的值；Windows 比 Wine 收得更紧、而 Wine 有理由更宽的地方，测试用 todo_wine
写明。

**安全中心（`ba249c4e867`、`5163567b70e`、`b49415181d2`、`5751bbdade2`）。**

- WSC 与 WMI 不是一一对应：WMI 叫 Defender “Windows Defender”，产品列表给本地化的“Microsoft Defender 防病毒”，GUID 相同
  （`{D68DDC3A-831F-4fae-9E44-DA132C1ACF46}`）；Windows 自带防火墙在产品列表里、GUID 是字符串 `"NULL"`，WMI 的
  `FirewallProduct` 里没有它；反间谍软件列表是空的（Defender 不算），反间谍提供方的健康照样 GOOD。wbemprox 的测试改为按 GUID
  配对、只数有 GUID 的产品；ClamAV 也只算防病毒。
- 返回码：不指定提供方的健康是 S_OK、GOOD；Initialize 之前 Count 是 OLE_E_BLANK、Item 是 E_INVALIDARG；Defender 不是
  “默认产品”；设置子状态“无需操作”，到期天数 ~0u。
- wscapi.dll 还以数据形式导出 `CLSID_WSCProductList`、`CLSID_WSCDefaultProduct`、`IID_IWSCProductList`、
  `IID_IWSCDefaultProduct`、`IID_IWscProduct`、`LIBID_wscAPILib`，链接 wscapi.lib 的程序从 DLL 导入它们，Wine 现在也导出；
  ProgID `wscAPI.WSCProductList(.1)` 也注册了。脚本在 Windows 上能建对象，但每次调用都是 0x8002801D：DLL 里的类型库写着
  2.0、注册的是 1.0。Wine 照样回答。Windows 把类注册成单元线程并给接口注册了代理；Wine 没有代理，仍是 both，不然 wbemprox
  的多线程单元就拿不到它。
- `root\SecurityCenter` 在 Windows 11 上还在：`AntiVirusProduct`、`AntiSpywareProduct`、`FirewallProduct` 三个类（各自的
  属性与 CIM 类型见 `tools/securitycenterprobe/results`），没有实例。Wine 原来连这个命名空间都没有，现在有了，查询给 0 个实例。

**`winmgmts:` 的三行 ERR（`23fa620648c`）。** Windows 上 WinMGMTS 的类对象同样没有 `IParseDisplayName`，而且在
`CLSCTX_ALL` 下也只答 E_NOINTERFACE；`MkParseDisplayName` 经 `IClassFactory` 拿到它，结果是指针 moniker。Wine 原来在进程内
服务器答了 E_NOINTERFACE 之后又去找本地服务器、远程服务器，最后报 REGDB_E_CLASSNOTREG，一路三行 ERR。现在进程内服务器的
回答优先，这类“没有那个接口”只记 WARN。wbemdisp 新测试 `test_class_object` 在 Windows 上通过。

**WMI 的脚本接口。** `SWbemProperty.IsArray` 原是桩，`CIMType` 遇到数组就失败，脚本逐个列属性时每个都报 445；现在数组给
元素类型、IsArray 为真（`11ca5c8d0d1`，`ChassisTypes` 与 `Manufacturer` 在 Windows 上对照过）。类路径给类本身、
`Path_.IsClass` 与 `SystemProperties_` 后来按 Windows 补上了，见“WMI 路径、对象路径与脚本对象”。

**媒体基础的工作队列（`2730b306e7b`）。** Windows 的工作队列线程自己进入多线程单元（长函数队列进入它自己的单线程单元），
上游 mfplat 测试 `test_queue_com_state` 一直这么写着 todo_wine。Wine 的线程只在启动时建的隐式 MTA 里。现在 rtworkq 的工作
线程第一次执行任务时进入相应的单元，留在里面直到线程退出；九个队列的子测试全部通过。

**传感器活动监视器（`6ab3cc49dfb`）。** `MFCreateSensorActivityMonitor` 先看 COM（没初始化是 CO_E_NOTINITIALIZED，输出不动），
再看媒体基础（没 MFStartup 是 MF_E_SHUTDOWN，即那时分配工作队列的回答，输出清零），然后才看参数（没有回调是 E_INVALIDARG）；
重复 Start 是 MF_E_INVALIDREQUEST；报告从媒体基础的多线程队列来。测试在 Windows 的两个会话里 78 项 0 失败。

**墨迹（`d3f15163f46`、`f90bbb1127f`、`b75cca840e0`、`b9a41218b1b`）。**

- RealTimeStylus 只收自由线程的同步插件：它先问插件要封送器（IStdMarshalInfo、IMarshal），不是自由线程封送器就 E_INVALIDARG，
  连 DataInterest 都不调；StrokeBuilder 只是异步插件。没有窗口时启用 0x80280005、启用中换窗口 0x80280006，启用前没有平板上下文，
  全部平板模式下 `GetTablet` 给 S_OK 和空指针。
- InkDisp：`GetBoundingBox` 的矩形右下开区间，笔宽一半向下取整向外扩（1000..3000 的点在默认 53 宽的笔下是 974..3027）；
  `ScaleToRectangle` 保留笔宽，把点映射进内缩半个笔宽的矩形，0.5 取偶；X/Y 的默认度量是 INT_MIN..INT_MAX；不是 ISF 的流
  （版本不对、长度超出）是 E_UNEXPECTED，内容坏是 E_INVALIDARG；空数组 Load 什么也不做；不认识的保存格式照 ISF 存；
  空墨迹存 GIF 是 E_UNEXPECTED；字符串数组不能当扩展属性；按钮位不在 GetPacketData 里。
- 鼠标拖动（`rtsprobe drag`，第 15 批，`9b92b002e77`、`73a3972c421`）：Windows 交给同步插件的包同样是相对窗口客户区的
  himetric（客户区 40,40 是 1058,1058），与 Wine 一致；次序是 StylusInRange、按下点的 StylusDown、**同一点再一个 Packets**、
  每次移动一个 Packets、StylusUp，回调在 RTS 自己的线程上。StrokeBuilder 的笔画是按下点加全部包，首点两次、末点一次。
  Wine 原来按下后不发 Packets、StrokeBuilder 抬起时又补一个末点，现在与 Windows 逐点相同。
- Base64 两种格式在 Windows 上 Save 出的是字符串：`base64:` 加编码，长度把结尾的 null 也算上；编码的流头部之后先记一个
  持久化格式标签（tag 28，base64 是 0x10000，GIF 注释里的是 1）。Load 直接收这个字符串；往有笔画的墨迹里再 Load 是
  E_INVALIDARG。两份 Windows 自己存的 ISF（带三处、四种类型的扩展属性）作为测试样例，Wine 解出的与 Windows 报的一致。
- 还没跟上的：鼠标“平板”的描述在 Windows 上用屏幕像素作逻辑范围、按显示器物理尺寸给分辨率，并有平板对象
  （`\\.\DISPLAY1`）；Wine 用 himetric、`GetTabletFromTabletContextId` 是 E_NOTIMPL（包本身的单位两边相同）。DynamicRenderer、GestureRecognizer 两个类 Wine 没有。
  GIF 持久化没有实现。CreateStroke 在 Windows 上拒绝任何包描述，但 rtscom 的 StrokeBuilder 要靠它建带压力的笔画，Wine 仍接受；
  缺 X 或 Y 的笔画在 Windows 上取不到点。两条手写的 ISF 样例（LZ 压缩的属性、扩展变换）Windows 解出的不同，要换成 Windows
  自己编出的样例（`tools/isfprobe` 的输出）。

**Excel 的合成绘制表面（`23afc3b16f3`）。** `tools/drawsurfprobe` 第一次在 Windows 上全是 E_INVALIDARG，原因是新表面的
第一次绘制必须覆盖整张表面，而探针先只画一角；改成先整张画一次之后才测到真实的状态机：同一表面再 BeginDraw，不论挂起
没有，都先结束手上那次（AirSpace 的“画、挂起、再画、再画”三块都留下）；同一时间只画一张表面；绘制中 Resize 结束这次绘制；
次序不对的调用是 COR_E_INVALIDOPERATION（0x80131509），不是 DirectComposition 的错误码。dcomp 照此改，新测试
`test_drawing_surface` 走一遍这些状态，在 Windows 的桌面会话里通过（第 14 批；同一文件里早先的测试在 29671 上另有 5 处
失败：效果工厂刚建好时 LoadStatus 是 Pending（还在编译），激活工厂的 GetRuntimeClassName 是 WRL 的 E_ILLEGAL_METHOD_CALL、
不动输出——`4a8e1032545` 照此改，四个工厂一样；其余两处是交互跟踪器，见文末）。winref 下午换到了本机所在的局域网（192.0.2.10，sshd 在 22 端口）。改后 Excel 打开工作簿、关闭都正常；
诊断日志里这次没有 AirSpace 事件，当初每次启动约 34 次的 CompositionErrorActivity 没法在本机复核。

**只记下、没有改的。**

- 最新 Windows 对末尾带反斜杠的文件名给 STATUS_NOT_A_DIRECTORY（ERROR_DIRECTORY），上游 ntdll/kernel32 的 file 测试因此各有
  一两处失败；与 Office 无关。——已照做，见“文件名与属性”一节。
- `GetFinalPathNameByHandle` 的“打开时的名字”、`FileNameInformation` 和 `NtQueryObject` 在 Windows 11 上也给磁盘上的大小写，
  `NtQueryObject` 给 `\Device\HarddiskVolumeN\...`；Wine 给打开时的大小写和 `\??\C:\...`。——大小写已照做（同一节），
  `\Device\HarddiskVolumeN` 还没有。
- WinVerifyTrust 按 RFC 3161 时间戳的时间验证（VSTOInstaller.exe 按 2023-07-03）——已照做，见下一节。

## 签名与时间戳：RFC 3161、副署与 Authenticode 策略（2026-10-02，altars-up `b676d2c2087`…`c2a255dc096`）

Office 的二进制几乎都是微软用一年期证书签的，签名里带 RFC 3161 时间戳（未认证属性 `1.3.6.1.4.1.311.3.3.1`，值是一个
CMS 签名的时间戳令牌，签的是 TSTInfo，TSTInfo 里的消息印记是签名者加密摘要的哈希）。证书过期之后，Windows 按时间戳的时间
验证签名。`tools/wintrustprobe` 在 winref（第 16、18、20、21 批）量到的：

- VSTOInstaller.exe（证书 2024-03 过期）：WinVerifyTrust 为 0，签名者按 2023-07-03 07:14:34.074 验证（毫秒都在）；时间戳
  本身作为一个副署者出现，类型 0x10（SGNR_TYPE_TIMESTAMP）、证书链三张、链状态 0、验证时间相同。旧式副署
  （`1.2.840.113549.1.9.6`，msdia100.dll、2015 年的 api-ms-win-crt）同样是类型 0x10 的副署者。
- 把它的时间戳从签名里剪掉（`VSTOInstaller-notimestamp.exe`，签名本身仍有效），WinVerifyTrust 是 CERT_E_EXPIRED，按调用时的
  当前时间验证；再把文件的创建时间设到证书有效期内，结果不变——Windows 不看文件时间。
- `CryptVerifyTimeStampSignature`：返回的上下文是一块内存，装着整个令牌和解码好的 TSTInfo；签名者证书从令牌自带的证书里找，
  返回的证书存储里只有证书（微软令牌里那张 Thales TSS 的属性证书不算）；数据不对是 NTE_BAD_HASH；不给数据就不查印记；
  截短一字节是 CRYPT_E_ASN1_EOD；把 TSTInfo 当令牌传是 CRYPT_E_ASN1_BADTAG；TSTInfo 在签名后被改过是 CRYPT_E_HASH_VALUE。
- `TIMESTAMP_INFO` 的解码（`tools/wintrustprobe/tstinfoprobe`，手工构造的 TSTInfo 每个可选部分单独出现）：序列号与 nonce 按
  小端，TSA 名字给 GeneralName 的编码，时间精确到毫秒（多出的位数截掉，“.5”是 500 毫秒），大小与 crypt32 惯常的 8 字节对齐
  一致；**精度字段无论哪种写法，Windows 都为它留出空间，却始终把 `pvAccuracy` 置空**。`TIMESTAMP_INFO` 也能编码（原样往返），
  `TIMESTAMP_REQUEST` 编码、`TIMESTAMP_RESPONSE` 解码。

Wine 原来的情形比“没实现时间戳”更糟，几处缺陷叠在一起：

- wintrust 根本不读 RFC 3161 时间戳；找不到时间时用的是**文件的创建时间**（2007 年起），旧式副署的时间未经验证就采用；
  `CryptMsgVerifyCountersignatureEncoded(Ex)` 是桩。
- **Authenticode 策略对 SHA-256 签发的证书根本不执行**：取证书签名哈希（禁用列表检查要用）的缓冲区是 20 字节，SHA-256 放不下，
  失败后连链策略一起跳过且不报错——如今所有证书都是 SHA-256 签的，于是过期、根不受信、自签名一律通过；那个哈希还被当成字节
  而不是 `CRYPT_HASH_BLOB` 传给 `CertFindCertificateInStore`，禁用列表也从没起过作用。
- crypt32 的签名消息对“非 data”内容整段（连标签和长度）做哈希、整段返回，令牌里的 TSTInfo 取出来还包在 OCTET STRING 里；
  而且**从不比较 messageDigest 属性与内容的哈希**——签名只覆盖认证属性，内容换成什么都能通过（Authenticode 的
  SpcIndirectData 也一样）。
- 数组解码器跳过不认识的元素时不计入已解码长度：微软令牌的证书集合最后是一张属性证书（`[1]`），于是它被当成 CRL 集合，
  解码失败，消息的证书存储也就打不开。
- DER 长度超出数据时，短格式差一个字节、长格式（计数器在读长度字节时已用完）都不报 EOD，后面才报 CORRUPT。
- GeneralizedTime 的小数秒按“最多三位的毫秒数”读：“.5”是 5 毫秒，不足三位时把 Z 当数字读而失败。
- UTF8String 解码时结尾的 null 写在两倍长度处（越界），可写长度按“字节数减 CERT_NAME_VALUE 的大小”算，会变成负数。

改动（每个提交的说明里写着对应的实测）：`b676d2c2087` 长度检查；`96e480a73ce` 数组跳过元素；`7ccd430404b` UTF8String；
`128fb6acc9c` 小数秒；`9dd82e22dc6` RFC 3161 的四种结构（测试即 Windows 的回答，Windows 上 0 失败）；`c8f9069017e` 签名
内容按 PKCS #7/CMS 取值做哈希、CMS 内容去掉 OCTET STRING 返回、检查 messageDigest（缺了是 CRYPT_E_AUTH_ATTR_MISSING，
不符是 CRYPT_E_HASH_VALUE）；`9e089d64e7c` 副署验证；`32700d9c888` `CryptVerifyTimeStampSignature`（`CryptRetrieveTimeStamp`
要联网向时间戳机构请求，仍是桩）；wintrust `41f0190dd40` 按调用时间而不是文件时间、指定了验证时间的证书按那个时间；
`b77f6c65567` 两种时间戳都作为副署者保存、各自验证、各自按时间戳时间建“时间戳签名”用途的链，签名者按第一个签名与链都成立的
时间戳的时间验证，否则按调用时间；`c2a255dc096` 策略检查。之后 `tools/wintrustprobe` 在 Wine 上与 Windows 逐行相同
（`results/*.wine.txt` 对 `*.win.txt`），只差下面这一处。

**根证书存储的缺口（待定）。** `tools/wintrustprobe/trustscan` 扫了前缀里 Office、共享组件和 OfficePLUS 的 1705 个二进制
（`results/trustscan.wine.txt`）：改前 1663 个通过，改后 1657 个——多出的 6 个是 CERT_E_UNTRUSTEDROOT，都是第三方签名的 .NET
库（.NET Foundation 的 OpenXML SDK、Json.NET、Windows Community Toolkit，Azure 签的 OpenTelemetry），根是 DigiCert High
Assurance EV Root CA、DigiCert Assured ID Root CA、DigiCert CS RSA4096 Root G5、Microsoft RSA Services Root CA 2021。宿主的
ca-certificates（Mozilla 的 TLS 根）已不再收前两个，后两个从来只用于代码签名。改前它们“通过”只是因为策略根本没执行。winref 上这些根
在 `LocalMachine\AuthRoot` 里（36 张，Root 里 51 张）：那是 Windows **自动根证书更新**按需下载的——
`HKLM\SOFTWARE\Microsoft\SystemCertificates\AuthRoot\AutoUpdate` 里存着 206718 字节的 authroot.stl（微软签名的 CTL，
列出受信根的 SHA-1 指纹与用途）和同步时间；链到一个不在存储里的根时，crypt32 查这张 CTL，在列就从 `ctldl.windowsupdate.com` 取证书放进 AuthRoot。Wine 没有这个
机制。要与 Windows 一致就得实现它（运行时会联网访问微软的服务器，这是要用户定的事），或者随 Wine 带一份 CTL 快照。这 6 个库
在 .NET 里加载时不验 Authenticode，Office 的保存回归（Word、Excel、PowerPoint 新建、编辑、保存、退出）在新构建上全部通过。

那 16 个 0x3 是 C2R/AppV 的符号链接占位文件（Wine 的重解析点），1 个 0x800b0003 是 0 字节的 eqnedt32.exe，改前改后一样。

## 交互跟踪器按 Windows 逐帧对齐（2026-10-02，altars-up `5a9354331e5`、`8c771a2ab5c`）

winref 的桌面会话能出合成帧之后（第 17、23、24 批），`tools/trackerprobe` 的 `inertia.c`、`clamp.c`、`requestid.c` 逐帧记下了
InteractionTracker 的行为（`results/*.win.txt`；规律与拟合见该目录的 README）。与 Wine 原来按文档写的出入：

- **惯性**：每帧按 `v/k·(1−e^(−kt))` 前进（刷新率 60.03 Hz，拟合到千分之一像素），速度低于 30 像素/秒就停，所以自然终点是
  `(v−30)/k` 而不是 `v/k`（v=200 时差 10 像素）；缩放线性滑行、低于 0.05/秒停、速度上限 5。InertiaStateEntered 总带修饰后的
  终点（没有修饰器时是夹进边界的自然终点）；请求加的速度不算“冲量”。
- **越界**：惯性冲过边界后由阻尼弹簧（约 ζ 0.9、ω 15 rad/s）拉回、停在边界上；Wine 原来把曲线压缩成正好停在边界。
- **自定义动画**不受边界约束：动画到 5000（上界 1000）就真到 5000，然后以同一请求号进入惯性（速度 0、自然终点即当前位置、
  修饰终点为零向量）弹回边界。Wine 原来每帧夹住。
- **空闲时的钳位**：位置请求无论 ClampingOption 是否禁用都立即夹进当时的边界，**之后边界移开也不回去**；边界推动空闲跟踪器时
  以最近一次请求号报 ValuesChanged。Wine 为 Word 就地激活加的“保留不钳位请求的位置”与此相反（Office 的情形 Windows 停在
  899，Wine 停在 900）。撤掉之后连同其余改动重跑 `tools/officeautomationprobe/embedevents`：Excel 的 EXCEL9 在 Word 滚动动画
  隐藏后照样再显示、此后一直可见，就地激活的工作表画在文档里（截图核对过）——当初那 1 像素的差并不是 Word 卡住的真正原因。
- **默认缓动**：不带缓动函数插入的关键帧，Windows 按三次贝塞尔 (0.41, 0.52)、(0, 0.94) 缓出；把每帧进度按这条曲线反解，
  得到的帧时间正好每帧 17 毫秒。Wine 原来按线性。
- **请求号**：缩放上下界、两种衰减率、惯性修饰器、位置调整（AdjustPosition…）各占一个请求号，位置上下界、中心点修饰器不占。

改后三支探针在 Wine 上与 Windows 的状态、请求号、自然与修饰终点、停止位置一致（`results/*.wine.txt`），逐帧数值只差在帧率
（Wine 约 58.5 Hz）。dcomp 测试加了越界、弹回与终点的断言，Windows 桌面会话 612 项 0 失败，Wine 617 项 0 失败；三应用保存回归
通过。改的过程中还发现并修掉：空闲钳位若在帧里再要求一帧，会让合成器帧接帧地空转（边界动画期间一秒上百次回调）。

## WMI 路径、对象路径与脚本对象（2026-10-02，altars-up `f156a72b9ec`、`3079386d816`、`b3999412b70`）

起因是脚本里的 `Path_` 几乎全是桩（`IsClass`、`Keys`、`Server`… 都是 E_NOTIMPL），`SystemProperties_` 不存在。第 25～30 批在
winref 上把三层都量了一遍（`tools/winmgmtsprobe`，README 有每支探针的结论），按量到的补齐：

- **wmiutils 的路径键**：键值有类型——双引号或单引号字符串（反斜杠只转义引号和反斜杠，`\t` 之类整条路径无效）、十进制数
  （放得下就是 sint32，负数放不下是 sint64，否则 uint64）、十六进制（一律 uint64）、true/false、花括号里的引用；空格、空名、
  `+5`、`1.5`、未闭合的引号都使整条路径无效。引号里的逗号、冒号、反斜杠属于值——Wine 原来见到值里的反斜杠就把它当命名空间，
  `Class.A="back\\slash"` 会解析错。写回文本时数字一律十进制（`0x10` 变 `16`）、布尔小写、字符串双引号转义。没有命名空间的路径
  前面不带冒号（Wine 原来给 `:Win32_LogicalDisk…`、`\\.:…`）；有命名空间才补 `\\.`。`GetKey`/`GetKey2`/`SetKey`/`SetKey2`/
  `RemoveKey`/`MakeSingleton`/`GetInfo`/`GetText` 原来全是桩。`GetKey2` 是把 `GetKey` 给的字节按键的类型读（所以带文本标志读
  数字键会读出字符码，Windows 就是这样）；`SetKey2` 给数字类型传 BSTR，读的是字符串的字节（`"7"` 当 uint32 读成 55）。
- **wbemprox**：类路径给类本身（genus 1、属性全 null），不存在的类 WBEM_E_NOT_FOUND（Wine 原来在这里空指针崩溃）；系统属性按
  Windows 的顺序，补了 `__SUPERCLASS`/`__DYNASTY`（Wine 没有类层次，给 null）。**查询结果的路径**：原始 COM 的 `ExecQuery`
  只要列了属性，`__PATH`/`__SERVER`/`__NAMESPACE` 就是 null，列全了键才有 `__RELPATH`（单例总有 `Class=@`）；
  `WBEM_FLAG_ENSURE_LOCATABLE` 把键补进投影并给出完整路径——而脚本层的 `ExecQuery` 不论传什么标志都带它，所以脚本拿到的对象
  总有键和路径。数字字面量与字符串属性按数字的文字比较（`Win32_Process WHERE Handle = 4`，Wine 原来查不到）。结果的命名空间
  按所在命名空间给：`ROOT\SecurityCenter2` 的杀毒产品原来被写成 `ROOT\CIMV2`，按 `__PATH` 再 `Get` 就落到错的命名空间；完整
  路径里的服务器可以写 `.`（Wine 原来只认计算机名）、命名空间按连接的比较。设备类的键拼成 `DeviceID`（`MSFT_PhysicalDisk` 本来
  就是 `DeviceId`，不动）。`IWbemContext` 的名字表、枚举、删除原来是桩；按测到的语义实现（插入顺序，删不存在的名字回 S_FALSE，
  枚举外 `Next` 是 WBEM_E_UNEXPECTED）。
- **wbemdisp**：`SWbemObject` 是 `ISWbemObjectEx`，`SystemProperties_` 按 `__PATH` 在前、`__GENUS` 在后列出。
  `WbemScripting.SWbemObjectPath` 可以创建，所有属性和设置器按 wmiutils 的路径实现，不成路径的文本返回 WBEM_E_FAILED 且不改
  原值；对象自己的 `Path_` 每次新建、只读（WBEM_E_READ_ONLY）。`Keys` 是取出时的副本，改它会写回路径（改某个键的 `Value`
  会写回然后**报 WBEM_E_FAILED**——Windows 就是这样）。`DisplayName` 按
  `WINMGMTS:{authenticationLevel=…,impersonationLevel=…,authority=…,(特权,!特权)}[locale=…]!路径` 只写设置过的部分，
  读入也按这个语法；独立路径的安全级别未设置时读取报 WBEM_E_FAILED。定位器、连接、对象的安全设置现在各自保留（原来每次
  `Security_` 都新建一个默认对象，设了也白设）：连接继承定位器的，对象和它的路径继承连接的；`winmgmts:` 名字对象的
  `{…}`、`[locale=…]` 也真正生效。特权集合按编号 1～27 保存和枚举，`Add` 越界 WBEM_E_INVALID_PARAMETER，`AddAsString` 只认
  `Se…Privilege` 全名。`SWbemNamedValueSet` 的计数、枚举、克隆、删除、命名值的读写原来都是桩。`Methods_.Count` 补上。

测试：wmiutils 1412 项、wbemprox services 91 项、wbemdisp 431 项（Wine 371 项）在 Windows 上都是 0 失败，新加的
`test_locatable` 在 Windows 通过（query 测试在 winref 上的其余失败是机器相关的老测试）；Wine 上四组全过。探针对照：`pathprobe`
只剩 Windows 未初始化的字节，`contextprobe`、`pathobject.vbs` 完全相同。Office 三应用保存回归通过。

随后补上（altars-up `1929d3edcbb`…`e47e1b979c4`，第 31～35 批）：

- **类名与错误详情**：每个脚本对象都答 `IProvideClassInfo`（`TypeName` 给 `SWbemServicesEx`、`SWbemObjectEx`、
  `SWbemNamedValue`…，这些 coclass 都进了类型库），连接对象是 `ISWbemServicesEx`（`Put` 20、`PutAsync` 21）。脚本里的调用
  失败时，`Err.Description` 是 wmiutils 的状态文本、换行变空格（“找不到 ”），`Err.Source` 是失败的类名——与 Windows 逐字一致。
  `ExecQuery` 默认带 `wbemFlagReturnImmediately`，查询错误推迟到枚举时才报、且不带描述；对象集合的 `Item`、`Security_` 补上。
  `SWbemLastError` 注册了，没有扩展状态时创建返回 E_FAIL（Windows 同样）。
- **wmiutils 的状态文本**：`IWbemStatusCodeText` 原来只回“Error code: 0x…”。现在按 winref 上 en-US 与 zh-CN 的
  wmiutils.dll.mui 读出的 189 条消息生成消息表和中文翻译；WMI 以外的码用系统消息，只接受 lcid 0，
  `WBEMSTATUS_FORMAT_NO_NEWLINE` 把换行变空格，设施名按设施码给 `WMI`/`Interface`/`Win32`/`<Null>`/`General`…
- **VBScript**：`GetObject` 原把任何解析失败都改成 MK_E_SYNTAX，现在对象解析器自己的错误原样给脚本（WMI 的 8004100E）；
  `CreateObject` 在没有安全管理器的宿主（cscript）里给出类工厂自己的错误（E_FAIL），并补上漏掉的类工厂释放。
- **特权**：`LookupPrivilegeDisplayName` 原是桩；按测到的长度约定实现，中英文文本都是 Windows 的（中文逐条实测）。
  `LookupPrivilegeValue` 原来只认到第 30 号，补到 36 号（`SeCreateSymbolicLinkPrivilege` 等），失败时 LUID 清零；
  wineserver 的管理员令牌补上 Windows 管理员有的 33～36 号（默认禁用），`AdjustTokenPrivileges` 启用符号链接特权因此成功。

测试：wbemdisp 469 项、wmiutils 1450 项在 Windows 上 0 失败，advapi32 新测试在 Windows 通过；Wine 上各组全过，vbscript 的
createobj/run/vbscript 也全过；Office 三应用保存回归通过。

仍差的：实例路径里的命名空间 Windows 照搬连接时的拼写（`root\cimv2` 或 `ROOT\CIMV2`），Wine 一律 `ROOT\CIMV2`；通过 vtable
直接调用的 C++ 客户端在 Wine 里拿不到 `IErrorInfo`（脚本经 `Invoke` 拿得到）；`SWbemLastError` 的扩展状态要 WMI 核心支持；
对不存在的类 Windows 报 WBEM_E_INVALID_CLASS，Wine 有意返回空结果；系统消息表（kernelbase 的 winerror.mc）措辞与覆盖面
与 Windows 不同，许多 COM/RPC 码没有文本；Wine 的 WMI 类表本身比 Windows 少属性和方法。

## 文件名与属性：末尾反斜杠、磁盘大小写、存档属性（2026-10-02，altars-up `b6be22fee26`）

这一节的结论由三个探针在 winref（Windows 11 build 29671）上测得：`tools/finalpathprobe/trailingprobe`（第 36 批）对每个文件函数
试“文件名后跟反斜杠”（`f.txt\`）、“把文件当目录用”（`f.txt\x`）、“新名字后跟反斜杠”（`n.txt\`）；`finalpathprobe` 用不同
大小写打开同一个文件（第 36 批）；`tools/fileattrprobe/archiveprobe` 测存档属性何时回来（第 38 批）。结果都存在各自的
`results` 里，Windows 与 Wine 逐行对照。

**末尾反斜杠。** Windows 11 上，文件名后面跟反斜杠就不再是目录名：打开、新建（连 `CREATE_NEW` 也是）、查属性、复制、移动、
删除，一律 STATUS_NOT_A_DIRECTORY（ERROR_DIRECTORY，267）；只有“用它建目录”是 STATUS_OBJECT_NAME_COLLISION。新名字后跟反斜杠时，
除了目录什么也建不出来；“把文件当目录用”是路径不存在。Wine 原来给 STATUS_OBJECT_NAME_INVALID（ERROR_INVALID_NAME，123），
`GetFileAttributes` 还给 3——这是 Windows 11 以前的行为，上游测试里已有 Windows ARM64 测试机给出新码的痕迹。现在 ntdll 逐段查找
时，若最后一段后有反斜杠而找到的是文件，就回 NOT_A_DIRECTORY（符号链接指向“文件\”也一样）；新建时名字末尾有反斜杠而不是建目录，
同样回 NOT_A_DIRECTORY。上游测试中预期旧码的地方（ntdll path 的 `test_nt_names`、ntdll file 的符号链接、kernel32 的
`_lcreat("testfi/")` 和 `CreateSymbolicLink` 到“…\”）改成以新码为准、旧码记为 broken——这些正是 Windows 11 上原有的失败。
scrrun 的 `MoveFolder` 靠“源名加反斜杠再移动”判断源是不是文件夹，原来只认 ERROR_INVALID_NAME，现在也认 ERROR_DIRECTORY
（Windows 上 scrrun 测试同样 0 失败）。

**只读访问的 supersede。** 探针顺带测出：`FILE_SUPERSEDE` 只要读权限也会把文件清空成新文件，只读文件也照样替换（上游测试
早就写着只读文件可被 supersede）。Wine 原来按只读方式打开、`ftruncate` 静默失败，文件原样留着。现在 wineserver 遇到“截断但没有
写权限”的打开，临时给属主加写位、另开一个可写描述符截断、再还原权限；新属性随后由 ntdll 设上。

**打开时的名字取磁盘上的大小写。** Windows 的 `FileNameInformation`、`NtQueryObject` 与 `GetFinalPathNameByHandle(FILE_NAME_OPENED)`
都由文件系统按磁盘上的名字拼出：`casedir\mixedcase.txt` 打开，三者都是 `CaseDir\MixedCase.txt`；用短名打开则保留短名、
按磁盘上的存法大写（`\PROGRA~1\COMMON~1`）。Wine 原来把调用者给的名字原样交给 wineserver，三者都是打开时的写法。现在
`NtCreateFile` 在交给服务器之前，把 NT 名逐段换成 Unix 名里找到的磁盘写法（长度相同、不区分大小写相等时），短名大写，盘符大写。
**模块名不动**：Windows 的 `GetModuleFileName` 保留 `LoadLibrary` 时的写法，所以只改交给服务器的那份名字，不改
`get_nt_and_unix_names`（加载器也用它）。kernel32 file 测试里一处查符号链接打开名的 todo_wine 因此通过。

**存档属性与其他属性（Windows 的规则，全部实测）。**

- 没有任何属性的文件，`GetFileAttributes`、`FindFirstFile`、`FileBasicInformation` 都报 `FILE_ATTRIBUTE_NORMAL`（0x80）；隐藏、
  只读、临时各自单独成立（0x2、0x1、0x100）。目录只有在设置后才带存档属性（0x30），并一直保留。
- 存档属性被清掉之后，这些操作会让它回来：改大小（立即）、改名、覆盖或截断、写备用数据流、建硬链接，以及写入数据——写入要到
  flush 或关闭之后才看得到。只打开写不写、读、改时间都不会。复制出来的新文件带它，原文件不变。
- Wine 原来：文件一律报 0x20，xattr（`user.DOSATTRIB`）只存隐藏和系统；目录不能带存档属性；临时属性存不住；还有一个次序 bug——
  先 `fchmod` 去写位、再删 xattr，而 `user.*` xattr 的增删要写权限，所以“隐藏文件改成只读”后仍然隐藏。
- 现在：xattr 里存隐藏、系统、存档、临时、脱机、不索引；没有 xattr 时文件默认带存档、目录不带；存的值等于默认值就删掉 xattr。
  改 xattr 在改权限之前做，只读文件临时加属主写位。新建、覆盖、supersede 的文件总带存档属性；写入、改大小、改名、建硬链接后，
  若 xattr 记着“已清除”就把存档属性加回来。解析不了的 xattr（Samba 4 的二进制格式）一律不改写，以免毁掉 Samba 的元数据。
  上游 ntdll 测试里 5 处相关的 todo_wine 因此通过（`FILE_ATTRIBUTE_NORMAL`、只有系统、只有隐藏、带重解析点的隐藏+系统）。
- Office 回归里 Excel 和 PowerPoint 存出的文件现在带 `0x2020`（存档 + 不索引）：Office 建文件时自己要的属性，以前被丢掉。

**其他顺带补上的。** `SetFileValidData`（`FileValidDataLengthInformation`）要求启用 `SeManageVolumePrivilege`，否则
STATUS_PRIVILEGE_NOT_HELD；检查放在取得句柄之后、看长度之前（Windows I/O 管理器的次序）。winref 上 ssh 会话的令牌所有特权都已启用，
所以那里得到的是 87，测试记为 broken。`GetVolumePathName` 保留盘符的写法（`c:\windows` 在 `c:\` 上），原来一律改成大写；
上游测试的对比本就允许大小写不同，预期值改成 Windows 11 的。advapi32 已认识 `SeCreateSymbolicLinkPrivilege`，ntdll 与 kernel32
测试里两处查这个特权的 todo_wine 去掉。

测试：Windows 上 ntdll file 4153 项、ntdll path 1012 项、kernel32 file 572811 项、kernel32 volume 875 项、scrrun filesystem
8940 项全部 0 失败（新加的末尾反斜杠、supersede、名字大小写、存档属性测试都在内）；Wine 上 ntdll file/directory、kernel32
file/directory/volume、shell32 shlfileop、scrrun、setupapi、cabinet、advpack、msi（install/package/action）、shlwapi path 全过。
探针对照：`trailingprobe` 与 Windows 逐行相同；`archiveprobe` 只差两行（见下）。Office 三应用保存回归通过。

仍差的：

- 写入后存档属性立刻可见，Windows 要等 flush 或关闭：要做到一样，需要在句柄上记“已修改”，而 ntdll 的句柄缓存 64 位已满，
  为这一点时差另建一张表不值得。
- Wine 没有备用数据流：`a.txt:s` 在宿主上是一个名为 `a.txt:s` 的独立文件，写它不影响 `a.txt`。Office 的“网络标记”
  （`Zone.Identifier` 流）因此也落不到文件上——这是一个独立的大缺口。
- `NtQueryObject` 给 `\??\X:\…`，Windows 给 `\Device\HarddiskVolumeN\…`；`GetMappedFileName`、`GetProcessImageFileName` 也一样
  （上游测试为此挂着 todo_wine）。改它要动 wineserver 的对象名和 kernelbase、dbghelp、advapi32 里解析这个名字的地方，单独做。
- Windows 式短名（`PROGRA~1`）在 Wine 里打不开：Wine 的短名由长名散列得出（`PROGR~xx`）；前缀里 `windows`、`system32` 在
  磁盘上是小写，许多 Unix 侧代码按小写精确查找，不改。

## Windows 拼写检查 API：msspellcheckingfacility 与宿主的 Hunspell（2026-10-02，altars-up `72ac49b431d`、`604ce6dbfa9`）

Word 每次启动都会 `CoCreateInstance(CLSID_SpellCheckerFactory)`（`{7ab36653-…}`，Windows 上是 `MsSpellCheckingFacility.dll`，
自由线程），取 `IUserDictionariesRegistrar`，用**空语言标签**注册 Office 的自定义词典
`%APPDATA%\Microsoft\Office\16.0\<id>\Proofing\RoamingCustom.dic`。Wine 原来没有这个类，这是 Word 启动日志里唯一的
ERR。逐项按 `tools/spellprobe`（第 42 批）、`regdict`（第 45 批）在 Windows 11 build 29671 上量出的行为实现：

- **语言**：Wine 列出宿主 Hunspell 的词典（`/usr/share/hunspell` 等与 `DICPATH`，`en_US` 记作 `en-US`），按标签排序；
  `IsSupported`/`CreateSpellChecker` 标签不分大小写，不做任何近似（`en`、`en_US`、前导空格都不行），空标签 E_INVALIDARG
  （答案清零），NULL E_POINTER；检查器的 `LanguageTag` 原样返回创建时的写法。`Id` 是 `MsSpell`，`LocalizedName` 按用户
  语言（“Microsoft Windows 拼写检查器”），没有选项（未知选项 E_INVALIDARG、值清零）。
- **查错规则**：全大写、含数字的词、URL 与邮件地址不查；撇号（`'` 与 `’`）和连字符连成一个词，复合词只报错的那段；
  与前一个词只隔空格、大小写不论、且前一个词拼对了，就是重复（DELETE，从第二个起）；有更正的词是 REPLACE（按原样
  大小写匹配）；拼错是 GET_SUGGESTIONS，`ComprehensiveCheck` 在只有一个建议时直接给 REPLACE。没有替换词的错误给空串。
  中文等非拉丁文字在英文检查器里算拼错。`Suggest` 最多 10 个，有更正的词只给更正。
- **用户词表与 Windows 同一套文件**：添加的词在 `%APPDATA%\Microsoft\Spelling\neutral\default.dic`（所有语言共用），
  移除的词（词典里的词也可以）在 `<语言>\default.exc`，更正在 `<语言>\default.acl`（`词|更正`），UTF-16 带 BOM；
  忽略只对该检查器有效。文件变了就重读，修改时持跨进程互斥量“读、改、写”。变化事件：添加 1 次，移除与更正各 2 次
  （与 Windows 计数一致）。
- **注册用户词典**：任何语言标签都接受，空标签表示所有语言（Office 就这样用）；重复注册/注销 S_FALSE；文件不存在或扩展名
  不是 .dic/.exc/.acl 为 E_INVALIDARG；按语言的注册与 Windows 一样存在 `HKCU\Software\Microsoft\Spelling\Dictionaries`
  （值名是语言，REG_MULTI_SZ 是路径），空标签的不保存（Windows 也不保存，Office 每次启动重新注册）。
- 实现：PE 侧做分词、规则与词表，Unix 侧 `dlopen` libhunspell（1.7/1.6/1.5），参数结构定宽，32 位程序共用同一张表；
  没有 Hunspell 或词典时不支持任何语言。接口在 `include/spellcheck.idl`，IID 进了 libuuid。

测试：新的 `msspellcheckingfacility` 测试在 Windows 上 364 项 0 失败（会备份并还原用户词表，跑后 winref 上三个文件仍是原样），
Wine 上全过；32 位程序也能用；Office 三应用保存回归通过，Word 启动时那条 ERR 没有了。

与 Windows 仍不同的：词典本身——Hunspell 的建议与 Windows 不同（`wrold` 有 world、wold 两个，所以综合检查给的是“取建议”
而不是直接替换，测试里记为 todo_wine），宿主 en_US 里没有 `café`、`naïve`；Windows 有的几处延迟或缓存不照做（更正改了之后
在本次会话里仍给旧更正、移除后再添加的词一时仍报错、注册的词典过一会儿才生效、更正成两个词后引擎所有检查都失败）。


## Word 启动时的 fixme 逐项补齐：注册表、网络、重启恢复、图形、指针、Web 服务与事件跟踪（2026-10-02，altars-up `6ec740aaac2`…`cb9a2430fbd`）

Word 每次启动打出几十条 `fixme`。按“Word 怎么问就怎么问、再问一圈周边”的办法，`tools/startupgapsprobe` 的探针在
Windows 11 build 29671 上量出规则（第 55–76 批），Wine 照着实现，同一个探针在 Wine 下的输出与 Windows 逐行对比。
排序先做会改变行为的（注册表、网络、崩溃恢复、图形），再做事件跟踪，最后是只是噪声的小函数（第 8、9 项，见下一节）。

- **注册表**（`6ec740aaac2`）：`RegQueryInfoKey` 的安全描述符大小是所有者、组和 DACL 的大小，句柄不能读时为 0；
  大小为 0 的类缓冲保持 0。
- **网络**（`e62eefcc98e`、`76e29831cd2`、`6665c775bc6`、`172b2f07a3f`、`e6168bdbda9`）：没载波的接口算断开；IP Helper 的
  变化通知由一个线程等 NSI 通知、逐行告诉注册者，初始通知在注册线程上、调用返回前，每个地址族一次（连通性提示的在线程上、
  稍后）。网络列表管理器把网络当作注册表里的配置文件（按网关识别，名字“网络”“网络 2”…），连接是“开着的接口”，有网关才算
  上互联网；主机的变化刷新状态并在各自套间里告诉接收器。接口像 Windows 一样注册：NETWORKLIST 类型库的接口走类型库封送，
  费用接口走代理类 `{DCB00009}`，否则别的套间里的接收器 `Advise` 不上（`CoMarshalInterface` 回 `E_NOINTERFACE`）。
- **重启与恢复、WER、Restart Manager**（`6b8e020103b`、`077f2f30e37`、`19214a1f699`）：注册的数据放在 PEB 的
  `WerRegistrationData` 指的 0x968 字节块里，布局照 Windows 11 x64，别的进程照样能读；命令行、恢复回调、WER 文件/内存块/
  元数据/运行时异常模块的上限和错误码按实测；进程崩溃时先跑恢复回调（每次 `ApplicationRecoveryInProgress` 再给一个间隔），
  运行满一分钟又注册了重启的就重启。`FileProcessIdsUsingFileInformation` 由服务器遍历所有进程的句柄与映射；Restart Manager
  的会话存在 `HKCU\Software\Microsoft\RestartManager`，`RmGetList`/`RmShutdown`/`RmRestart` 按实测给状态。
- **图形**（`2defe20f09e`、`644c425c979`、`15ce537d955`、`3408bf9d6c6`）：`CheckFeatureSupport` 回答 Windows 11 知道的每一项、
  只认自己的结构大小（`D3D11_OPTIONS2` 例外）；GDI 兼容表面 `ReleaseDC` 只保留脏矩形内 GDI 画的，带丢弃的 `GetDC` 从全零开始，
  重复 `GetDC` 等为 `DXGI_ERROR_INVALID_CALL`；Direct2D 只在绘制中给 DC，表面目标拒绝直通 alpha，默认文字抗锯齿只在不透明目标
  上是 ClearType；DirectWrite 默认参数 1.8/0.5/1.0/1.0/RGB，按显示器读 `Avalon.Graphics` 下 ClearType 调谐器留的值。
- **指针设备**（`cccb591332d`）：没有触摸屏和笔的机器上，`GetPointerDevices` 问数目给 0、给数组则
  `ERROR_SYSTEM_DEVICE_NOT_FOUND`；原来缺的 `GetPointerDevice`、`GetPointerDeviceCursors` 一调用就结束进程。
- **Web Services**（`4ba16dad110`、`6b83fddd876`）：畸形输入让读取器进入故障状态（之后各调用 `WS_E_OBJECT_FAULTED`，
  `WsReadType` 仍先查参数）；`WS_ERROR` 里放 Windows 放的字符串（行列、错因，中英文逐字一致）；SSL 传输安全绑定照做，
  证书回调在 `WsOpenChannel` 时问、服务代理到第一次调用才问。
- **事件跟踪与事件源**（`a090989a977`、`cb9a2430fbd`）：Word 为自己开的私有进程内会话（`v2_WINWORD:…`，循环日志
  `%TEMP%\Outlook Logging\WINWORD_…-32.etl`，10 MB）原来只是返回成功的桩，九个提供程序从不被启用。现在 ntdll 在进程内维护
  会话：启动时按 Windows 的顺序校验，`EnableTraceEx2` 按“私有当场、其他稍后”告诉提供程序所有会话合起来的级别与关键字，
  经典提供程序拿到 Windows 那样打包的记录器句柄；事件按 Windows 的格式写进日志文件（Windows 的 `tracerpt` 能读，第 75 批），
  或交给进程内的实时消费者；`OpenTrace`/`ProcessTrace` 也能读日志文件。Word 启动、关闭后它的日志文件里有两个 Office 提供程序的
  13 条事件。事件日志与事件源有了真实句柄，服务器名与源名按实测校验。规则细节见 `tools/startupgapsprobe/README.md`。

测试：新的 advapi32 `trace` 测试在 Windows 上 374 项 0 失败、Wine 上全过；`eventlog` 测试去掉了 27 个已通过的 todo_wine，0 失败；
各探针（`etw`…`etw5`）Wine 与 Windows 逐行相同，只差环境（记录器号、处理器数、`elevated`）。

与 Windows 仍不同的：会话只在本进程内（别的进程看不见、控制不了，`QueryAllTraces` 只列本进程的）；Windows 按处理器分缓冲区，
这里一个会话一个缓冲区（实时消费者因此可能少收到一次 `BufferCallback`）；不在进程内的旧式私有记录器 Windows 要等一分钟才
返回 1460，这里立即返回；事件日志本身（`ReportEvent` 写、`ReadEventLog` 读）仍是桩。

## Word 启动时的 fixme 补齐（续）：小函数与其余（2026-10-02，altars-up `2ac922706c6`…`daa727b433f`）

第 8、9 项是 Word 启动时问到、Wine 只有桩或半实现的小函数。仍是先量后做：`tools/startupgapsprobe` 的 `misc`、`misc2`、`misc3`、
`misc4` 在 Windows 11 上量（第 77–80 批），Wine 下逐行对比。做完后 Word 启动只剩下面“仍不同”里的五类 fixme（原来几十条）。

- **性能与功耗信息**（`2ac922706c6`）：`SystemPerformanceInformation` 是 Windows 10 起的 376 字节，旧的 312 字节仍收，介于两者的
  按给的长度填；新计数取自 Linux（脏页与阈值、共享内存、页表、可驻留页、上下文切换、缺页、内核内存当作分页/非分页池）。
  `SystemPowerCapabilities` 报本机的电源键、盖子、睡眠状态（deep 是 S3，只有 s2idle 是现代待机）、能恢复的休眠、背光、温控、
  RTC 唤醒、UPS、转动的磁盘，不再是一台 XP 台式机的值；结构补上 XP/10 加的字段，不留未初始化字节。
- **缓解策略**（`76696a4b564`，协议改动）：进程的策略存在服务器里，别的进程问得到；创建时给的选项按 Windows 11 的换算变成
  策略（26 个选项逐个量过：两位一组的开/关/第三种，高熵 ASLR 跟随自底向上 ASLR，影子栈连带上下文 IP 校验）；子进程策略
  受限的进程再建进程得 `ERROR_CHILD_PROCESS_BLOCKED`（367），该属性只收 4 字节。默认策略加上 SEHOP，指针认证只在 ARM64。
- **持久线程**（`e81d2844471`）：`WT_EXECUTEINPERSISTENTTHREAD` 和持久环境的工作在每个池一个、永不退出、可警报等待的线程上跑，
  排给它的 APC 在那里执行。
- **私有对象安全**（`d5015b5941d`）：`CreatePrivateObjectSecurity(Ex)` 原来给 Everyone 全权限，`SetPrivateObjectSecurity(Ex)`
  什么也不做却返回成功。现在所有者/组/DACL/SACL 的来源、继承（容器既生效又继续传的 ACE 在需要映射时拆成两条）、令牌默认
  DACL、自动继承、保护、标签、空令牌的错误都按实测；`ConvertToAutoInheritPrivateObjectSecurity`、`RtlQuerySecurityObject`
  也做了。顺带修了 SDDL：输出认识强制标签（`ML`、`NR/NW/NX`）和其余 ACE 类型、对象 GUID，NULL ACL 写 `NO_ACCESS_CONTROL`，
  SACL 用自己的控制位；解析把 `NO_ACCESS_CONTROL` 读成 NULL ACL、SACL 的标志落到 SACL 位上。
- **短名**（`b942c116e0a`）：`SetFileShortNameW` 像 Windows 一样在调用期间临时启用还原特权（`RtlAcquirePrivilege`，新实现），
  要能删除的句柄、合法 8.3 名、目录里没有别的文件用着（长名或短名，183），转大写，空串去掉短名。短名存在文件的扩展属性里，
  目录打上标记，列目录和按短名查找只在有标记的目录里读属性；`FileAlternateNameInformation`、`GetShortPathName`
  （本身是 8.3 的部分保持原样）都按实测。参数错误的 `NtQueryInformationFile` 不动 I/O 状态块。
- **小函数**：`LOAD_LIBRARY_REQUIRE_SIGNED_TARGET`（自身无签名的进程装不了未装过的库，577）、精确中断时间、带唤醒上下文的
  可等待计时器、无包进程的包 ID、MUI 版本信息、堆扩展信息（级别 1）、窗口的输入范围与 `TF_GetInputScope`、文本存储的
  编辑事务、数组与 blob 转字符串、工作组的加入信息与 AAD、WinHTTP 的 IPv6 快速回退、WMI 代理的安全毯、COM 激活失败的
  错误码与 `MULTI_QI`、`OaBuildVersion`（50.5014）、xmllite 的读写属性与紧凑空元素（`<b/>`）、调试 OpenGL 上下文、
  `RpcMgmtEnableIdleCleanup`、type_info 名字随模块释放、许可值（`Kernel-ProductInfo`、`Container-License-Mode` 等，
  `wineboot -u` 后生效）。只是噪声的（SAX 读取器的其他接口、`RoGetActivationFactory`、`DllDebugObjectRPCHook`、
  GDI 呈现与线型）改成只在真丢了东西时说。
- **xmllite 写入器与 Windows 11 不同的一处**：没有打开的元素时 `WriteEndElement`/`WriteFullEndElement` 先返回 `E_INVALIDARG`
  且不改状态（Wine 的测试原按旧 Windows 写，在 Windows 11 上 29 处失败）；测试改为以 Windows 11 为准、旧值算 broken。

测试：kernelbase `process` 新增的创建策略测试在 Windows 上 175 项 0 失败；xmllite `writer` 2815 项、`reader` 9059 项、advapi32
`security` 3782 项、kernel32 `file`（含新的短名测试）、`path`、`process`、ntdll `file`/`directory`/`info`/`threadpool`、
propsys、msctf、opengl32 等在 Wine 上 0 失败；`misc`…`misc4` 与 Windows 逐行相同，只差环境与下面列的几处。Word 保存回归
（启动、新建、编辑、保存、关闭、退出）在新版本上通过。

与 Windows 仍不同的：Wine 管理员令牌的默认所有者是 513 组而不是 Administrators（私有对象的所有者因此不同）；自动生成的短名
是 Wine 的散列（`SFN4~2UB`）而不是 `~1` 编号；`RtlQueryHeapInformation` 的 0x80000001 类和扩展信息的 0x80000000 级别、
`DOMDocument60` 的 `IMarshal`、核心上下文里的线型、粗粒度 `QueryUnbiasedInterruptTime` 不走。Word 启动还剩：wininet 的
Cookie 属性（Secure、HttpOnly、SameSite）、事件日志写入（`ReportEventW` 仍是桩）、D3D10 接口仿真、某个窗口的 D3D 呈现
走 GDI 回退且 blit 失败、一处 noexcept 函数抛出异常——下一步逐个查。

## Word 退出码 3：FH4 的 catch 状态，以及 Cookie、堆与已销毁窗口的呈现（2026-10-02，altars-up `602da6db9f1`…`e73c2701894`）

上一节末尾剩下的几类里，这四处当天补完（`tools/startupgapsprobe` 的 `misc5` 在 Windows 11 上量，第 81–83 批）。

- **退出码 3**（`e73c2701894`）：Word 每次关闭都以 3 退出，日志最后一行是 `err:seh:check_noexcept noexcept function
  propagating exception`。链是用 bptrace 的栈回溯在抛出点量出来的：wwlib 的关闭例程调 `ExitProcess` →
  `LdrShutdownProcess` → MSO 的 `DllMain` 跑 CRT 的 atexit 表 → MSO 里 `OfficeVoice::GetVoiceRoamingSettings()` 的函数内
  静态对象析构 → 它持有的听写语言漫游设置（`Roaming::RoamingObject`）析构 → Mso30 的 `GetRoamingManager` →
  `RoamingManager::GetInstance`：管理器在两秒前的有序关闭里已经 `Uninit`，于是抛 `Roaming::RoamingNotInitializedException`。
  这在 Windows 上同样会抛：`VoiceRoamingSettings::UnInit` 只出现在虚表里，整个 Office 没有一处调用（只有 WWLIB 导入
  `GetVoiceRoamingSettings`，用的是取/设声音的两个槽）。Windows 靠的是析构函数里的 `catch (...)`（覆盖状态 1–3，调用处是 2）：
  里面的 `Roaming::HandleRoamingCacheExceptionTag` 再 `throw;`，按类型分类，`catch (const RoamingNotInitializedException&)`
  接住，析构函数正常走完。Wine 的 `__CxxFrameHandler4` 在那一帧用了状态 0：它把“下一个处理器从哪个状态找起”放在 FLS 里
  交接，约定 -2 表示按 IP 求，只在 DLL_THREAD_ATTACH 时设 -2；而进程退出时 `LdrShutdownProcess` 先清掉所有线程的 FLS
  值再发 DLL_PROCESS_DETACH，读到的是 NULL，即 0；vcruntime140_1 载入之前就在跑的线程也从没被设过。对照实验很干净：
  同一流程只把 vcruntime140_1 换成 Office 自带的原生版，退出码 0（两次），换回内置版 3（两次）。改法：状态偏移 2 存，
  NULL 即 -2，线程附加时不再需要设。ucrtbase 新测试 `test___CxxFrameHandler4` 在运行时生成一个带 FH4 表的函数
  （`catch (...)` 只覆盖调用处的状态 1，外面再包一层 `__C_specific_handler`，漏掉时返回异常码而不是让进程崩），在主线程、
  载入后建的线程、载入前就在跑的线程里各跑一次：Windows 11 用原生 vcruntime140_1 三者都捕获（第 83 批，44 项 0 失败），
  Wine 修前第三种漏掉。修后 Word 连续三次关闭退出码 0，断点确认走的正是抛出 → 分类 catch → 析构函数的 continuation。
- **Cookie**（`d4644bbd51d`）：带 Secure 的 Cookie 原来照常保存、打一条 FIXME，SameSite 是不认识的选项。Windows 11 上
  Secure 的 Cookie 无论在哪设的都只给 https；SameSite 不管写什么都收下、Cookie 照存；HttpOnly 的不带 `INTERNET_COOKIE_HTTPONLY`
  设不进去（返回 5，`COOKIE_STATE_REJECT`），取时也要带这个标志才给。探针只用 `*.invalid` 域上的会话 Cookie，结束时过期掉。
- **堆的性能数字**（`cd7169ca82b`）：Word 问 `HeapExtendedInformation` 的 0x80000000 级别。Windows 11 给每个堆 160 字节：
  地址、保留与提交、段数、提交中在用的与空闲的（两者加起来就是提交量）、空闲块数、单独分配的大块；不指定堆时按进程堆在先、
  其余按创建先后逐个给。级别 0–2 给堆本身，不指定堆时给全部堆的保留、提交与个数；缓冲只够请求头是参数错误，不够放答案时
  `STATUS_BUFFER_TOO_SMALL` 并给出所需大小。
- **已销毁窗口的呈现**（`602da6db9f1`）：Word 向自己已经销毁的窗口（启动画面就是一个）的交换链呈现。那时设备上下文已不属于
  任何窗口，wined3d 退到备用窗口、走 GDI 呈现，失败（`Failed to blit`），前面还有一条把原因说成缺 GL 上下文的 FIXME。
  现在向已销毁的窗口呈现悄悄地什么也不做，设备上下文与窗口对不上时打 WARN。

工具：bptrace 的 `BPTRACE_WALK` 改为从目标进程内存里读各模块的 `.pdata`（dbghelp 要打开模块文件，而 C2R 模块报的路径在磁盘上
不存在，回溯在第 0 帧就停），断点可写成 `模块!RVA`、一次附加跨几个模块；新脚本 `scripts/pe-xrefs.py`（谁用 rip 相对寻址或直接
调用碰到某个 RVA）、`pe-importers.py`（谁按名或按序号导入某个函数，含延迟导入）、`pe-fh4.py`（解出一个函数的 FH4 表与某地址
处的状态）。`build-winetest.sh` 按 makedep 选 C 运行库（测试导入哪个用哪个，否则 msvcrt），C 运行库自己的测试加
`-fno-builtin`，统一 `-mlong-double-64`：msvcrt、msvcr90、msvcr120、ucrtbase 的测试从此能编，且在 Wine 上 0 失败。

Word 启动还剩：事件日志写入（`ReportEventW` 仍是桩）、D3D10 接口仿真（`SwapDeviceContextState`）、关闭时两条
`CoReleaseMarshalData` 错误。

### Word 退出时的两条 COM 错误：断开之后的释放、ROT 与 GIT（altars-up `0ab0953efa8`…`6f9748683d6`）

Word 关闭时先对自己的对象 `CoDisconnectObject`，再撤销它们的 ROT 登记、拖放目标和 GIT cookie。Wine 在撤销时
`CoReleaseMarshalData` 找不到 stub manager，打两条 ERR、返回 `RPC_E_INVALID_OBJREF`（bptrace 在 combase 下断看到调用者是
ole32 的 `rot_entry_delete`、`RevokeDragDrop` 与 GIT 的撤销）。`tools/startupgapsprobe` 的 `comrel` 在 Windows 11 上量了这一类
（第 84–86 批），结果与 Wine 差了好几处，按实测改：

- `CoReleaseMarshalData` 对已断开、已释放过、apartment 已结束的数据一律 S_OK（`696cbe39224`）。
- marshal 对象的 IUnknown 只加一个引用（stub manager 的），Wine 原来多加一个接口桩的（`0ab0953efa8`）；同一提交里
  `AGILEREFERENCE_DELAYEDMARSHAL` 的 agile reference 改为一直持有对象、创建线程直接取对象、其它线程（同 apartment 的也算）才
  marshal——原来 marshal 后放掉对象，测试里靠那个多出的引用凑对了数。roapi、usrmarshal 测试里原来 todo 的引用计数随之相符。
- ROT 把断开对象的登记当作不存在：`IsRunning` S_FALSE、`GetObject`/`GetTimeOfLastChange` 为 `MK_E_UNAVAILABLE`、枚举不列、
  只有 `Revoke` 还认得 cookie（`696cbe39224`）。存活与否由 combase 按 OBJREF 的 OXID/OID 查 stub manager，不去 unmarshal
  （跨 apartment 的 unmarshal 要 RemAddRef，所属 STA 不泵消息就会卡住）。
- GIT 登记只持有接口、不建 stub；本 apartment 取回对象本身，别的 apartment 第一次来取时经登记 apartment 的
  `IContextCallback` 在那里 TABLESTRONG marshal；断开只丢 stub 不丢表里的引用，之后本 apartment 照样取得到、别的
  apartment 得 `CO_E_OBJNOTREG`，撤销 S_OK（`6f9748683d6`）。

`comrel` 在修改后的 Wine 上与 Windows 逐行相同（引用计数、跨 apartment 的代理、断开后的各个返回值）。ole32 新测试
（`test_release_marshal_data_disconnected`、`test_globalinterfacetable_disconnect`、`test_ROT_disconnected`）单独编成程序在
Windows 11 上 58 项 0 失败（第 87 批）；ole32 全部测试、combase roapi/string 在 Wine 上 0 失败。Word 的日志从此一条 ERR 也没有，
启动只剩 `ReportEventW` 与 D3D10 接口仿真两条 FIXME。

## 事件日志：经典日志的读写，以及 wevtapi 的查询、订阅、渲染与格式化（2026-10-02，altars-up `886d72dfbf5`…`cc575565116`）

altars-up 这天变基到了 Wine 11.19（`455e3509b98`）；变基前的分支留作 `altars-up-pre-1119`，前面各节到“Word 退出码 3”为止的
提交号指的是它。

Word 启动时向 Application 日志报一条事件（`ReportEventW`），Wine 原来什么也不写；`ReadEventLog`、计数、备份、清空都是桩，
wevtapi（Vista 以来的事件日志 API）除了几个函数也是桩。规则都在 Windows 11 上量（`tools/startupgapsprobe` 的 `evtread`、
`evtapi`、`evtquery`、`evtlimits`、`evtsub`、`evtrender`、`evtmsg`，第 88–113 批，全部只读），细则见探针 README 的“事件日志”。

- **经典日志**（`886d72dfbf5`）：每个日志一个经典格式的文件（`system32\winevt\Logs\<日志>.evt`），所有进程共用，每次访问
  前后一个命名互斥体，`NotifyChangeEventLog` 靠一个命名事件。来源写进同名的日志、键里列着它的日志，否则 Application。记录
  的布局、读的移动、错误码照 Windows；清空后编号接着走；快超过 MaxSize 时丢最旧的。事件日志服务启动时写 6009、6005，停止时
  写 6006，与 Windows 相同。Word 的那条事件进了 OAlerts。顺带：ntdll 给每个线程分 TLS 槽时跳过没有 TEB 或已退出的线程
  （`9f4b719565b`，原来偶尔打一条 ERR）。
- **netevent.dll**（`c262aad3d0d`）：事件日志服务各事件（6005、6006、6008、6009、6011、6013）的消息表，英文原文与简体中文翻译
  （zh_CN.po），登记为 System 日志里 EventLog 来源的 `EventMessageFile`。
- **wevtapi**（`cc575565116`）：建在经典日志之上。查询（通道、文件、结构化查询与 Suppress）、XPath（`band()`、`timediff()`、
  `!=`、位置与 `position()`，括号 24 层、谓词 21 层、连接 23 项的上限）、`EvtNext`/`EvtSeek`、订阅（拉取与回调）、渲染（XML、
  系统/用户/路径上下文）、书签、格式化（消息、级别、任务、关键字按发布者的语言，`RenderingInfo`）、日志信息、清空与导出、
  通道枚举与通道配置、本机会话。几处 Windows 自己的怪规则照做了：位置作存在测试只看第一个元素；和数字比较时 `"10.00."`
  不算 10；订阅里已有的事件不置信号、`EvtNext` 从不等待，说完没有之后再问是 `ERROR_INVALID_OPERATION`；渲染值时清零整个缓冲区；
  上下文的路径成树，祖先路径后加是 87。没有照做的：位置配 `!=` `<` `>` 的结果（Windows 自相矛盾）；还有 5000 项的 `or` 查询
  会让 Windows 的事件日志服务崩溃（量上限时在 winref 上撞到一次，服务停了约两分钟后由服务控制管理器重启；之后的探针都限在
  小值）。

还不一样的：经典记录里没有进程号与线程号，Wine 的 `Execution` 给 0（Windows 对别的进程报来的事件给那个进程号、线程号 0，
服务自己的事件给服务的线程号）；日志文件路径是 `.evt` 不是 `.evtx`；通道只有经典日志；用户上下文里那张指针表的内容（Windows 的
指向别处，Wine 指向缓冲区里的字符串）。wevtapi 测试在 Wine 上 387 项 0 失败，单独编成程序在 Windows 11 上 236 项 0 失败（第
113 批，不含会写 HKLM 的通道配置测试）；各探针在 Wine 下的输出与 Windows 逐行对过，剩下的差别都是上面这些与数据本身
（内部版本号、计算机名、事件条数）。

Word 启动时剩下的 FIXME 只有 D3D10 接口仿真（`SwapDeviceContextState`）。

## D3D10 接口仿真：另一个接口的调用被丢弃（2026-10-03，altars-up `86dae43b2ac`…`43339ce9dba`）

Word 启动时最后一条 FIXME 是 d3d11 `SwapDeviceContextState` 的 “D3D10 interface emulation not fully implemented yet!”。
Office 用 D3D10.1 设备画（Word 载入 d3d10_1.dll），Direct2D 在它上面换入自己的 D3D11 上下文状态、画完再换回，换回 D3D10
状态时打出这一条。`tools/startupgapsprobe` 的 `d3d10emu`、`d3d10emu2` 在 Windows 11 上量了两个方向（第 114–115 批）：模拟
`ID3D10Device` 的状态在用时，立即上下文的 D3D11 调用凡是设置、绘制、计算、拷贝、更新、清除、`GenerateMips`、`ClearState`
都被丢掉，读状态得到空状态的值（视口个数 0、混合因子全 1、采样掩码全 1，只有 `PSGetSamplers` 不写输出），`Map`/`Unmap`
与查询照常；D3D11 状态在用时，`ID3D10Device` 的同类调用一样被丢掉。Wine 原来两边都照做。

- **d3d11**（`43339ce9dba`）：照 Windows 丢弃；D3D10 方法里转调 D3D11 方法的几个改调不检查的函数体。顺带改一个旧错：经
  `D3D11CoreCreateDevice` 建的设备，第一个状态被标成模拟 `ID3D10Device1`（状态在设备里建好之后才知道它是 D3D11 设备）——
  原来只影响那条 FIXME，照做之后就会让每个 D3D11 设备的调用都被丢掉。共享表面的拷贝是 d3d11 自己做的，不经检查。
- **Wine 自己在应用的设备上做的事**（`86dae43b2ac`…`435a78fa89b`）先改为换入自己的状态，否则在 Office 的 D3D10.1 设备上会被
  丢掉：d2d1 位图的拷贝与清除、覆盖遮罩的清除；dcomp 绘图表面的图块读写与清除、纹理镜像；dxgi 合成交换链呈现时的拷贝；
  mfplat DXGI 表面缓冲的拷贝；mfmediaengine 的传帧（它原来还把自己的着色器、缓冲、目标留在应用的上下文里）。

验证：d3d11 测试里原来 todo 的 47 项全过，其余与改前相同（6 项改前就失败，是本机 GL 驱动的）；d3d10core 0 失败；d2d1、dxgi
与改前相同；dcomp、mfmediaengine 0 失败；mfplat 改前改后都在同一处崩溃（不是宿主媒体库，是 Wine 自己的 D3D12 缓冲，见下文）。两个探针在 Wine 下与 Windows
逐行相同。Word 启动与关闭（退出码 0）、经自动化输入的正文、PowerPoint 新建的幻灯片都画得正确，三个保存回归通过；Word 启动
时一条 FIXME 也不剩了。

经自动化驱动、或 Word 跑得久一些时，日志里还有几条 err，随后处理（altars-up `04c80c52c27`…`d71573adee6`）：

- `Windows.Foundation.Diagnostics.AsyncCausalityTracer`：Windows 上由 combase.dll 提供（`ActivatableClassId` 的 `DllPath`），Wine 没有这个
  类。`causality` 量了 Windows 11（第 116–117 批）：只有工厂一个对象，agile，实现 `IActivationFactory` 与
  `IAsyncCausalityTracerStatics`，`GetRuntimeClassName` 是 `E_ILLEGAL_METHOD_CALL`，`ActivateInstance` 是 `E_NOTIMPL`；各个 Trace 只查
  级别与来源（越界 `E_INVALIDARG`），状态处理器收下、按任何 token 移除都是 S_OK。combase 照做，类经 `classes.idl` 登记。
- `RevokeDragDrop` 对已销毁的窗口、MXXMLWriter 被问 `{e19c7100-9709-4db7-9373-e7b518b47086}`：`oledead`（第 118 批）显示 Wine 的行为已经与
  Windows 相同（`DRAGDROP_E_INVALIDHWND`、放置目标的引用 Windows 也不放；`E_NOINTERFACE`），只是不该打 ERR，改为 WARN。
- `Common Files\Microsoft Shared\ClickToRun\msoxmlmf.dll` 装不上：不是缺口。前面查过（见上文 “`apartment_add_dll couldn't load in-process dll
  "...\ClickToRun\msoxmlmf.dll"`” 一条）：它来自 OfficeClickToRun.exe，内嵌清单故意把 InfoPath 的 XML MIME 过滤器
  `{807583E5-5146-11D5-A672-00B0D022E945}` 声明在自己目录里一个不存在的 `msoxmlmf.dll` 上，Windows 上同样加载失败，只是不打日志；
  10 月 3 日对当前安装又核对了一遍清单。间歇出现是因为它跟着 C2R 服务绑定清单文件的时机走，日志进了 Word 的输出是因为服务继承了
  第一个拉起会话的进程的 stderr。`RoGetActivationFactory` 在没初始化 COM 的线程上的 `ensure_mta`、Word 退出时 cscript 释放代理撞上的
  `get_stub_manager_from_ipid` 是上游有意留的诊断，没动。
- mfplat 测试在 :77 上崩溃（交给 Claudex 查，`fbff0a5a7a1`）：死在宿主线程里，是 Mesa lavapipe（`libvulkan_lvp.so`）的空指针，
  起因却在 Wine。D3D12 表面缓冲写入后，`Unlock2D` 在缓冲自己的复制队列上把数据拷进纹理，没人等这次拷贝；紧接着释放缓冲，就把上传
  资源、命令列表和分配器在拷贝途中放掉了。释放前等缓冲自己的 fence 到最后提交的值即可：写入的拷贝不等外部同步对象，读取的拷贝在
  `Lock` 里本来就等，所以不会卡死。新测试在缓冲释放后读回纹理，Windows 11 上写入的数据同样留在纹理里（第 120 批）。修后全量 mfplat
  在 :77 跑完，剩下的失败都是 `mfplat.c:1658` 的旧失败（以冒号、斜杠开头的路径，见下一条）。全量 `trace+d3d,trace+vulkan` 会改变时序让崩溃消失，
  要用窄的诊断（析构时 fence 的完成值小于提交值）和去掉修复的反向对照来定位。
- 那 8 个旧失败（altars-up `4d7e5cfea14`，`tools/colonpathprobe`，第 121–124 批）：`::C:\…` 在 Windows 上是盘 `:` 上的路径
  （`GetFullPathNameW` 给 `::\C:\…`），盘不存在就是“路径未找到”，后面各段一概不看；Wine 为省一次 stat 不查盘符，拿后面
  带冒号的段作答，报了“名称无效”。盘存在时 Windows 11 也有先后：空段、中间段的流整条路径先查；非法字符、`..` 查到那一段才算，
  前面目录不存在就是“路径未找到”；`.` 在中间段是“路径未找到”（上游测试把这一种记作 broken，期望的是旧版 Windows 的
  “名称无效”）。照做之后 mfplat 全量 0 失败，ntdll 里链接目标中途带 `.`、kernel32 里删掉盘符定义后再打开这两条上游 todo 也通过了。
  UNC 服务器名带冒号（`\\file:\…`）随网络环境变（这台 Windows 是 64，上游测试机是 53），没动。

## 前缀更新时冒出来的 FIXME，与 RoGetParameterizedTypeInstanceIID（2026-10-03，altars-up `c645e671272`）

dist-up 换了 wine.inf 之后，第一次启动 Word 会先做前缀更新（`wineboot -u`），服务进程跟着重启，它们的 FIXME 也打进了 Word 的日志：
`RpcServerRegisterIf3` 的安全描述符、mountmgr 伪造的存储设备属性、`CoInitializeSecurity`、`LoadUserProfileW`、RAS 枚举、
`SystemLeapSecondInformation`、`BCryptGetFipsAlgorithmMode`、msxml 的 `NewParser` 与 `putref_schemas`，还有四个进程调用
`RoGetParameterizedTypeInstanceIID`（桩，回 `E_NOTIMPL`）。之后正常冷启动 Word，一条 err/fixme 都没有。

- **`RoGetParameterizedTypeInstanceIID`**：.NET 的 WinRT 互操作用它算 `IVector<String>` 这类参数化类型实例的 IID；上游和 CrossOver
  都是桩。`tools/paramiidprobe` 在 Windows 11 上量了签名的写法、14 个基本类型、定位器被问到的顺序和各种错误（第 125 批），
  combase 照做：签名在 WinRT 命名空间下做 SHA-1 得第 5 版 UUID（bcrypt 延迟导入），`RoParameterizedTypeExtraGetTypeSignature`、
  `RoFreeParameterizedTypeExtra` 一并实现。新测试在 Windows 上 0 失败（第 126 批）。
- 当时其余几条还未实现；同日后续原生测量、权限根因、三组修复和真实应用回归见
  [Office 前缀更新诊断：原生验证与修复](office-prefixupdate-20261003.md)，不要将此处历史状态当成当前待办。
- 2026-10-04 继续处理 Word 启动日志里的其余诊断（FIPS 策略、AI_FQDN、证书 link、WsAbortServiceProxy 与 WinHTTP 同步取消），
  原生测量、失败尝试和验证见 [Office 剩余功能修复：第一批](office-rest-fixes-20261004.md)。
