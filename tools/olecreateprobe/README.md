# OleCreate 对没有进程内服务器的类：`olecreateprobe.exe`

CrossOver 在 `OleCreate` 里 `CoCreateInstance(INPROC_SERVER|INPROC_HANDLER)` 失败后一律改用
`OleCreateDefaultHandler`。本探针在 `HKCU\Software\Classes`（参数 `hklm` 则在 HKLM）临时注册一个类的几种形态——
不注册、只有键、只有 LocalServer32、InprocHandler32=ole32.dll（带或不带 LocalServer32）——分别打印
`CoCreateInstance` 与 `OleCreate`（OLERENDER_NONE、OLERENDER_DRAW）的结果、对象是否在运行、`Run` 的结果，最后删除注册。
LocalServer32 指向不存在的程序。

`results/olecreateprobe.win.txt`（Windows 11 build 29671）：

- 只有 LocalServer32 的类，`OleCreate` 与 `CoCreateInstance` 一样返回 REGDB_E_CLASSNOTREG——**不会**退回默认处理器，
  CrossOver 那一改不是 Windows 行为。
- InprocHandler32=ole32.dll 时 OLERENDER_NONE 成功、对象未运行，`Run` 返回 REGDB_E_CLASSNOTREG；OLERENDER_DRAW
  需要运行服务器，`OleCreate` 失败。

Wine（`results/olecreateprobe.wine-hklm.txt`，注册在 HKLM）与 Windows 逐行相同。注册在 HKCU 时 Wine 原先全部失败：
Wine 的 HKEY_CLASSES_ROOT 不合并 `HKCU\Software\Classes`（Windows 会）。altars-up 的 kernelbase 合并视图与
combase/ole32 先查用户的类（`7d0dd7e27c7`、`691f1d81c14`）之后，注册在 HKCU 的结果（`results/olecreateprobe.wine.txt`）
与 Windows 逐行相同。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror olecreateprobe.c -o olecreateprobe.exe -lole32
-loleaut32 -luuid -ladvapi32`。
