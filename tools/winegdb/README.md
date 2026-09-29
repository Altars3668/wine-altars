# winegdb：给 Wine 进程挂 gdb 看 unix 一侧

winedbg 的栈停在 PE 与 unix 的边界（例如 `glGenBuffers+0x4a` 这个 thunk），驱动里在等什么
只有原生调试器看得到。在这台机器上挂 gdb 有三道坎，这里各有一个对策。

## 1. Yama 不让附加：`ptracer.c`

`kernel.yama.ptrace_scope = 1` 时只有祖先进程能 ptrace。Wine 进程由启动器派生后
改挂到 `systemd --user` 下，而且 ntdll 连上 wineserver 后会调用
`prctl(PR_SET_PTRACER, server_pid)`（`dlls/ntdll/unix/server.c`），把之前设的例外覆盖掉。
所以包装脚本里先 `prctl(PR_SET_PTRACER_ANY)` 没有用。

这个预加载库拦截 `prctl()`，把 `PR_SET_PTRACER` 一律改成 `PR_SET_PTRACER_ANY`（其中
仍包括 wineserver）：

```sh
gcc -shared -fPIC -O2 -o ptracer.so tools/winegdb/ptracer.c
LD_PRELOAD=$PWD/ptracer.so wine ...      # 环境变量会传给 Wine 派生的每个进程
```

`gdb -p` 附加失败时报的是 “Inappropriate ioctl for device”，实为 `EPERM`。

## 2. gdb 不认识任何共享库：`loadsyms.py`

预加载器绕过了 ld.so 的链接表，所有帧都是 `??`。脚本按 `/proc/<pid>/maps` 把每个 ELF
按加载基址 `add-symbol-file`：

```sh
gdb -q -p <pid> -batch -ex 'set debuginfod enabled off' \
    -ex 'set debug-file-directory <ddeb 解出的 usr/lib/debug>:/usr/lib/debug' \
    -ex 'source tools/winegdb/loadsyms.py' -ex 'source tools/winegdb/wintid.py' \
    -ex 'thread apply all bt 40'
```

Ubuntu 的 Mesa 去掉了内部符号，debuginfod 上也可能没有这个版本：从
`http://ddebs.ubuntu.com/pool/main/m/mesa/` 取对应版本的 `mesa-libgallium-dbgsym_*.ddeb`，
`dpkg-deb -x` 解开即可。单个地址也可以直接 `addr2line -f -i -e <build-id>.debug <偏移>`，
偏移 = 地址 − 该库在 maps 里文件偏移为 0 的那段的起址。

## 3. 哪条 Linux 线程是哪个 Windows 线程：`wintid.py`

Wine 线程的 `%gs` 指向 TEB，`TEB->ClientId.UniqueThread` 在 0x48。Mesa 自己起的
`gl0`/`gdrv0` 等工作线程继承创建者的 `%gs`，所以也能看出它们属于哪条 Windows 线程。

## 另两个配合的开关

- `WINEDEBUG=warn+opengl`：unix 调用里的段错误会被 ntdll 的 `handle_syscall_fault()`
  变成返回值，只在这里留下 `glXxx returned 0xc0000005`。
- `WINEDEBUG=+seh`：`handle_syscall_fault` 打出故障的 `ip`、`addr` 和寄存器。

实际用例见 `patches/mesa/README.md`。
