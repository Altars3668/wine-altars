# 脚本宿主不是服务提供者时的 CreateObject：`scriptsiteprobe.exe`

用 C 程序分别托管 vbscript 与 jscript：一次宿主站点不实现 `IServiceProvider`（Wine 的 cscript 就是这样），一次实现但
不提供任何服务；执行 `CreateObject("Msxml2.DOMDocument.6.0")`（对象有 `IObjectWithSite`）与
`CreateObject("Scripting.Dictionary")`（没有），打印结果类型与宿主被询问服务的次数。

`results/scriptsiteprobe.win.txt`（Windows 11 build 29671）：两种宿主下两个引擎都能建出两种对象。改动前 Wine 的
vbscript 在宿主不是服务提供者时对有 `IObjectWithSite` 的对象报 0x800a01ad（jscript 早已正确），于是 Wine 的 cscript
里连 MSXML 6 文档都建不出；vbscript 的修复之后与 Windows 一致。

仍不同的：`TypeName` 在 Windows 上是 `DOMDocument60`（MSXML 提供类信息），Wine 是 `IXMLDOMDocument3`；MSXML 通过站点
询问的服务 Windows 为 3 个、Wine 为 2 个。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Werror scriptsiteprobe.c
-o scriptsiteprobe.exe -lole32 -loleaut32 -luuid`。
