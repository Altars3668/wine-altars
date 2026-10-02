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
`Path_.IsClass` true, `__GENUS` 1 and every property null, for classes with instances and without; Wine gives the
first instance, or `WBEM_E_NOT_FOUND` when there is none, and has no `Path_.IsClass` or `SystemProperties_` yet.

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -municode -o winmgmtsprobe.exe winmgmtsprobe.c -lole32 -luuid`
