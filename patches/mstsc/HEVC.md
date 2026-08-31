# HEVC over RDP: what is known

Microsoft's client decodes HEVC. gnome-remote-desktop cannot send it, because
FreeRDP has no HEVC at all — not a codec id, not a capability, nothing. These
are the facts needed to write it, each read out of the client binary rather than
from documentation, since MS-RDPEGFX does not describe this codec.

## The client's side

`mstscax.dll` carries a complete HEVC decoder: `Hevc420Decompressor`,
`HevcDecompressor`, `HevcAlphaChannelHw`, with DXVA and CPU paths and 444/alpha
support. It is switched on and off by the same family of settings as H.264 --
`EnableH265CPUDecode`, `DisableH265HardwareDecode`, `DisableFirstPartyHEVC`,
`TS_PROP_CORE_ENABLE_HEVC_CPU_DECODE` -- and it refuses to be surprised:

    Client did not advertise HEVC but server enabled it. Cannot proceed.

## The codec id is 0x0010

From the table `FillCodecsUsedString` uses to name the codecs a session used.
Each entry is a family name and the codec ids belonging to it:

    entry 0: name='AVC'              count=3  codec_ids=[11, 14, 15]
    entry 1: name='HEVC'             count=1  codec_ids=[16]
    entry 2: name='RemoteFX Image'   count=2  codec_ids=[5, 9]
    entry 3: name='RemoteFX Texture' count=1  codec_ids=[8]

The AVC row is 0x0B, 0x0E, 0x0F -- exactly FreeRDP's `RDPGFX_CODECID_AVC420`,
`AVC444` and `AVC444v2` -- so the table is codec ids and the reading is checked.
**HEVC is 0x0010**, one past FreeRDP's `RDPGFX_CODECID_MAX`.

## The framing is the AVC420 framing

`Hevc420Decompressor` and `Avc420Decompressor` implement the same interface --
both have a `??_7...@@6BRdpXInterfaceAvcDecoder@@@` vtable -- and their methods
correspond one for one: `BuildQualityMap`, `GetRectangles`, `Decompress`,
`DecompressDXVA`, `DecompressCPUTexture`, `CreateAlphaTexture`. So an HEVC
frame is carried in the AVC420 metablock: the same region rectangles and
quality vector, with an HEVC bitstream in place of the H.264 one.

That is what makes this worth doing rather than a research project: the server
side is a codec id, a capability bit, and an encoder.

## The capability bit is 0x400, on capset version 0x000B0200 and up

Read off the wire, by making the client advertise it. `CRdpGfxCaps::CreateInstance`
takes the HEVC bit as an argument and masks the result with the capset version's
own allowed-flags; running the client twice with that argument forced to 1 and
to 0, and diffing what the server logged:

    hevc=1   0x000B0200 flags 0x620    0x000B0300 flags 0xE20
    hevc=0   0x000B0200 flags 0x220    0x000B0300 flags 0xA20

**The difference is 0x400 in both, on two different capset versions.** That is
the cross-check: one row could be a coincidence, two rows agreeing is the bit.

The other bits are not HEVC: 0x200 is set on both versions either way, 0x800 on
0x000B0300 either way, and 0x20 is `AVC_DISABLED` (this run did not force the
AVC bit).

**0x400 never appears below capset version 0x000B0200.** The allowed-flags mask
in the client's own table stops at 0x62 for everything up to 0x000A0600, so no
amount of asking will get an HEVC flag onto a capset FreeRDP currently knows.

### Two gates keep a shipping client from ever sending it

`RdpGfxProtocolClientDecoder::StartIO` walks all 13 rows of the table and skips:

* **0x000A0701 and 0x000B0101, unconditionally.** Two hardcoded compares against
  the row's friendly-version number, no setting involved.
* **0x000B0200 and 0x000B0300, unless the `RdpPipeTest11_2` property is set.**
  Read by name through the property set, the same mechanism as
  `EnableHardwareMode`, and 0 here.

So this client build advertises HEVC only with that test property on, and forcing
it at a breakpoint is what produced the numbers above.

**It can be answered from outside**, and `rdp-host.c` does:

    GRD_FORCE_PROP='RdpPipeTest11_2=1'

