# wine-altars

A Wine fork aimed at running closed-source Microsoft clients on Linux —
currently **Microsoft 365 (Click-to-Run)** and, from the work this is built on,
Microsoft's RDP client engine.

Base: **Wine 11.0** + a 13-patch series carried over from the
`gnome-remote-desktop` / `mstsc` interop work, plus whatever this project adds.

## Layout

    patches/mstsc/      the inherited 13-patch series (CredSSP, MUI redirection,
                        TraceLogging, srpapi, ...) — see patches/mstsc/README.md
    patches/office/     patches written for Microsoft 365, plus the
                        measurement-only diagnostics that earned them
    patches/ported/     work ported from wine-staging / Valve / wine-tkg
                        (see docs/upstream-evaluation.md for what was rejected
                         and why — that list is the useful half)
    scripts/            build, and the Office import pipeline
    docs/               what was measured, and where it currently stops
    wine-src/           the Wine tree (own git repo, gitignored here)

`wine-src` is a git repository with three refs that mean something:

| ref                        | what it is                                        |
|----------------------------|---------------------------------------------------|
| `base` (tag `wine-11.0`)   | pristine upstream tarball, nothing applied        |
| `altars`                   | `base` + the 13 patches, one commit each          |
| `provenance/original-tree` | the tree as inherited, kept to prove equivalence  |

`altars` was verified byte-identical to the inherited tree, so the patch series
is known to reproduce a Wine that was already shown to work.

## Build

    scripts/build-wine.sh          # configure + make + install into dist/

64-bit only (`--enable-archs=x86_64`) — every client targeted here is x64 and a
multiarch build costs several times as much for nothing.

## Microsoft 365

    scripts/setup-prefix.sh              # create the prefix, Windows 11 mode
    scripts/import-office.sh             # copy Office out of a real C: drive
    scripts/export-office-registry.py    # SOFTWARE + SYSTEM + NTUSER + App-V vreg
    scripts/apply-office-registry.sh     # import the .reg into the prefix
    scripts/merge-case-collisions.py     # REQUIRED after import: fold directories
                                         # that differ only in case (NTFS source,
                                         # ext4 prefix) -- see the docs for why
                                         # OfficeClickToRun.exe went missing

**Status: Word's window paints, and stops on licensing.** It reaches its Start
screen — title bar, nav rail, document cards — behind a modal licensing dialog,
and Office's own Microsoft sign-in page renders inside it. Two rendering
findings got it there, and one gap remains.

*Fonts.* Wine's DirectWrite builds its font collection **only** from
`HKLM\...\CurrentVersion\Fonts` and never scans the font directory; GDI does
scan it, so the discrepancy is invisible until something asks DirectWrite.
`import-office.sh` copies 423 Windows fonts into the prefix and registers none,
so DirectWrite could not match even Tahoma and every `DrawTextLayout` failed
(971 per start-up). `scripts/register-prefix-fonts.py` writes the entries — a
curated 75-file set, because registering all 422 makes Word stall at 56 loaded
modules while dwrite spends a million calls building the collection.

