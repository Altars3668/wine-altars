# 外壳项目的属性存储：`propstoreprobe.exe`

Wine 的 `IShellItem2` 属性方法（`GetPropertyStore`、`GetString`、`GetUInt64`、`GetFileTime`……）全是桩，Office 显示每个外壳项目
（最近使用的文件等）时都用 `GPS_FASTPROPERTIESONLY` 要一次属性存储，每次普查五十来次 FIXME。shell32 的测试对这些接口没有规格。

探针在 %TEMP% 下建一个 12 字节的文本文件和一个空目录，列出 `SHGetPropertyStoreFromParsingName` 在
`GPS_FASTPROPERTIESONLY` 与 `GPS_DEFAULT` 下给的每个属性（规范名、VARTYPE、文本值；时间只标“一个时间”，路径与名字是探针
自己的），以及 `IShellItem2::GetString(ItemNameDisplay/ItemType)`、`GetUInt64(Size)`、`GetFileTime(DateModified)` 的结果，最后删掉两者。

`results/propstoreprobe.wine.txt`：altars-up（全部 E_NOTIMPL）。Windows 的结果（winref）定下快速属性的集合后再实现。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror propstoreprobe.c -o propstoreprobe.exe -lshell32 -lpropsys -lole32 -luuid`。