`CTSPropertySet::GetBoolProperty` is slot 0x78 of `??_7CTSPropertySet@@6B@`
(`mstscax.dll+0x6cb828`), with `GetIntProperty` at 0x68 -- exactly the slots the
call sites reach for. Replacing those two vtable entries answers configured
names and passes everything else to the original pointer, so no trampoline and
no instruction-length decoding is needed, and the client is otherwise untouched.
Each override prints itself, so a reading taken through it cannot be mistaken
for the client's own answer.

With it, and with no breakpoint at all, mstsc advertises the 11.x capsets:

    CapsAdvertise[8]: version 0x000B0200 flags 0x00000220 AVC_DISABLED
    CapsAdvertise[9]: version 0x000B0300 flags 0x00000A20 AVC_DISABLED

and with the codec fields poked as well -- they are still 0 here because
`COP::IsHardwarePresentSupported` fails under Xvfb, not because of this setting:

    CapsAdvertise[8]: version 0x000B0200 flags 0x00000600 AVC HEVC
    CapsAdvertise[9]: version 0x000B0300 flags 0x00000E00 AVC HEVC
    CapsConfirm:      version 0x000B0300 flags 0x00000E00 AVC HEVC

That is a full HEVC-capable negotiation against the server, from Microsoft's own
client, and it leaves the single breakpoint free for something else.

**None of this is Wine-specific.** `rdp-host.exe` is an ordinary Win64 PE that
imports only ole32, oleaut32, user32 and gdi32, and it hosts the same
`mstscax.dll` a real Windows has. Copy it to a Windows machine, set the same
environment variable, and the hook works there for the same reason.

Two things differ on Windows, one of them in our favour:

* **The codec fields should not need poking.** They are 0 here because
  `COP::IsHardwarePresentSupported` fails under Xvfb -- no hardware compositing.
  On a machine with a GPU that check is expected to pass, in which case
  `GRD_FORCE_PROP='RdpPipeTest11_2=1'` alone should be enough to get AVC and
  HEVC advertised. **Expected, not measured** -- there is no Windows machine
  here to try it on.
* **The vtable RVA is per build.** `0x6cb828` belongs to mstscax.dll
  10.0.28000.1251 and nothing else. Passing a wrong one would write two pointers
  into whatever lives at that offset, so the host now refuses unless both slots
  point inside the module's image, and says what to do instead. For another
  build, take the RVA from Microsoft's public symbols:

      pdb-symbols.py mstscax.pdb mstscax.dll --list 999999 | grep '??_7CTSPropertySet@@6B@'
      -> 0x006cb828  ??_7CTSPropertySet@@6B@

  and pass it as `GRD_FORCE_PROP_VTABLE`. The slot offsets themselves --
  `GetIntProperty` at 0x68, `GetBoolProperty` at 0x78 -- are interface layout
  and are far more stable than the vtable's address.

What this does *not* do is change `mstsc.exe` itself. The hook lives in the
process that hosts the control, so it is a way to drive Microsoft's engine with
the setting on, not a way to make the shipping client behave differently for an
ordinary user. Making *that* happen would mean injecting into `mstsc.exe` or
patching a signed system DLL, and neither is needed to test a server.

The supported routes were tried first and none of them works:

* **`IMsRdpExtendedSettings::put_Property`** answers `E_UNEXPECTED` for
  `RdpPipeTest11_2`, and for `ForceCapsVersion` and `DisableUDPTransport` as
  well, while answering `E_FAIL` for `EnableHardwareMode`. Two different errors
  from one interface is the useful part: it recognises some names and not
  others, so these internal ones are simply not in what it accepts, and no
  amount of getting the argument types right will change that.
  `rdp-host.c` keeps the instrument as `GRD_EXT_PROPS="Name=1,Other=0"`.
* **The registry**, under `HKCU\Software\Microsoft\Terminal Server Client` and
  its `Default` subkey. The property still read 0.

`ForceCapsVersion` is the other name worth knowing, found next to
`RdpPipeTest11_2` in `.rdata`: it is the `[rsp+0x64]` the same loop compares, and
a non-zero value makes the client advertise **only** the matching version --
including 0x000B0200 or 0x000B0300 *without* `RdpPipeTest11_2`, through a
dedicated compare. Same problem reaching it, but it is the smaller lever if a
way in turns up.

**Before building the server side, decide whether a client that will actually
enable this exists** -- a newer mstsc is the likeliest answer.

### MSRDC and Windows App are the same engine, and it is gated differently