*Presentation.* Office puts its whole UI in a `DXGI_SWAP_EFFECT_FLIP_DISCARD`
swapchain on a **child** `NetUIHWND` (1438x808 for Word's frame), not on the
top-level window, and never touches DirectComposition. Wine routes a child
window through its offscreen path — XComposite-redirected client window,
reparented onto a 1x1 dummy, `StretchBlt` onto the top-level — and that works:
19 presents per start-up, full-window destination rect, blit succeeds, and the
redirected X window can be read back with Office's start screen in it. The
window is no longer white. **What changed is not known** — the earlier all-white
captures are not reproducible and every candidate was tested and ruled out
(see the docs; that includes proving it was not a screenshot artifact).

*The gap: text and icons — Office wants a shared surface Wine cannot give it.*
Use **`SETLANG.EXE`** to work on this, not Word — the Office Language
Preferences dialog is not licence-gated, starts in 25 seconds, and fails
identically: every panel, border and theme colour renders and no control has any
text. Office shapes **260 glyph runs** per start-up and rasterises **four**.
Office names the reason itself: NetUI throws forty `AirSpace::DeviceError` C++
exceptions per dialog (read out of the PE's `ThrowInfo`, which an MSVC throw
hands you), and the count matches, exactly, forty stubbed
`IDXGIResource::GetSharedHandle` calls and forty `E_NOINTERFACE`s for
`IID_IDXGIKeyedMutex`. Office's compositor renders on one D3D device and
composites on another through a `MISC_SHARED_KEYEDMUTEX` surface — measured:
different `IDXGIDevice`, different thread.

Patch 0008 implements it: a handle registry in wined3d, `GetSharedHandle` and
`IDXGIKeyedMutex` in dxgi, and in d3d11 an `OpenSharedResource` that gives each
participant a texture on its own device and moves the content between them at
the keyed-mutex handoff. Aliasing the originating texture instead looks correct
in every trace and silently does not carry pixels — `tools/d3dshare` is sixty
lines that prove it in one run.

**Word's Start screen and Office's dialogs now render**: distinct colours on
screen go from 8 to ~1300 for Word and 577 to 1263 for the Language Preferences
dialog, with every label, template name and button caption present. Still
outstanding, both verified pre-existing by building without the patch: Word's
licensing dialog is a black rectangle, and Word fails to open a window on about
one start in three.

**The black dialog is fixed.** It looked at first like something outside this
tree's reach — its buffers carry `misc 0` (no keyed mutex, so 0008 was never
in play for it), `+d3d11` showed a real `ClearRenderTargetView` to white plus
57 quads of `DrawIndexedInstanced`, and reading the destination window right
after the blit that presents it also read white, independently of Wine —
while `xwd -root` on that exact rectangle at that exact moment read solid
black regardless, on both the Xvfb this project measures on and, re-tested
later, the account's own real desktop. What actually explains it: this dialog
presents through `winex11.drv`'s `x11drv_surface_swap`, which only genuinely
waits for a GL swap to finish when the driver supports
`GLX_OML_sync_control` — absent on both displays tested — and falls back to a
bare `XFlush()` otherwise, which flushes the X11 protocol queue and not the
GPU. The pixel read that follows immediately, from a different thread, was a
coin flip: redrawn correctly every time, read back black roughly half the
time. `patches/office/0010-the-licensing-dialog-races-its-own-present.patch`
adds the missing `glFinish()` — the same thing two sibling code paths in the
same file already do for the same reason. Confirmed on the same read that
returned solid black every time before: the dialog's title, red error icon,
both lines of licence text and both buttons, fully legible.
`patches/office/0009-diagnostics-the-licensing-dialogs-pixels-are-correct.patch`
carries the read-only traces this rests on; the full trace-by-trace account,
including the one real desktop re-test still left to do, is in the docs.

**The "one start in three shows no window" report is the Safe Mode prompt,
and it is now answerable.** After an unclean exit Word offers Safe Mode with
a plain Yes/No messagebox — real Win32 controls, not NetUI — and a script
only watching for the main window cannot tell that apart from a hang. With
`WORD_NO_AUTOCLICK=1` it is plainly visible: `#32770 vis=1
(791,473)-(1128,607)`, the process at 0.7% CPU with its main thread in
`anon_pipe_read`. `run-word.sh` answers it through `tools/uiclick`, which
presses the object where MSAA says it is.

The version of that autoclick this replaces had **two** bugs, and between
them they account for most of this project's "Word disappeared" reports:

* It selected its target **by size** (250<W<450, 60<H<200) on the reasoning
  that Office's own dialogs are larger. The licensing `NUIDialog` is
  **375x178** — inside that box. So every run fired a blind click into the
  licensing dialog, and pressing that dialog's OK is what makes an
  unlicensed Office exit.
* Its offset (+203,+81 from the window origin) lands at (994,554); the
  Safe Mode "No" button is at (971,570)-(1023,596). It was **missing the
  button** — which is the other half of why the prompt looked unanswerable.

What actually flags "the last exit was unclean" is still unfound — not the
Word registry tree, not Wine's `RegisterApplicationRestart` (a stub that
keeps no state). See the docs.

