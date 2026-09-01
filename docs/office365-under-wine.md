# Microsoft 365 under Wine — what was measured

Session of 2026-08-31. Everything below is either something a tool printed or a
file that exists. Where something is inferred it says so.

**Where it stops today:** `WINWORD.EXE` loads 186 modules and exits with status
0, having never loaded a single line of Word. It stops inside Click-to-Run's
bootstrap.

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
