# 双字节前导字节后跟一个不能作尾字节的字节：`dbcsprobe.exe`

按 ANSI 代码页读进来的文本常常并不是那个代码页的——中文系统上把 UTF-8 当 GBK 读是最常见的情形——前导字节后面于是跟着
引号、逗号、空格或换行。Wine 把这一对当成一个无效字符，那个 ASCII 字节就没了：`B4 22 78` 在 936 下成了 `? x`，引号不见；
kernel32 的测试只证明 Windows 会保留前导字节后的 NUL。把 UTF-8 的 CSV 或脚本按 ANSI 读时，这会让字段、字符串粘在一起
（Excel 普查脚本在 zh_CN 下报语法错误就是这样来的）。

探针对 936、932、949、950 各取几个前导字节，后面接各类字节（控制符、空格、引号、逗号、数字、`?`、`@`、反斜杠、0x7F、
0x80、0xA0、0xFF），以及末尾孤立的前导字节，分别不带和带 `MB_ERR_INVALID_CHARS` 转换，打印得到的字符。

`results/dbcsprobe.wine.txt`：Wine；Windows 的待测（winref），据此改 Wine 的转换。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror dbcsprobe.c -o dbcsprobe.exe`。