**There is no crash.** Every "Word disappears" report in this project —
after typing, after N seconds, on non-ASCII input — is the same event:
something pressed OK on the licensing dialog, and an Office that has twice
been told its licence cannot be verified exits. Measured, not inferred: run
wine as a job of the invoking shell and `wait` for it, and the answer is
`exit status 0` every time. Not a signal, not an unhandled exception. The
dialog is **modal and holds the keyboard** (`GetForegroundWindow` =
`NUIDialog`, via `tools/winenum -focus`), so keystrokes aimed at the
document go to it instead — and a space bar on a dialog presses its default
button. Confirmed one keystroke at a time: first space dismisses dialog #1,
second space (on dialog #2) and the process is gone. The CJK string that
"crashed" Word began with two spaces.

Consequence for anyone testing: **never send a keystroke without reading
`winenum -focus` first**, and do not let any script click a dialog it has
not identified by class.

**Word, Excel and PowerPoint all reach their main window.** PowerPoint was the
last one, and its cause was not where its error message pointed: it reported
`内存或系统资源不足，无法启动 PowerPoint。` while having 14 GB free. What
actually happened is that **Wine was shadowing the `riched20` Office brought**.

Office loads `Common Files\Microsoft Shared\Office16\Riched20.dll` by full
path, but the name matches a known system dll and builtin wins by default, so
Wine substitutes its own — 9 exports against Office's 66. `oart.dll` then does
`GetProcAddress(h, "MathBuildUp")`, gets NULL, throws
`Art::CTextLayoutException`, and PowerPoint's exception classifier does not
recognise it, so it reports a symptom. The 57 missing exports are Office's own
(equation conversion, LaTeX/MathML/OMML, text-box layout, cloud fonts) —
implementing them is not the answer; not shadowing the file Office brought is:

    WINEDLLOVERRIDES='riched20=n'

Set permanently by `import-office.sh`. Measured with it: PowerPoint's
`PPTFrameClass` window and full ribbon, Excel and Word unchanged (2403 vs 2404
and 1896 vs 1902 distinct colours). `scripts/office-dll-overrides.py` finds
this class of problem by comparing export tables rather than guessing; it
reports four more candidates (`msvcp100`, `msvcp120`, `concrt140`, `inkobj`)
which are **not** set, because none has been shown to matter.

Getting there needed the C++-throw diagnostic actually working, and it had not
been: on x86_64 `RaiseException` is hand-written assembly
(`__ASM_GLOBAL_FUNC`), so a hook added to the portable C version beside it
compiles into a build that never runs it. Moved to `dispatch_exception` and
cross-checked against `+seh` (five throws each, exactly). Its first version
scanned the stack and reported an allocator's *success* path as if it were a
caller; replaced with a real `RtlVirtualUnwind` walk, which produced the chain
from the throw to `POWERPNT.EXE`'s main in one run.

**Excel works; PowerPoint's earlier crash is fixed too.** They shared
one crash and it is fixed. `IWebProviderError` has **three** properties
(`ErrorCode`, `ErrorMessage`, `Properties`) and this tree's WAM broker
answered `get_ResponseError` with `S_OK` and a null pointer — which
C++/WinRT's generated getter wraps without testing, so the caller's next line
is a virtual call through nothing. Implementing two of the three then moved
the fault to Office's CFG dispatch thunk (`jmp rax`) with a garbage target:
slot 8, one past the end of an eight-entry vtable. **A WinRT interface
implemented with most of its methods is not partially working; it is a live
jump into whatever `.rdata` follows the table.** `patches/office/0011-*`.

Excel needed one more: creating a workbook faulted in **our** riched20 —
`cfany_to_cf2w` (`style.c:38`) dereferences a `CHARFORMAT` it never checks,
and Excel's own `ITextHost::TxGetCharFormat` answers `S_OK` without filling
the out pointer. `patches/office/0012-*`. Excel now reaches its Start screen
and opens a workbook with the full ribbon, grid, formula bar and status bar.

PowerPoint stops on a `#32770` saying `内存或系统资源不足，无法启动
PowerPoint。` — a symptom, not a cause. Ruled out, each by measurement: it is
not short of memory (30 GB physical, 14 GB free, 71 GB page file), not
add-ins (`/safe` is identical), not `GetGuiResources` (never called), not the
`.fon` font warnings. Located to `ppcore.dll +0x5ff900`, an
`ok=false; call; if (!ok) MessageBox` shape. See the docs for what the
exception chain does and does not establish.

**Three diagnostics were built to get there, and two of them are generally
useful** (`patches/office/0013-*`): a stack scan at an unhandled fault
(winedbg produces no backtrace for these, and the faults land in a
three-byte CFG thunk with no unwind info — the scan finds the caller anyway),
and the caller of any `MessageBox` (`+msgbox`), which is the only cheap way
to ask "who put that box up?" when Office's dialogs name symptoms. A third,
a stack scan at C++ throws (`+cxxthrow`), is carried but did not fire — the
patch says why and what to check before trusting its silence.

**Where Office actually calls, extracted rather than guessed.** C++/WinRT
emits a `consume_<Interface><...>::<Method>` string for every method it
consumes, so grepping Office's binaries lists exactly what it calls. Against
that list this tree covers everything except `IWebAccountMonitor` and
`IWebAuthenticationCoreManagerStatics3::CreateWebAccountMonitor`. The full
table is in the docs and is the checklist for this area.

*(The account of how the first of those faults was found — kept because the
method is the reusable part.)* Unlike Word's "crash", Excel's is real — a null read, every run, right after its Safe Mode
prompt is answered. Traced to `mso30win32client.dll +0x5775f4` via
`+loaddll`; the containing function came from `.pdata`, not the PDB (see
below); and the two `.rdata` pointers it stages for its own error path name
the caller outright: C++/WinRT's `consume_...IWebProviderError`. The gap was
in this tree — `get_ResponseError` answered `S_OK` with a null out-pointer,
and C++/WinRT's generated getter `check_hresult`s the HRESULT and then wraps
the pointer **without testing it**, so the caller's next line is a virtual
call through nothing. `patches/office/0011-*` implements the
`WebProviderError` class; confirmed with `+webauth` that `get_ErrorCode` is
now reached and answered. A second, different fault sat behind it on the
same MSAL/broker path, in Office's CFG dispatch thunk, and it was **not**
unaffected-because-external as first concluded: the `FindAccountProviderAsync`
experiment (two byte-for-byte different answers, one identical fault) was a
sound measurement read the wrong way. A fault invariant under a change means
the change is irrelevant to it, not that it belongs to someone else — and this
one was `IWebProviderError` being one method short, above.

**Office's own PDBs are unusable for addresses; `.pdata` is not.** Measured
on `Mso30Win32Client.pdb`, fetched for this exact binary: the GUID/age match,
its section table matches the PE's, and `scripts/pdb-symbols.py` agrees with
`llvm-pdbutil` symbol for symbol — and yet only **3.1%** of its 59,585
`.text` publics land on a `.pdata` function start, with no mode in the
delta histogram, so no calibration can rescue it. The bytes settle it: the
PDB's address for a known symbol is `74 4e 48 83 7f 18` (mid-function), the
`.pdata` start is `48 83 ec 48` (a prologue). Use `scripts/pe-pdata.py` for
function bounds on Office's own modules. (VBE7's PDB resolves cleanly, so
this is a property of Office's build, not of PDBs.)

**Getting a WinRT IID without the metadata.** `Windows.Security.winmd` is not
in this prefix. C++/WinRT leaves a name/guid table in any binary that
consumes these types — a UTF-16 interface name, then its IID at the next
16-byte boundary — and `WebView2Host.dll` carries the whole
`Windows.Security.Authentication.Web.Core` set. Verify the rule against an
IID you already know before trusting its answer for one you do not.

**The black blocks are Word's title bar and its Backstage rail; a real defect
was found in this tree while chasing them, and it is not their cause.** Reported as "some windows render as black blocks". They are
not windows: `uidump` names them `[title bar] rect=(43,1,698,48)` and
`[pane]/NAVBAR rect=(1,49,66,760)`, and those are exactly the `rows 0..46` and
`cols 0..64` that read solid black out of the swapchain's own client window.
Intermittent — 2 of the 6 hardware-GL starts whose pixels were read — and
invisible under software GL or with `+d3d11` tracing on, which is what a race
looks like.

Office composites its UI out of three 2048x1280 `MISC_SHARED_KEYEDMUTEX`
atlases, and patch 0008 carried each handoff **entirely in `AcquireSync`**, so
the acquiring thread issued `CopyResource` and `Map` against the *releasing*
device's immediate context — a context another thread owns. The log's own
thread prefix shows all 55 handoffs in a start-up doing it. D3D11 immediate
contexts are single-threaded by contract; Wine's global lock stops it
corrupting anything and does not order the copy against work the owner has
queued, so a copy can read an atlas the owner has not finished drawing, and an
atlas that arrives incomplete blacks out every element it carried.
`patches/office/0015-*` splits the carry: `ReleaseSync` copies out on the
releasing device's own thread, `AcquireSync` copies in on the acquiring one,
with a generation counter so nobody copies twice.

    hardware GL, untraced, before        2 of 6  starts had a black band
    after, the first fourteen starts     0 of 14
    after, the next eleven               5 of 11

**And that is the correction that matters here.** Fourteen consecutive clean
starts read as a fix and were written up as one; it did not hold. 0015 is still
right — issuing `CopyResource`/`Map` against a device's immediate context from
a thread that does not own it is not defensible under D3D11's threading
contract whatever the pixels do, and it happened on all 55 handoffs of a
start-up — but **what causes the black band is still open.**

**And the last of the three: hovering a dialog button turned the dialog black.**
One measurement, one pointer move apart, on the dialog's own X window:

    at rest            545 distinct colours,     0 black pixels
    pointer on 帮助     90 distinct colours,  53488 black  (80%)

with exactly one thing in the log between them —
`fixme:dxgi:d3d11_swapchain_Present1 Ignored present parameters`. Office
redraws the one button that changed and calls `Present1` with a
`DXGI_PRESENT_PARAMETERS` naming that rectangle (measured:
`(283,101)-(349,127)`, the 帮助 button to the pixel). DXGI's contract is that
everything outside it is already correct on screen. Wine presented the whole
back buffer instead — and with a flip model that buffer is not the one the
untouched pixels were drawn into, so everything Office did not redraw came from
whatever it last held: nothing, on its first trip round. Hence 80% black, all
except the button under the pointer.

`patches/office/0021-*` passes the union of the dirty rectangles down as the
present's source and destination rectangle, falling back to a full present for a
scroll rectangle, for a back buffer that is not the window's size, and for the
first present on a swapchain and the first after `ResizeBuffers`. It fixes the
dialog exactly — 545 colours and 0 black at rest, 544 and 0 with the hover
highlight drawn.

**The obvious version of that broke the main window, and why is the useful
part.** Blitting only the dirty region — what DXGI does — gave `rail = 287, 287,
230, 287, 97, 230, 230, 230` over eight starts, against 287–288 in the eight
before. One line of GL semantics explains it: **the drawable is double buffered
and `wglSwapBuffers` exchanges the whole of it**, so a partial blit does not
leave the rest of the window alone, it shows the drawable's *other* buffer
there. "The window keeps what it had" is true of DWM and false of this path.

So the composite is kept on Wine's side: a texture the size of the back buffer
holds the last thing presented, a partial present copies its dirty region into
that and the whole of it back into the back buffer, and the buffer is presented
entire as before. One full-size GPU copy per partial present, and both faults
go. Measured over eight starts: `rail = 287` and `title = 111` in every one, 19
black pixels in every one — text and nothing else, where the same census before
varied and failed outright one start in sixteen.

Also worth carrying forward: Office presents **exactly one complete frame per
swapchain** — 43 on the main window, 1 full and 42 with dirty rectangles; 7 on
the dialog, 1 and 6. Whole-buffer presents had been repairing, every frame,
whatever had not reached the drawable legitimately; the composite now does that
job deliberately rather than by accident.

**Reading the composited screen is possible after all**, which is what made
that measurable. Under Xwayland `import -root` reads nothing and GNOME Shell's
screenshot API refuses unknown callers, so the last session had to stop at
"our pixels are right and what the compositor shows cannot be read". But
`mutter --headless --virtual-monitor 1920x1080` is the same compositor, on the
real GPU, with Xwayland, and it exposes `org.gnome.Mutter.ScreenCast` —
`scripts/measure-desktop.sh` starts it, `scripts/screen-capture.py` grabs a
composited frame, `scripts/xwin-pixels.py` reads any X window's own pixels
*including alpha*, and `scripts/render-census.sh` repeats a start N times
because none of these faults is deterministic. First thing it settled: the
dialog's window and the composited frame agree exactly (545 colours, 0 black,
both), so the compositor is faithful and the black was always in the window.

