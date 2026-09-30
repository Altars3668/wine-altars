# `winmgmts:` 显示名怎样找到解析器：`displaynameprobe.exe`

脚本里的 `GetObject("winmgmts:")` 就是 `MkParseDisplayName`：它先向 ProgID 的类对象要 `IParseDisplayName`，
要不到才经 `IClassFactory` 建实例再要。Wine 的 wbemdisp 类对象只给 `IUnknown`/`IClassFactory`，第一步得到
`E_NOINTERFACE`，COM 于是去找并不存在的本地服务器，每次都在日志里留下两条 ERR（`create_server ... not registered`、
`no class object ... could be created for context 0x15`），回退之后查询才成功。

探针分别问：

- wbemdisp 的 `DllGetClassObject` 对 `WinMGMTS`、`SWbemLocator` 各给哪些接口，给出的是否同一个对象，给出的
  `IParseDisplayName` 能不能直接解析 `winmgmts:`；
- `CoGetClassObject` 在只注册了进程内服务器、而它拒绝所要接口时，按 `CLSCTX_INPROC_SERVER`、`0x15`、`CLSCTX_ALL`
  各回什么（Wine 在后两种里把进程内的 `E_NOINTERFACE` 换成了 `REGDB_E_CLASSNOTREG`）；
- `CoCreateInstance` 各接口的结果；
- `MkParseDisplayName` 对几个 WMI 名字的结果、吃掉的字符数、名字对象的种类（`MKSYS`、类 ID）、显示名，以及绑定出的
  对象类型。

`results/displaynameprobe.wine.txt`：Wine（altars-up）的输出；Windows 的待测（winref）。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror displaynameprobe.c -o displaynameprobe.exe -lole32 -loleaut32 -luuid`。
