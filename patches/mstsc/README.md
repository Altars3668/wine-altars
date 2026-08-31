# Wine patches

What Microsoft's RDP client needs from Wine that Wine does not have. Each of
these was found by running the client and reading what it said — see
`tests/wine-mstsc/README.md` for how, which is more useful than any of the
patches individually.

Against Wine 11.0. All thirteen apply to a pristine tree with `patch -p1`.

    tar -xf wine-11.0.tar.xz && cd wine-11.0
    for p in .../wine-patches/*.patch; do patch -p1 < "$p"; done
    autoconf -o configure configure.ac      # srpapi adds a makefile
    mkdir build && cd build
    ../configure --enable-archs=x86_64 --prefix=/somewhere --disable-tests
    make -j"$(nproc)" && make install

Building 64-bit only is deliberate: the client is 64-bit, and a full multiarch
build costs several times as much for nothing.

`tests/wine-mstsc/setup.sh` notices a Wine that has these — it looks for
`srpapi.dll` in the Wine installation — and skips every prefix-local
replacement it would otherwise install. Pointing `PATH` at a patched Wine is
the whole switch.

## The patches

**`iphlpapi-network-connectivity-hint.patch`** — `GetNetworkConnectivityHint`
and `NotifyNetworkConnectivityHintChange`, from Windows 10 2004. The client
delay-imports them, and a delay-import that cannot bind raises
`STATUS_DELAY_LOAD_FAILED` rather than returning an error, so the process dies
with nothing written anywhere.

The hint is derived from the routing table rather than answered with a
constant: a default route means the host can reach the internet, only local
routes mean it cannot, none means no connectivity. The notification variant is
honest about being a stub — Wine reports no route changes either
(`NotifyRouteChange2` is a stub), so there is nothing to hang a subscription
on. It still delivers the current state once, which is what most callers want.

**`setupapi-cm-open-devnode-key.patch`** — `CM_Open_DevNode_Key` was a stub
returning `CR_SUCCESS` **without opening anything**, which hands the caller an
uninitialised `HKEY`; `CM_Open_DevNode_Key_Ex` was not implemented at all, and
calling a Wine stub raises an exception rather than returning a failure code.

Both are implemented against the registry, in `devinst.c` where the device tree
lives: the hardware key under `Enum\<instance>\Device Parameters`, the software
key named by the device's `Driver` value under `Control\Class`. Wine keeps the
device tree where Windows does, so this can be real rather than stubbed.

**`ole32-clipboard-enterprise-info.patch`** — `OleGetClipboardWithEnterpriseInfo`.
`OleGetClipboard` plus what Windows Information Protection knows about the data
on the clipboard. Wine enforces no such policy, so all four out-strings are
`NULL` and callers read the data as unmanaged, which is correct.

**`srpapi-new-dll.patch`** — the Windows Information Protection queries, which
Wine has none of. Callers load it by name and give up with
`ERROR_MOD_NOT_FOUND`, so the module existing is most of the point. The answers
are what a machine outside any such policy should give: no policy, no
enterprise identities, execution allowed. Functions whose shape is not
documented are left as `@ stub` rather than guessed at.

**`kernelbase-mui-resource-redirection.patch`** — the big one, and the one with
the widest effect beyond this client.

Windows keeps a module's dialogs, strings and menus in a separate
`<dir>\<language>\<module>.mui` and redirects resource lookups there. Wine does
not, and `GetFileMUIPath` returned `ERROR_CALL_NOT_IMPLEMENTED`. A module built
that way carries **no `RT_DIALOG` and no `RT_STRING` at all**, so every dialog
it opens and every string it loads fails — and code that reads a failed dialog
as a refusal takes decisions nobody made. `mstsc.exe` cannot start for this
reason, and it is not alone.

`FindResourceExW` now falls back to the module's `.mui`, cached per module and
searched over the user's then the system's preferred UI languages before
`en-US`. `LoadResource` checks which module's resource directory the `HRSRC`
actually lies in, because an entry found in the `.mui` carries an offset
relative to that file rather than to the module it was asked about.
`GetFileMUIPath` is implemented over the same search.

Not covered: `EnumResourceTypes`/`EnumResourceNames` still enumerate only the
module's own directory, so a `.mui`'s resources are findable but not listable.
Nothing so far has needed that, and it is worth doing before this goes
upstream.

**`ntdll-tracelogging-decoder.patch`** — decode TraceLogging events instead of
discarding them.

Most of what a modern Windows component reports about itself goes through ETW,
and TraceLogging events carry their own schema: provider name, event name, and
every field name and type travel with the event. They can therefore be decoded
without the manifest a classic ETW consumer needs. Wine discarded them at a
FIXME.

A provider stays silent until a session enables it, and TraceLogging checks a
flag set by the enable callback rather than testing per event -- so
`EtwEventRegister` has to invoke that callback or nothing is ever written. All
of it is behind `WINEDEBUG=+tracelog`; off, providers stay as silent as before.

Worth knowing: the RDP client this was written for turned out to report the
interesting parts through the older WPP tracing rather than TraceLogging, so it
did not answer that question. It is a general capability regardless.

**`msxml3-attribute-typed-value.patch`** — the one that finally let the client
connect. `IXMLDOMAttribute::put_nodeTypedValue` was a FIXME returning
`E_NOTIMPL`.

The RDP client builds its connection parameters as an XML document and sets a
numeric attribute on it. The `E_NOTIMPL` propagated out through six layers --
`CRDPENCConnectorStringSerializer::SetConnectorId`,
`CRdpWinConnector::StartConnection`, `CTSTcpTransport::Connect`,
`DoStartConnect`, the connection handler -- and arrived as a bare disconnect
with no socket ever opened and nothing said about why.

An attribute with no schema data type has no typed value distinct from its
text, so this stores the coerced string, which is what `put_value` does for the
same node and what reading it back produces.

**`sspicli-prepare-for-cred-read.patch`** — `SspiPrepareForCredRead` was
`@ stub`, and calling a Wine stub raises an exception rather than returning a
failure code, so a caller merely asking whether it has saved credentials for a
target dies instead. It is the read counterpart of `SspiPrepareForCredWrite`,
which is implemented a few lines above it, and this follows the same
conventions.

**`advapi32-credread-not-found.patch`** — `CredReadW` answered every credential
type but the two it stores with `ERROR_INVALID_PARAMETER`. Those types are
valid; Wine simply keeps no credentials of them, and the truthful answer to
"have you got one of these" is `ERROR_NOT_FOUND`. Callers act on the
difference: not-found sends them on to whatever credentials they were given,
while invalid-parameter says the call itself was malformed and they abandon
what they were doing. A Remote Desktop client asks for type 6 --
`CRED_TYPE_DOMAIN_EXTENDED`, which `wincred.h` was also missing, leaving
`CRED_TYPE_MAXIMUM` a value short -- and stopped there. With this it goes on to
try the other targets it knows.

**`schannel-context-attributes.patch`** — two context attributes Schannel did
not answer, each of which made the RDP client abandon a working TLS connection.

`SECPKG_ATTR_PACKAGE_INFO` describes the package that secured the context.
Wine returned `SEC_E_UNSUPPORTED_FUNCTION` for it, and the client reported
"An authentication error has occurred (Code: 0x80090302)" -- that same status,
surfaced as an authentication failure -- and disconnected.

`SECPKG_ATTR_SESSION_KEY` is answered with keying material exported from the
session under RFC 5705, since GnuTLS does not hand out the master secret. It is
stable per session and different between sessions, which is what callers
binding their own state to a connection need. **It is not the literal session
key Windows returns**, so anything that must agree with a peer about this value
byte for byte will not.

**`winegstreamer-hevc-decoder.patch`** — an HEVC decoder transform, alongside
the H.264 one. Media Foundation under Wine had none, so anything that asked
whether this machine can decode H.265 was told no. GStreamer decodes it, and the
same transform serves.

It is enough for Microsoft's RDP client: with it, `MFTEnumEx` for
`MFVideoFormat_HEVC` finds the transform, the client builds and configures it,
and decodes an HEVC frame from gnome-remote-desktop — `ProcessInput` 151,
`ProcessOutput` 152, and the client reporting 151 frames decoded. See
`tests/hevc-bitstream/README.md`.

**`ntdll-win11-24h2-version.patch`** — a Windows 11 24H2 entry in Wine's version
table, build 26100, selectable as `winecfg -v win11_24h2`. Wine's newest is
`win11` at build 22000, and Microsoft's RDP clients have checks of the form
`GetVersionExW() >= 10.0.22526` behind features added after that. Nothing else
changes: the existing `win11` keeps its build number, so no application that
depends on it moves.

Worth knowing what this did **not** solve. The 22526 check in `rdclientax.dll`
gates capset version 0x000A0701, not the 11.x ones, and only when RemoteApp mode
is on -- so it is not what keeps HEVC off the wire. See `HEVC.md`.

**`secur32-credssp-package.patch`** — a CredSSP security package, which Wine
has never had. **Microsoft's RDP client authenticates through it.** Against
gnome-remote-desktop with Network Level Authentication required:

    [RDP] Captured NLA-authenticated username: 'mstsctest'
    [RDP] Authenticated using NTLM, not applying any additional policy.
    DVC ChannelId 1 creation succeeded (name=Microsoft::Windows::RDS::Graphics)
    [RDP] Layout manager: Creating PipeWire stream for node id 35

and the client reports `OnConnected`. A full session, graphics pipeline and all.

The package is the whole MS-CSSP exchange: its own TLS session through
Schannel, SPNEGO/NTLM tokens carried inside DER-encoded TSRequests, the public
key of the TLS session signed with the authentication context to bind the two
together, and the credentials delegated at the end.

Two things about it are worth knowing before changing it:

**The last authentication token must travel with the public key.** MS-CSSP has
the client send both in one TSRequest, and a server whose own authentication has
nothing further to say does not wait for another message -- it reads the public
key out of the request that completed the authentication. Sending them in
sequence, which is the obvious reading, leaves that field empty and FreeRDP
answers "Encrypted message buffer too small".

**`winbind` must be installed.** Wine's NTLM is a front end for Samba's
`ntlm_auth`; without it `AcquireCredentialsHandle(L"NTLM")` fails with "Can't
start ntlm_auth" before any token is produced.

Three things outside the package had to be true first, and each was its own
investigation -- `tests/wine-mstsc/README.md` has them:

* the client had to be `MsRdpClient11NotSafeForScripting`, since the scriptable
  control refuses to act on credentials it was handed;
* `CredReadW` had to answer not-found rather than invalid-parameter, which is
  `advapi32-credread-not-found.patch`;
* the prefix needed the usual `TERMSRV/*` credential delegation policy.

## Verified

With these applied and no prefix-local replacement of any kind:

    FindResource(RT_DIALOG, 13226)      -> a real DLGTEMPLATEEX  (NULL on stock Wine)
    LoadString(300)                      -> "16"                 (nothing on stock Wine)
    GetFileMUIPath                       -> C:\windows\system32\en-US\mstscax.dll.mui
    GetNetworkConnectivityHint           -> InternetAccess, Unrestricted
    CM_Open_DevNode_Key_Ex               -> CR_NO_SUCH_DEVINST for an empty device tree
    OleGetClipboardWithEnterpriseInfo    -> present
    SrpGetEnterprisePolicy               -> ENTERPRISE_POLICY_NONE
    SrpDoesPolicyAllowAppExecution       -> allowed

    EnumResourceTypes on a split module  -> the .mui's types, then its own

**With these, Microsoft's RDP client connects.** It completes the TCP handshake,
sends its X.224 Connection Request, and is turned away by the server for the one
remaining reason:

    The remote computer requires Network Level Authentication, which your
    computer does not support.

-- which is the client's own wording, read out of its .mui by the resource
redirection above. Wine has no CredSSP package, so NLA is the last gap, and it
is a real one rather than the several things that merely looked like it. See
`tests/wine-mstsc/README.md`.