**Both of them are fixed, and it was one line in this tree.** The rail's labels
and icons, the "新建" heading, the 最近/收藏夹/与我共享 tabs and the search box
all come back, and the black band has not appeared since.

Getting there needed an instrument this tree did not have: what each *draw* was
aimed at. `patches/office/0019-*` gives every draw its render target, viewport,
scissor and shader resources, and `scripts/d3d-draw-map.py` replays a log and
reassembles them. It showed at once that the rail was not a region Office
declines to draw — 320 draws go into the window's swapchain, 62 of them with a
scissor over the rail, every one sampling a **2048x2048 cache texture that has
no render target view**, filled instead by 2006 `CopySubresourceRegion` calls
per start-up out of the three shared 2048x1280 atlases. Matching each glyph
fill's rectangle against those copies' source boxes showed Office *does* copy
the rail's tiles (开始 filled at `(130,3)-(154,14)`, copied from
`(129,0)-(155,16)`). Dumping both sides of that copy named the bug:

    src ... 2048x1280 misc 0x100 ... shared ... gen 0 dirty 0

`gen 0` — the compositing device's copy of the atlas had never been given
anything. 0015 carried a shared surface at `ReleaseSync` and skipped it while
the surface had one participant, on the reasoning that there was nobody to
carry to yet. **The second participant opens the surface after the first has
already drawn into it**, so that first release is the one that matters.
Fixing only that breaks it the other way — a participant that merely *read* the
surface then makes the other side look stale and its next acquire overwrites
what it drew — so the carry is now driven by whether a participant actually
wrote (`shared_dirty`). `patches/office/0018-*`.

    hardware GL                           before            after (16 starts)
    navigation rail, distinct colours     2, always         287-288 in 15
    title bar, distinct colours           1 .. 111          111-221 in the same 15
    solid black bands                     5 of 25 starts    1 of 16

