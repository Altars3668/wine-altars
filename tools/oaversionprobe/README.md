# `OaBuildVersion()`：`oaversionprobe.exe`

Office 每次启动都调 `OaBuildVersion()`。Wine 的表只到 Windows 8；对声明支持 Windows 10 的程序（`GetVersion()` 报 10.0），
它记一条 `Version value not known yet` 的 FIXME 并返回 `MAKELONG(0xffff, 50)`。探针带同样的兼容性清单，打印
`GetVersion()`、`OaBuildVersion()` 与 oleaut32.dll 的文件版本。

`results/oaversionprobe.wine.txt`：Wine；Windows 的待测（winref）。
