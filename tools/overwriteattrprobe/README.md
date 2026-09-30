# 覆盖已存在的隐藏、系统、只读文件时属性怎么变：`overwriteattrprobe.exe`

`CreateFile` 的文档说：`CREATE_ALWAYS` 覆盖一个已有的隐藏或系统文件时，若给的属性里不含同样的隐藏/系统位，就以
`ERROR_ACCESS_DENIED` 失败。Wine 的测试只覆盖了只读（ntdll `test_NtCreateFile` 的表，altars-up `170955e4ab0` 已照它实现：
覆盖与替换时文件取得新属性）。

探针对六种已有文件（普通、隐藏、系统、只读、隐藏+系统、隐藏+只读）和八组请求的属性，分别用 `CreateFile` 的
`CREATE_ALWAYS`、`TRUNCATE_EXISTING` 以及 `NtCreateFile` 的 `FILE_OVERWRITE`、`FILE_OVERWRITE_IF`、`FILE_SUPERSEDE` 覆盖，打印
结果（`NtCreateFile` 附 `io.Information`）、之后的属性和大小。文件放在 `%TEMP%` 下的新目录里，结束时删掉。

`results/overwriteattrprobe.wine.txt`：Wine（`170955e4ab0` 之后：覆盖隐藏文件不管给什么属性都成功）；Windows 的待测（winref）。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror overwriteattrprobe.c -o overwriteattrprobe.exe -lntdll`。