The rail is the result that carries weight: 2 colours in *every* start measured
before — in Excel as well as Word, under software GL as well as hardware, with
the Office UI theme forced either way, and with 0015 reverted — and 287 in
fifteen of the sixteen since. **The sixteenth failed completely** — rail back to
2, title bar to 1, the full black band — which is the other half of the result:
the band and the rail are one fault. What is left of it is the case this
emulation cannot close by bookkeeping: the opener can acquire on key 0 before
the owner has released anything, and on Windows it would see the owner's live
pixels through real shared storage. Making that first acquire wait is the next
move and is not made yet, because it changes blocking on a path where a wrong
guess deadlocks.

**Reading the composited screen is possible after all**, which is what made
that measurable. Under Xwayland `import -root` reads nothing and GNOME Shell's
screenshot API refuses unknown callers, so the last session had to stop at
"our pixels are right and what the compositor shows cannot be read". But
`mutter --headless --virtual-monitor 1920x1080` is the same compositor, on the
real GPU, with Xwayland, and it exposes `org.gnome.Mutter.ScreenCast` —
`scripts/measure-desktop.sh` starts it, `scripts/screen-capture.py` grabs a
composited frame, `scripts/xwin-pixels.py` reads any X window's own pixels
*including alpha*, and `scripts/render-census.sh` repeats a start N times
because none of these faults is deterministic. First thing it settled: the
dialog's window and the composited frame agree exactly (545 colours, 0 black,
both), so the compositor is faithful and the black was always in the window.

