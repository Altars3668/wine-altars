# 一次性断点读数

`bpread` 使用标准 Win32 调试 API，在指定模块的 RVA 设置一次性断点；命中后恢复
原字节和 RIP，不单步、不重装断点，不修改目标函数的返回值。

```text
bpread <module.dll> <rva_hex[,rva_hex...]> <hit_count> -- <cmd> [args...]
bpread <module.dll> <rvas> <hit_count> -- --attach <wine_pid>
```

`--attach` 使用 Wine/Win32 PID，不是 Linux `/proc` 中的 PID。模块名采用不区分大小写
的子串匹配；调用方应给出唯一模块名。目标退出但未命中时，旧工具也可能返回 0，
因此必须核对 `HIT` 数量，不能只看退出码。

## 敏感路径必须收窄输出

默认模式会打印寄存器和对象内存，**不能直接用于凭据或许可对象**。

- `BPREAD_FIELDS_ONLY=1`：禁止通用寄存器、RCX 对象和 RBP 内存输出。
- `BPREAD_R14_QWORDS=0x78,0xb8,...`：仅读取事先确认的数值/长度字段；偏移必须对齐，且有数量与范围上限。不得用来转储字符串或凭据。
- 新源码的 `BPREAD_CODE_TARGET=1`：在已由反汇编确认的 CFG 间接调用点读取 RAX。
  只有目标属于可执行 `MEM_IMAGE` 时才输出 image base 与 RVA；不读取目标字节。
  该模式强制启用 fields-only，并忽略 R14 字段列表，避免夹带对象读取。

**二进制版本注意：** 2026-09-07 更新 `tools/bpread/bpread.exe` 的操作未获执行，
该现成二进制仍不包含新的 code-target 模式。本轮实际测量使用任务临时目录中
从当前源码编译的 `bpread-code-test.exe`。不要把设置环境变量等同于二进制支持它；
使用时同时设置 `BPREAD_FIELDS_ONLY=1`，并要求输出出现预期的 `code-target` 行。

## 构建与受控测试

当前源码可用 `x86_64-w64-mingw32-gcc -O2 -Wall` 编译为单独的诊断 EXE。
`code-selftest.c` 的两个导出点用于验证代码目标模式：

1. 在 `code_marker` 入口设断点，正常运行时 RAX 指向 `code_target`，应输出后者的 RVA。
2. 给测试程序传入任意附加参数，RAX 改为数据指针；应输出 `unavailable`，不得打印该指针。
3. 两项均须命中一次，且无通用寄存器、RCX 对象或 R14 字段输出。

本轮两项已实际通过，目标程序均正常继续执行。`field-selftest.c` 保留原有 R14
受控数值测试。请勿把测试中的合成数据用于 Office 或任何真实认证请求。

## Office 调试边界

早期启动调试曾受 CONTEXT 初始化、初始 loader breakpoint 处理和单步行为影响。
当前测试已在真实 Word 启动中命中一次性断点并正常分离；这不等于所有时机都安全。

必须先确认旧诊断实例真正退出，避免新进程被旧实例接管。正常关闭后仍可能出现
第二个错误框；MSAA action 返回成功不保证进程已经退出。不要全局终止 Word，
不要因等待超时而直接杀掉仍持有未恢复断点的调试器。
