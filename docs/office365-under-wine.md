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
