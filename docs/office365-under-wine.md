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