Both ship `rdclientax.dll` -- Windows App carries it under `msrdc/` inside the
MSIX -- at the same version, and their capset tables are **byte-identical**. So
there is one question here, not two. That table is longer than mstsc's, 15 rows
against 13:

    0x0070  0x000B0200  len 4  allowed 0x007e2   HEVC
    0x0071  0x000B0300  len 4  allowed 0x00fe2   HEVC
    0x0072  0x000B0400  len 8  allowed 0x01300
    0x0073  0x000B0500  len 4  allowed 0x02fe2   HEVC

**`RdpPipeTest11_2` is not what gates them here.** The value is fetched into a
local and never read again -- one write from the getter's out-parameter, no
consumer -- so whatever keeps mstsc quiet does not apply. Reading the loop, the
skips found are: 0x65 unconditionally; 0x6b behind an object query and, only in
RemoteApp mode, `GetVersionEx() >= 10.0.22526`; 0x6f behind a
`LoadLibraryW(L"dcomp.dll")` probe, which Wine satisfies. **For 0x70, 0x71 and
0x73 -- the three that can carry HEVC -- no skip was found.**

So there is no evidence that MSRDC or Windows App would refuse to advertise
HEVC, and some reason to think they would. It has not been observed, because
the engine will not connect when driven through the ActiveX control: it routes
into the RD Gateway path, where `GatewayUsageMethod` must be 1 or 2 and ours is
0, and answers with the generic disconnect reason 4872 ("the connection
information was tampered with"). Forcing the value at the trap moves the failure
rather than clearing it -- it then wants a gateway that does not exist.
`SkipAvdSignatureChecks` is a real setting the control accepts, and does not
help. `FileContents` and `RdpFileName` suggest the supported way in is a whole
`.rdp` file rather than properties set one at a time.

### Why it will not connect, and where that ended

Traced as far as it goes without rebuilding MSRDC's connection model:

* Reason 4872 is the **default arm of a jump table** that maps an internal code
  to a disconnect reason. The code it was handed is 0 -- nothing specific was
  set -- so the constant names no check and chasing it leads nowhere.
* The deciding branch is four frames up, in the transport setup: it reads
  `GatewayUsageMethod` and requires 1 or 2. Ours is 0. **The engine routes every
  connection through the RD Gateway path** when driven this way.
* Forcing that value at the trap, and separately forcing the verdict register of
  the call that selects the transport, both move the failure to "An internal
  error has occurred" -- it then wants a gateway that does not exist. Each poke
  shifts the failure rather than clearing it, which is what driving an object
  model that was never initialised looks like.
* `SkipAvdSignatureChecks` is accepted and does not help. `!PublisherBypassList`
  is about local-device redirection, not this. The client's own verbose logging
  is behind a global at `+0xc05ce0` tested in 4910 places; raising it changes
  nothing because these messages go to WPP tracing rather than
  OutputDebugString.
* **`MsRdpClient11NotSafeForScripting` is the newest control this DLL can
  create.** Its type library also declares `MsRdpClient12` and
  `RemoteDesktopClient` -- the latter being the `ApplySettings(rdpFileContents)`
  API that would have been the way in -- but neither has a class factory here:
  the creatable CLSIDs appear twice in the binary, those two appear once, and
  `CoCreateInstance` answers `CLASS_E_CLASSNOTAVAILABLE`.

**The gateway reading above was wrong**, and the correction is worth keeping.
`GatewayUsageMethod == 0` is handled correctly: the transport setup returns
`S_FALSE`, and the call to the reason mapper beside it merely *records* 4872 as
the reason to use if something later fails without setting its own. That is why
4872 names no check and why forcing values around it only moved the failure --
those pokes were breaking a path that worked.

What actually fails is upstream of anything reachable from the control. Two
strings in `msrdc.exe` say what the engine is waiting for:

    OnRelaunchConnection: WARNING - No WinRT connection ...
    LAUNCHSOURCE, LAUNCHSOURCEPID, LaunchPartner, GetForLaunchUri

`msrdc.exe` is the **session host**. It is started by a partner process through
a launch URI and receives its connection settings as a **WinRT** object; the
`.rdp` file alone gets it as far as registering an `avdConnection0` with Windows
Error Reporting and no further -- it never opens a socket. Under Wine it runs,
initialises COM and ETW, and then waits for a WinRT connection that nothing
provides. Wine's WinRT is a stub: `RoGetActivationFactory` is semi-stub and
returns `REGDB_E_CLASSNOTREG` for the classes these clients ask for.

**And the WinRT reading was wrong too.** That came from a string rather than
from watching the process, and watching it says otherwise: run under
`WINEDEBUG=+combase`, `msrdc.exe` **never calls `RoGetActivationFactory` at
all**. It creates a window, registers an `avdConnection0` with Windows Error
Reporting, and idles in a message loop. WinRT is not what it is blocked on --
it never gets that far.

What it is waiting for is an activation it never received. Its launch sources
are named in the binary -- `UriProtocolLaunch`, `FileHandlerLaunch`, `ShellLink`,
`TaskbarPin`, `StartMenuTileLaunch`, `ConnectionCenter` -- and the URI scheme is
**`ms-wvd-hp:`** with an `&aadtenant=` parameter. A Windows Virtual Desktop host
pool and an Azure AD tenant. The other way in is the partner process passing a
connection object, and that partner is `msrdcw.exe`, a WPF application.

Three entry points, all closed here:

* **ActiveX control** -- reaches `Connect()`, then a disconnect is *posted to a
  window* and delivered later, so the stack at delivery names the plumbing and
  not the decision. Finding the decision means following an asynchronous state
  machine with no symbols.
* **`ms-wvd-hp:` URI** -- AVD host pool, needs an Azure AD tenant.
* **Partner process** -- `msrdcw.exe`, WPF, which Wine's mono cannot run.

Every layer of this engine assumes it is being driven by Microsoft's own client
application. That is a reasonable thing for it to assume, and it is why three
successive readings here -- the gateway, the test property, WinRT -- each named
something real and none of them was the blocker. The blocker is that nothing is
driving it.

### What the ActiveX path actually does before giving up

Measured rather than read, which the three readings above were not:

* **No socket is ever created.** Watching `ss` through a whole run: zero
  connections to the port. Under `WINEDEBUG=+winsock` the client calls
  `WSAStartup`, `gethostname`, and `getaddrinfo` for its **own** hostname --
  identity, not the connection -- and then stops. There is no `socket`, no
  `connect`, and the target address is never resolved. The abort is local and
  early.
* **The disconnect is posted, not thrown.** Seven `PostMessageW` calls, all
  `WM_USER+19` to the control's window; the last two come from a chain in the
  RDP core state machine at `+0x3c9c79 -> 0x3f60cb -> 0x3e9d33 -> 0x3edd94 ->
  0x3d10d2`. The window procedure then performs the disconnect, which is why the
  stack at delivery names plumbing.
* **4872 and its message are labels applied at delivery.** The reason is
  `0x1308`: class `0x13`, code `0x08`. Breaking where the class selects a
  resource string confirms it -- string 13901, "the connection information was
  tampered with" -- and that lookup happens *inside* the disconnect delivery.
  The class came from the default arm of a mapper that ran during transport
  setup, meaning nothing on the failing path set a reason of its own.

So the decision is in the state-machine region above, reached through several
virtual dispatches, in a binary with no public symbols. Finding it means
breakpointing that region for the first failing HRESULT, one dispatch at a time.

Two things not to repeat here. This engine has two logging schemes and **both are
unreachable**: one wants a logger object that is never constructed, the other
writes to an ETW session Wine only stubs. A breakpoint on any log site therefore
cannot fire, and its silence proves nothing -- a test on three of them was void
by construction. And every hypothesis formed by reading code rather than
watching the process has been wrong here, four times running.

## Which clients ask for HEVC

One connection from Windows App on Android to gnome-remote-desktop, read out of
the server's own log:

    Client caps set RDPGFX_CAPVERSION_8    flags 0x00000002
    Client caps set RDPGFX_CAPVERSION_81   flags 0x00000002
    Client caps set RDPGFX_CAPVERSION_10   flags 0x00000022
    Client caps set RDPGFX_CAPVERSION_102  flags 0x00000022
    Client caps set RDPGFX_CAPVERSION_103  flags 0x00000020
    Client caps set RDPGFX_CAPVERSION_104  flags 0x00000022
    unknown capability set version 0x000B0101  flags 0x000001A2
    unknown capability set version 0x000B0300  flags 0x000001A2

`0x1A2` is SMALL_CACHE | **AVC_DISABLED** | SCALEDMAP_DISABLE | SCP_DISABLE.
**No 0x400.** This client uses the 11.x capsets -- the only ones that can carry
the HEVC flag -- and does not set it.

What that does **not** close is mstsc, and an earlier version of this file said
it did. The claim was that HEVC is gated by `RdpPipeTest11_2`, which nothing
sets. Setting it is certainly what lit the flag up here -- but here is Wine, and
the reading was of a fallback rather than of the rule.

Microsoft documents the client side as automatic: the flag is advertised when
the local GPU does HEVC **hardware decode** (4K YUV 4:2:0) and the Microsoft
HEVC Video Extension is installed. The registry that goes with it is
`HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services` ->
`HEVCHardwareEncodePreferred`, and that is a **server**-side policy for a
Windows session host -- nothing for a server that is not Windows, and nothing
the client reads.

The binary agrees with that reading. `RdpGfxProtocolClientDecoder::StartIO`
decides which capset versions to advertise, and consults `RdpPipeTest11_2` on
one branch of a comparison against `GetFriendlyVersionNum` -- 101, 107, 111,
112, 113. Whether the HEVC bit goes into them is decided elsewhere, in
`RdpGfxClientChannel::CheckHardwareAndAvcSupport`, which is where the string
`HEVCDecodeCapability` lives. Under Wine that check cannot pass: there is no
DXVA HEVC decoder and neither of the two Media Foundation decoders the client
looks for exists.

And the Android reading above is the proof that the versions are not the gate:
it advertises 0x000B0101 and 0x000B0300 with no test flag anywhere near it. It
simply does not set 0x400.

| Client | Reaches a personal server | Advertises HEVC |
| --- | --- | --- |
| mstsc under Wine | yes | only with `RdpPipeTest11_2` forced -- no decoder to report |
| mstsc on Windows, HEVC-capable GPU | yes | **no** -- measured: offers no capset above 10.6 |
| MSRDC / Windows App on Windows | **no** -- AVD, Windows 365, Dev Box and RDS only | would, probably |
| Windows App on Android | yes | **no** -- measured above |

## Answered on real Windows: mstsc does not offer 11.x at all

The test above was run. Windows 11 on this machine, an RX 6900 XT (hardware HEVC
decode), `Microsoft.HEVCVideoExtension` installed, stock mstsc, connecting to
gnome-remote-desktop over TCP. Read out of the server's log:

    CapsAdvertise[0]: version 0x00080004 flags 0x00000000
    CapsAdvertise[1]: version 0x00080105 flags 0x00000000
    CapsAdvertise[2]: version 0x000A0002 flags 0x00000000
    CapsAdvertise[3]: version 0x000A0200 flags 0x00000000
    CapsAdvertise[4]: version 0x000A0301 flags 0x00000000
    CapsAdvertise[5]: version 0x000A0400 flags 0x00000000
    CapsAdvertise[6]: version 0x000A0502 flags 0x00000000
    CapsAdvertise[7]: version 0x000A0600 flags 0x00000000
    Accepting ... RDPGFX_CAPVERSION_106, AVC444: true, AVC420: true, HEVC: false

**Eight capability sets, stopping at 10.6. No 11.x at all.** The HEVC bit exists
only on 0x000B0200 and 0x000B0300, so there is nowhere for it to go. The client
decoding capability is not what is missing -- this machine has it -- and forcing
`RdpPipeTest11_2` under Wine produces exactly the two versions that are absent
here. The gate is the capability set version, and that property is what opens it
on a real Windows too.

This section said three different things over one day, so it is worth being
plain about which are measurements:

* **Measured.** Stock mstsc on Windows, against a server that is not AVD, offers
  10.6 and no higher. No HEVC.
* **Measured.** mstsc under Wine with `RdpPipeTest11_2` forced offers 0x000B0200
  and 0x000B0300 with the HEVC bit, and decodes an HEVC frame.
* **Measured.** Windows App on Android offers 0x000B0101 and 0x000B0300 with no
  HEVC bit, and with AVC disabled -- so 11.x is reachable without the test
  property, by some client, somehow.
* **Not known.** What makes mstsc take the 11.x path for Azure Virtual Desktop,
  Windows 365 and Dev Box, where Microsoft documents HEVC as working. Something
  about those connections differs from a plain RDP server, and the server-side
  `HEVCHardwareEncodePreferred` policy is for a Windows session host, not
  something a client reads.

What follows from that: **HEVC on this server is real and works, and no stock
Microsoft client will ask for it.** The clients that can use it are the ones
whose capability advertisement we control -- the patched FreeRDP client, which
decodes it -- and mstsc with the property hook, which is a lab instrument.

### The finding worth acting on instead

The same reading shows **AVC_DISABLED on every 10.x and 11.x capset**, so this
client refuses H.264 as well, and the session falls back to RemoteFX
Progressive:

    CapsAdvertise: Accepting capability set with version RDPGFX_CAPVERSION_104,
      Client cap flags: H264 (AVC444): false, H264 (AVC420): false
    Encode session probe: avc444=no avc420=no hwaccel-vaapi=yes amd-gpu=yes

The VAAPI H.264 encoder is initialised and then unused. Why the client disables
AVC is **not** established -- device decoder, policy, or something the server
does or fails to offer are all still open -- and it is a present quality problem
for real sessions rather than a hypothetical one. Windows App does support connecting to a
personal PC -- Microsoft's own documentation lists "Remote Desktop Services, and
a remote PC" beside Azure Virtual Desktop -- so no AVD subscription is needed to
run that test.

**One connection from a real Windows client would settle this**, and the server
now names the flag in its log.

The table itself, at `.rdata+0x85b860`, 0x50 bytes a row: friendly version
number, capset version, capsData length, `~allowed_flags`, then the allowed
flags one per dword.

    0x50  0x00080004  len 4   allowed 0x001
    0x51  0x00080105  len 4   allowed 0x013
    0x64  0x000A0002  len 4   allowed 0x022
    0x65  0x000A0100  len 16  allowed 0x000    skipped unconditionally
    0x66  0x000A0200  len 4   allowed 0x022
    0x67  0x000A0301  len 4   allowed 0x062
    0x68  0x000A0400  len 4   allowed 0x062
    0x69  0x000A0502  len 4   allowed 0x062
    0x6a  0x000A0600  len 4   allowed 0x062
    0x6b  0x000A0701  len 4   allowed 0x0e2    skipped unconditionally
    0x6f  0x000B0101  len 4   allowed 0x1e2    skipped unconditionally
    0x70  0x000B0200  len 4   allowed 0x7e2    needs RdpPipeTest11_2
    0x71  0x000B0300  len 4   allowed 0xfe2    needs RdpPipeTest11_2

The rows agree with MS-RDPEGFX where it documents them -- 0x02 SMALL_CACHE,
0x20 AVC_DISABLED, 0x40 AVC_THINCLIENT -- which is what makes the rest of the
table worth trusting. FreeRDP knows nothing above 0x000A0701.

## Wine now has an HEVC decoder

`winegstreamer-hevc-decoder.patch`. Media Foundation had no HEVC decoder at
all -- `MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_HEVC)` returned
nothing -- so anything that asks this machine whether it can decode HEVC was
told no. `mft-probe.exe` asks that question directly, which is how each of the
four gaps below was told apart from the others:

    H.264  decoders: count=1  activate -> usable
    HEVC   decoders: count=1  activate -> usable

Four separate things were missing, and fixing any three of them changes nothing:

* **the decoder itself.** `hevc_decoder_create` in `video_decoder.c`, beside
  the H.264 one, over the same `video_decoder_create_with_types`.
* **the COM class.** A CLSID in `mfplat.c`'s factory table is not enough; the
  class also needs a `coclass` in `winegstreamer_classes.idl` or nothing can
  create it. Note that `regsvr32` hangs in this prefix -- a known problem
  recorded in `tests/wine-mstsc/setup.sh` -- so the registry entry has to be
  written by hand, as `setup.sh` already does for the control.
* **the MFT registration**, so `MFTEnumEx` can find it. This is done from
  `msmpeg2vdec`, which registers the H.264 one the same way, and not from
  winegstreamer: that DLL delay-imports mfplat and calling into it from its
  `DllRegisterServer` hangs `regsvr32`.
* **the media type mapping.** `wg_media_type.c` turns an `MFVIDEOFORMAT` into
  GStreamer caps and knew nothing of HEVC, so the caps named no codec, no
  decoder matched, and the answer came back no while `avdec_h265` was installed
  all along. This is the one that took the longest to see, because every layer
  above it looked correct.

## Why the client advertised no codec

Every capset it sent carried `RDPGFX_CAPS_FLAG_AVC_DISABLED`, with a usable
H.264 decoder and a usable HEVC decoder in Media Foundation. The whole chain is
now measured, and the answer is not in the protocol at all.

### The field that decides, and the proof that it decides

`OnChannelOpened` computes the two codec bits it passes to
`RdpGfxProtocolClientDecoder::StartIO(int avc, int hevc)`:

    18028fd5e: cmp DWORD PTR [r15+0x90], ebp   ; ebp == 0
    18028fd65: jne 18028fd73
    18028fd67: mov r8d, ebp                    ; HEVC = 0
    18028fd6a: cmp DWORD PTR [r15+0x94], ebp
    18028fd71: je  18028fd76
    18028fd73: mov r8d, esi                    ; HEVC = 1
    18028fd76: cmp DWORD PTR [r15+0x88], ebp
    18028fd7d: jne 18028fd8a
    18028fd7f: mov edx, ebp                    ; AVC = 0
    18028fd81: cmp DWORD PTR [r15+0x84], ebp
    18028fd88: je  18028fd8c
    18028fd8a: mov edx, esi                    ; AVC = 1

so AVC is advertised iff `[r15+0x88] || [r15+0x84]`, HEVC iff
`[r15+0x90] || [r15+0x94]`. All four measured 0.

Reading a field only suggests it is the gate. Writing it settles the question:
with `GRD_BREAK_POKE=r15+0x88=1`, one four-byte store at the trap, every capset
changed on the wire in the same run --

    CapsAdvertise[2]: version 0x000A0002 flags 0x00000020    before
    CapsAdvertise[2]: version 0x000A0002 flags 0x00000000    after

and the server brought up an H.264 encoder for the session. `[r15+0x88]` is the
AVC gate, and nothing about the object's layout had to be worked out to know it.

### Who writes it

`RdpGfxClientChannel::CheckHardwareAndAvcSupport` (`+0x28c9d8`), and it writes
exactly three fields, all through `r13` holding the primary `this`:

    18028cbb5: mov DWORD PTR [r13+0xb8], eax    AVC
    18028cc56: mov DWORD PTR [r13+0xc0], eax    HEVC
    18028ccf9: mov DWORD PTR [r13+0xc8], eax

`r15` in `OnChannelOpened` is an interface sub-object, not the primary `this` --
`[r15]` is `??_7RdpGfxClientChannel@@6BIWTSVirtualChannelCallbackPrivate@@@`.
The displacement between them is **0x30**, and that is measured rather than
assumed: within one run `rcx+0xb8` at this function's entry and `r15+0x88` at
`OnChannelOpened` are the same address, and 0xc0-0x90 gives the same 0x30
independently. Earlier scans for a writer of `[this+0x88]` failed because they
used the sub-object as the base.

A hardware watchpoint would have found this without any of that arithmetic, and
it is implemented -- `GRD_WATCH_MEM` -- but **it does not work under Wine**.
Wine accepts `Dr0` and `Dr7`, stores them, and hands them back unchanged on a
read, and a write to the watched address raises nothing. `GRD_WATCH_SELFTEST`
watches a variable the host writes itself, which is what turns that from a
guess into a fact; run it before believing any watchpoint result here.

The symbols were what actually found the writer: `CheckHardwareAndAvcSupport` is
its name in the public PDB, one grep away, and no scan was needed at all.

### The chain, and where it stops

Each step measured at a breakpoint:

1. `CTSGraphics::GetColorDepth` -> **16**. The conversion of that returns 16,
   the code requires 33, and everything after is skipped. **The colour depth was
   the test harness's own doing** -- `rdp-host.c` had never set `ColorDepth`,
   and 16 is what the control defaults to. It now sets 32.
2. With 32bpp: the `EnableHardwareMode` setting is read through the plugin
   config object and returns **1**. Past the gate.
3. `RdpWinLegacyUIManager::IsSupported(presentation=1, pixelFormat=2, 0)`
   through the object at `[this+0x158]`, which is where the answer comes from:
   * `RdpXGraphicsUtil::ValidateVideoDecodeCapability` -> **0, success**. The
     decoder is accepted. Wine's Media Foundation H.264 decoder is good enough
     for the client.
   * `COP::IsHardwarePresentSupported` -> **0**, and the return is
     `(eax != 0)`, so `IsSupported` returns 0 and the AVC field stays 0.

**The blocker is hardware presentation, not a codec and not the protocol.** The
client wants to present decoded frames through a hardware compositing path; on
Xvfb with software rendering it has none. That is a property of the host this
harness runs on, and a real client on real hardware does not hit it -- so it is
not something a server can fix, and not something worth fixing in Wine to get
this work done.

For testing, `GRD_BREAK_POKE` forces the gate, and the client then advertises
AVC and negotiates it with the server for real.

### What the client knows that FreeRDP does not

The capset descriptor table above, and the two gates over it. Nothing about the
codec bits changed when the harness ran at 16-bit colour rather than 32 -- the
table is read the same either way -- so those readings stand.

## The order to build it in

1. **Done for AVC**, and it is a test-harness setting plus a poke rather than
   anything on the server: 32-bit colour, and `GRD_BREAK_POKE=r15+0x88=1` to
   step over the hardware-presentation check this host cannot pass. The client
   then advertises H.264 and negotiates it for real.
2. **Done.** The bit is 0x400 on capset version 0x000B0200 and 0x000B0300 --
   see above. To reproduce, against a server that logs the capsets:

       FreeRDP-3.30/build/server/shadow/cli/freerdp-shadow-cli \
         /port:3406 /sec:tls -auth &

       WINEPREFIX=~/.wine-mstsc DISPLAY=:77 \
       PATH=~/Sources/GitSources/wine-patched/bin:$PATH \
       GRD_BREAK_RVA=0x481cf9 GRD_BREAK_REPEAT=1 \
       GRD_BREAK_POKE='rsp+0x68=1,rsp+0x6c=1' \
         wine ~/.wine-mstsc/drive_c/rdp-host.exe 127.0.0.1:3406 mstsctest 'Passw0rd!' 14

   `rsp+0x68` is the `RdpPipeTest11_2` answer and `rsp+0x6c` is the HEVC
   argument; drop the second to get the baseline to diff against. Add
   `GRD_BREAK_POKE=r15+0x88=1` at `GRD_BREAK_RVA=0x28fd5e` instead to clear
   `AVC_DISABLED` -- the two cannot be done in one run, since there is one
   breakpoint.
3. Add capset versions 0x000B0200 and 0x000B0300, the 0x400 capability, and
   `RDPGFX_CODECID_HEVC = 0x0010` to FreeRDP's rdpgfx server, reusing the AVC420
   metablock path. The two new versions are not optional: 0x400 cannot appear on
   any version FreeRDP has today.
4. Add a VAAPI HEVC encode session to gnome-remote-desktop beside the AVC one.
   The card here can do it: `VAProfileHEVCMain` and `VAProfileHEVCMain10` both
   offer `VAEntrypointEncSlice`.


## Inside Wine: where the client actually stops

Wine is not the obstacle. `winegstreamer-hevc-decoder.patch` puts an HEVC
transform in Media Foundation, and with `WINEDEBUG=+mfplat` the client is seen
to find it, build it and set it up completely:

    mfplat:MFTEnumEx MFT_CATEGORY_VIDEO_DECODER, 0x3f, {MFMediaType_Video,MFVideoFormat_HEVC}
    mfplat:transform_activate_SetGUID  MFT_TRANSFORM_CLSID_Attribute, {9dfcdf2e-...}
    mfplat:hevc_decoder_create
    mfplat:transform_SetInputType
    mfplat:transform_GetOutputAvailableType   (x3)
    mfplat:transform_SetOutputType
    mfplat:transform_ProcessMessage           (x2)

GStreamer builds a real `vah265dec` pipeline underneath it. **And then
`ProcessInput` is never called** -- not once, in any run. The client configures a
working decoder, receives the frame, and does not feed it.

So the protocol error does not come from a missing decoder. It comes from the
client's own `HevcDecoder::DecodeHeader`, which parses the bitstream before
building the sample, and which a breakpoint shows being handed exactly the bytes
we sent. Whatever it wants of an HEVC access unit, our stream does not have it --
and that is now a question about the stream, answerable without Wine in the way.

Two things tried and ruled out:

* **The Store decoder.** `Microsoft.HEVCVideoExtension` on the Windows install
  next door registers `H265Decoder.CH265DecoderTransform` -> `HEVCDECODER_STORE.dll`,
  a WinRT activatable class with no CLSID anywhere -- the app state repository
  records only the class name and the path. Copying the DLL in and registering
  the class changes nothing: the client never calls `RoGetActivationFactory` for
  it. It enumerates through Media Foundation, which is how it finds Wine's.
* **An access unit delimiter.** The H.264 path here sends one in front of every
  frame and the HEVC path did not, which looked like exactly the sort of thing a
  parser scanning for access units would miss. Sending one changes nothing --
  `ProcessInput` is still never called. It is kept regardless: it is what the
  H.264 path does, and our own client is unbothered by it.