**Still outstanding, and now narrowed to a boundary.** After 0015 the rail is
no longer black — it is *empty*, and so are the "新建" heading, the
最近/收藏夹/与我共享 tabs and the search box, while the template captions beside
them render. Unlike the black band this is not a race: it survives `+d2d`
tracing, and a resize does not repair it. What was measured, with two new
diagnostics:

* every missing string reaches Wine's own glyph-run renderer
  (`d2d_device_context_draw_glyph_run_bitmap`) — these are Wine's calls, not
  Office's;
* and Wine does the whole job correctly. `patches/office/0017-*` prints what
  the fill was given: 124 fills in one start-up, **zero** all-zero masks, every
  destination rectangle inside its clip, every transform the translation that
  clip was pushed under, every brush real. 开始 carries 202 set bytes of
  coverage out of 264; 空白文档, which does appear, carries 384 of 576.

So the loss is past `fill_geometry`, in Office's own compositing of the atlas
slots it just filled — its D3D11 draws, which nothing in this tree instruments
yet. That is the next instrument to build.

*A retraction that came with it:* an earlier reading of
`patches/office/0016-*` (which dumps a shared atlas at every handoff, in colour
and in alpha) said the labels "never reach the atlas". They are indeed absent
from all 24 snapshots — and that proves nothing, because the atlas is a
recycling cache: 0017 shows 开始 filled at (130,3) and later at (1,3), 空白文档
at five different rectangles. A snapshot holds whoever occupies the slots at
that instant.

