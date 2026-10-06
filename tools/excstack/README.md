# excstack：在第一次机会异常处打印调用栈，不改目标

```
excstack <process.exe> [exception-code-hex] [count]
```

用标准调试 API 附加到同一 Wine 会话里名为 `<process.exe>` 的第一个进程，等指定代码（默认 `c0000005`）的第一次机会
异常；命中时打印异常记录、寄存器和用 dbghelp `StackWalk64` 回溯的调用栈（`模块!符号+偏移`，或 `模块+RVA`），然后把
异常原样交还目标（`DBG_EXCEPTION_NOT_HANDLED`），命中 `count` 次（默认 1）后分离。不改目标内存，不设断点；附加时
加载器的初始断点照惯例 `DBG_CONTINUE`。

2026-10-06 用它抓 Word 打开“引用”选项卡时第一次机会访问冲突的调用栈，由此追到 msxml3 从流加载文档的复制循环（见
[docs/office-upgrade-references-corners-20261006.md](../../docs/office-upgrade-references-corners-20261006.md)）。

注意：

- 要的是 wineserver 的进程号（`tools/toplevels` 打印的那种），不是 Linux 的 pid。
- 必须在目标启动完成后再附加：启动早期附加调试器本身就可能让目标崩溃。
