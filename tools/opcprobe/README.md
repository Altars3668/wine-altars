# Windows OPC API 写什么、读什么：`opcprobe.exe`

用系统的 `IOpcFactory`（opcservices.dll）建一个包：各种压缩选项的部件、内部目标、外部绝对目标、外部相对目标
（`TargetMode="External"`）的关系，自动生成的关系 ID；写出的字节以 base64 打印（`zip32`、`zipdefault` 两段，
`unzip-output.py <输出> zip32` 解开看 ZIP 条目与 XML），`GetRelationshipsContentStream` 给出的关系部件原文，读回后
的列表；再把手工做的 ZIP（无 `[Content_Types].xml`、未声明的扩展名、绝对目标缺 TargetMode、TargetMode 的几种写法、
缺 Id、重复 Id……）交给 `ReadPackageFromStream`，打印三种读取标志下的错误码；以及 `DeletePart`、`DeleteRelationship`、
`GetEnumeratorForType`、`ComparePartUri`、`GetRelativeUri` 和部件内容流、`CreateStreamOnFile` 流的各 IStream 方法。

起因：Word 打开 PDF 时 PDFREFLOW.EXE 用这套 API 生成 docx，Wine 丢了外部关系的 TargetMode（altars-up `83989262b3a`）。
`results/opcprobe.wine.txt` 是修 TargetMode 之后、补其余桩之前的 Wine 输出；Windows 的结果待测（winref）。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror opcprobe.c -o opcprobe.exe -lole32 -luuid -loleaut32 -lurlmon`。
