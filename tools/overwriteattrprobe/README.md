# 覆盖已存在的隐藏、系统、只读文件时属性怎么变：`overwriteattrprobe.exe`

`CreateFile` 的文档说：`CREATE_ALWAYS` 覆盖一个已有的隐藏或系统文件时，若给的属性里不含同样的隐藏/系统位，就以
`ERROR_ACCESS_DENIED` 失败。[MS-FSA] 2.1.5.1.2（打开已有文件）写的是同一条：覆盖或替换时文件存的属性有隐藏（或系统）而请求
的没有，`STATUS_ACCESS_DENIED`；新属性加上 ARCHIVE、去掉 NORMAL 与 NOT_CONTENT_INDEXED。Wine 的测试只覆盖了只读
（ntdll `test_NtCreateFile` 的表，altars-up `170955e4ab0` 已照它实现：覆盖与替换时文件取得新属性）。

探针对六种已有文件（普通、隐藏、系统、只读、隐藏+系统、隐藏+只读）和八组请求的属性，分别用 `CreateFile` 的
`CREATE_ALWAYS`、`TRUNCATE_EXISTING` 以及 `NtCreateFile` 的 `FILE_OVERWRITE`、`FILE_OVERWRITE_IF`、`FILE_SUPERSEDE` 覆盖，打印
结果（`NtCreateFile` 附 `io.Information`）、之后的属性和大小。另问替调用方覆盖文件的几个函数遇到隐藏、隐藏+系统、只读的
目标时怎么做：`WritePrivateProfileString`（desktop.ini 就是隐藏+系统）、`CopyFile`、`MoveFileEx(MOVEFILE_REPLACE_EXISTING)`。
文件放在 `%TEMP%` 下的新目录里，结束时删掉。

已按 [MS-FSA] 实现（altars-up）：

- `97d0f78ea33`：覆盖、替换存为隐藏或系统的文件，请求的属性里没有同样的位就拒绝，文件内容不动。只看文件存的属性
  （`user.DOSATTRIB`），点文件在 Wine 里只是显示为隐藏，不算。
- `e40b7d311c0`：Wine 的 profile 写入原先用 `CREATE_ALWAYS` + `FILE_ATTRIBUTE_NORMAL` 覆盖整个文件——在 `170955e4ab0` 之后
  这会把 desktop.ini 的隐藏、系统属性冲掉，按上一条又会被拒；改为打开已有文件、写完截断。

`results/overwriteattrprobe.wine.txt`：Wine（上述提交之后）。Windows 的待测（winref），重点看 profile 写入隐藏+系统文件的
结果与写后的属性（Wine 现在保留属性）、`CopyFile` 与替换式移动在隐藏目标上的结果。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror overwriteattrprobe.c -o overwriteattrprobe.exe -lntdll`。
