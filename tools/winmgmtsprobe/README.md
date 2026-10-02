# winmgmtsprobe

How `winmgmts:` becomes a moniker.  `MkParseDisplayName` takes the prefix as a ProgID and binds a class moniker to
the class asking for `IParseDisplayName`; the probe prints what each step gives: `CoGetClassObject` for WinMGMTS
asked for `IParseDisplayName` and `IClassFactory`, in process and with `CLSCTX_ALL`, `CoCreateInstance` asked for
`IParseDisplayName`, and the whole parse with the kind of moniker it returns.  SWbemLocator is the control: a class
object that has no reason to parse names, to see whether lacking the interface fails as `E_NOINTERFACE` or as a
class that is not registered.

    winmgmtsprobe.exe

On Windows 11 (`results/winmgmtsprobe.win.txt`) the class object lacks `IParseDisplayName` too, and says
`E_NOINTERFACE` in every context; `MkParseDisplayName` gets one through `IClassFactory` and returns a pointer
moniker (kind 5).  Wine said `REGDB_E_CLASSNOTREG` with `CLSCTX_ALL`, once it had also tried a local server, with
three ERR lines on the way; since altars-up `23fa620648c` it answers as Windows does.

`classobject.vbs` asks what a class path gives (`results/classobject.win.txt`): on Windows the class itself, with
`Path_.IsClass` true, `__GENUS` 1 and every property null, for classes with instances and without, and
`WBEM_E_NOT_FOUND` for a class that does not exist.  Wine gave the first instance, or `WBEM_E_NOT_FOUND` when there was
none; since altars-up `3079386d816` it gives the class (`results/classobject.wine.txt`).

## Paths, keys and the scripting objects

Windows 11 build 29671 (winref, batches 25 to 30); the Wine results are altars-up `b3999412b70`.

- `objectpath.vbs`: `SWbemObject.Path_` and `SystemProperties_` for classes and instances, and what `Get` gives for paths
  with and without keys.  Wine differs in what its tables lack (`__SUPERCLASS`, property and method counts) and in the
  namespace's spelling: Windows writes an instance's namespace as the connection spelled it, `root\cimv2` or
  `ROOT\CIMV2`, and a class's as the repository has it.
- `dispids.c`: the DISPIDs of `SWbemObject`'s members, and that it is an `ISWbemObjectEx` (26 `Refresh_`,
  27 `SystemProperties_`, 28 `GetText_`, 29 `SetFromText_`).
- `pathprobe.c`: what wmiutils' `IWbemPath` makes of 42 paths -- info flags, the text in every form, server, class, and
  each key through `GetKey` and `GetKey2` with every flag -- and what `SetKey`, `SetKey2`, `RemoveKey` and
  `MakeSingleton` write.  Wine's output is the same but for bytes Windows never initialised (a stale stack word read as
  the rest of a number's text, a `VARIANT_BOOL`'s padding).
- `pathobject.vbs`: a `WbemScripting.SWbemObjectPath` of its own: every property for a set of paths, each key's type,
  and what each setter does.  Wine's output is the same.
- `contextprobe.c`: `IWbemContext`'s names, their order, enumeration and deletion.  Wine's output is the same.
- `namedvalues.vbs`: `SWbemNamedValueSet`, an object path's `Keys`, `DisplayName` with every setting, monikers with a
  locale, and the `Path_` of a query's result that left the key out.  Wine differs in `TypeName`, the namespace's
  spelling and `Keys.Add "N", Null`, which Windows takes as a garbage number.
- `privileges.vbs`: `SWbemPrivilegeSet` and `SWbemPrivilege`, and scripting's `ExecQuery` with every flag.  Wine
  differs in `TypeName`, the privilege's display name (advapi32's `LookupPrivilegeDisplayNameW` is a stub) and its
  smaller `Win32_Process`.
- `locatable.c`: what `IWbemServices::ExecQuery` gives for a query that leaves the keys out, with and without
  `WBEM_FLAG_ENSURE_LOCATABLE`, on the probe's own process; and the namespace an instance's path has when the
  connection spells it in capitals.
- `classinfo.c`: what each scripting object says it is: Windows answers `IProvideClassInfo` with its class
  (`SWbemServicesEx`, `SWbemObjectEx`, `SWbemNamedValue`...), which is what `TypeName` prints; Wine has no
  `IProvideClassInfo` yet.

Build the C probes with `x86_64-w64-mingw32-gcc -O2 -Wall -o <probe>.exe <probe>.c -lole32 -loleaut32 -luuid`
(`-lwbemuuid` for `locatable.c`); run the scripts with `cscript //nologo <script>.vbs`.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -municode -o winmgmtsprobe.exe winmgmtsprobe.c -lole32 -luuid`
