# winmgmtsprobe

How `winmgmts:` becomes a moniker.  `MkParseDisplayName` takes the prefix as a ProgID and binds a class moniker to
the class asking for `IParseDisplayName`; the probe prints what each step gives: `CoGetClassObject` for WinMGMTS
asked for `IParseDisplayName` and `IClassFactory`, in process and with `CLSCTX_ALL`, `CoCreateInstance` asked for
`IParseDisplayName`, and the whole parse with the kind of moniker it returns.  SWbemLocator is the control: a class
object that has no reason to parse names, to see whether lacking the interface fails as `E_NOINTERFACE` or as a
class that is not registered.

    winmgmtsprobe.exe

Under Wine (altars-up `8bf54fc4b20`) the class object lacks `IParseDisplayName`: `E_NOINTERFACE` in process,
`REGDB_E_CLASSNOTREG` with `CLSCTX_ALL` once ole32 has also tried a local server, and ole32 prints three ERR lines
before its fallback through `IClassFactory` parses the name.  The parse gives a pointer moniker (kind 5).

Build: `x86_64-w64-mingw32-gcc -O2 -Wall -municode -o winmgmtsprobe.exe winmgmtsprobe.c -lole32 -luuid`