**Reading the composited screen is possible after all**, which is what made
that measurable. Under Xwayland `import -root` reads nothing and GNOME Shell's
screenshot API refuses unknown callers, so the last session had to stop at
"our pixels are right and what the compositor shows cannot be read". But
`mutter --headless --virtual-monitor 1920x1080` is the same compositor, on the
real GPU, with Xwayland, and it exposes `org.gnome.Mutter.ScreenCast` —
`scripts/measure-desktop.sh` starts it, `scripts/screen-capture.py` grabs a
composited frame, `scripts/xwin-pixels.py` reads any X window's own pixels
*including alpha*, and `scripts/render-census.sh` repeats a start N times
because none of these faults is deterministic. First thing it settled: the
dialog's window and the composited frame agree exactly (545 colours, 0 black,
both), so the compositor is faithful and the black was always in the window.

**Still outstanding, and now narrowed to one step.** After 0015 the rail is no
longer black — it is *empty*, and so are the "新建" heading, the
最近/收藏夹/与我共享 tabs and the search box, while the template captions beside
them render. Unlike the black band this is not a race: it survives `+d2d`
tracing and a resize does not repair it. `+d2d` shows Office **does** rasterise
every missing string, through the identical call sequence as the ones that
appear (`DrawTextLayout` → `DrawGlyphRun` → A8 mask → bitmap brush →
`FillGeometry`), differing only in destination rectangle. And
That sequence is **Wine's own** `d2d_device_context_draw_glyph_run_bitmap`,
not Office's. `patches/office/0016-*` — which writes a shared atlas out to disk
at every handoff, something only 0015's system-memory buffer made possible —
shows the atlases carrying what reaches the screen and never the rail's
labels, in the alpha plane as well as in colour (premultiplied BGRA makes
black text indistinguishable from nothing in a colour-only dump; checking one
channel would have made this a guess). So the loss sits between `FillGeometry`
and the atlas. Not yet separated, and the reason nothing in d2d1 has been
changed: "the fill never landed" versus "it landed and the slot was recycled
before the first handoff" — the earliest snapshot is already the fourth
handoff, and D2D fill rectangles do not map 1:1 onto atlas pixels. The
measurement that closes it is the same trick one layer up: dump the D2D target
at `EndDraw` rather than the shared texture at `ReleaseSync`.

**The licence is still unsolved.** `Office.Licensing.FullValidation` fails with `0xc004f015`
(`SL_E_LICENSE_NOT_INSTALLED`), `Office.Licensing.OnlineRepair` returns `S_OK`
in two ticks without going online, and no licensing host is contacted at
start-up. The licence in the prefix was issued to a different device's
`HardwareId`. What was measured, and the long list of things that turned out
**not** to be the gate — the broker, valid tokens, consent, connectivity, the
Click-to-Run service, the App-V layer, the identity caches — is in
[`docs/office365-under-wine.md`](docs/office365-under-wine.md); read that before
repeating any of it.

