# XSLT 的 AllowXsltScript 与 AllowDocumentFunction：`xsltsecprobe.exe`

对 DOMDocument30 与 DOMDocument60，用三种样式表变换一个小文档：msxsl:script 中的 JScript 函数（脚本为普通文本、
或在 CDATA 段里），以及 `count(document('')/*/*)`；样式表文档的设置分别取默认、显式 true、显式 false，打印
`transformNode` 的结果或错误。

`results/xsltsecprobe.win.txt`（Windows 11 build 29671）：

- MSXML 3 默认允许脚本与 `document()`，设为 false 后调用即令变换失败（E_FAIL）；MSXML 6 默认两者都禁止，设为 true
  才可用。
- 脚本是普通文本还是 CDATA 段都一样运行。

改动前的 Wine：两个设置都只保存不生效（脚本与 `document()` 总是可用），且只认 CDATA 形式的脚本。改动后结果与 Windows
一致，只差失败时 Windows 给出的 IErrorInfo。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror xsltsecprobe.c -o xsltsecprobe.exe -lole32
-loleaut32 -luuid`。