**Office's own sign-in page works.** The real Microsoft sign-in form renders
inside Word — email field, Next button, the live `odc.officeapps.live.com` page.
Four things have to be true at once, and none of them is the licence:

```sh
scripts/install-c2r-marker.sh          # or Office takes a reduced path (see below)
curl -sLo ~/.cache/wine/wine-gecko-2.47.4-x86_64.msi \
     https://dl.winehq.org/wine/wine-gecko/2.47.4/wine-gecko-2.47.4-x86_64.msi
wine msiexec /i ~/.cache/wine/wine-gecko-2.47.4-x86_64.msi /q   # or you get Wine's Gecko prompt
MINT_HOST=… MINT_USER=… scripts/mint-wam-tokens.sh              # tokens expire in hours
scripts/office-sign-in.sh                                       # leaves the page on screen
```

The fourth is a deletion: **identity caches copied from another machine must be
moved aside.** `IdentityCache` and `OneAuth` under `AppData\Local\Microsoft`
are DPAPI blobs sealed by Windows; Wine rejects every one of them, and while
they are there Office says your account cannot be accessed instead of offering
to sign in. Gone, it shows the ordinary signed-out state and the button works.

Driving the form with Win32 input does not work — MSAA's value setter is
`E_NOTIMPL` on a Gecko field, and `SendInput` keystrokes leave it empty even
with the window focused and raised. **`xdotool` does not**: Gecko is a real X11
client, and `xdotool mousemove ... click` + `xdotool type` land in the field
over the same input path a real X server delivers to any other Linux
application, sidestepping `SendInput`'s Unicode-key emulation entirely (see
`docs/office365-under-wine.md`, "Sign-in's third blocker, found"). So the form
can now be filled without a person's hands on a keyboard; completing the
sign-in is still yours to finish, because what is left is the credential
itself and the decision to spend it against a live Microsoft endpoint.
Whether that licenses this device is untested — it is the obvious next thing to
try, and it is now one command away from being tried.

One Wine gap worth knowing about on its own: **Office decides whether it is a
Click-to-Run install by asking `GetModuleHandleW` whether the Windows App-V
client is loaded in its own process** — nothing else. Wine has no App-V client,
so every Office app reported `Not_C2R`, took a reduced start-up path, and left
its licensing dialog's text empty. `scripts/install-c2r-marker.sh` supplies a
module by that name; Word then reports the same audience as the reference
machine, loads 362 modules instead of 167, and the dialog finally says what it
is. It does not license Office — see the docs — but it is a real gap.

Two traps that each cost an investigation, in case they bite again. Since Wine 9
a prefix keeps **its own copies of the builtin dlls** in `C:\windows\system32`
and loads those, so `make install` alone changes nothing about what Office runs
— a stale deployed binary and a wrong conclusion look identical from the log.
Run `scripts/sync-prefix-dlls.sh` after every build, before every measurement.

The other: the per-user
half of the import must go into the profile **the runtime** uses. `dist-cx`
(the CrossOver base) runs Office as `crossover`, not `$USER`, so a licence
copied to `C:\users\$USER` is invisible and Office correctly reports itself
unlicensed. `import-office.sh` now asks `wine` rather than assuming.

## Docs

* [`docs/office365-under-wine.md`](docs/office365-under-wine.md) — the Office
  work: what was measured, what it rules out, where it stops.
* [`docs/upstream-evaluation.md`](docs/upstream-evaluation.md) — wine-staging,
  Valve and wine-tkg surveyed against this project's actual blocker. 131 + 400
  commits looked at, one taken.
* [`docs/method.md`](docs/method.md) — the instruments, and the traps already
  hit.

## Method

Every claim here is a register read, a memory read, or bytes on the wire. An
inference from a true fact is not a measurement — several plausible ones have
already been wrong. The instruments and the traps are described in
`docs/method.md`.
