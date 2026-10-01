# Mesa 共享上下文链表的竞态：`mesasharerace`

`patches/mesa` 修的竞态的最小复现，不需要 Wine、Office 或显示：在 EGL 的 surfaceless 平台上（直接走渲染节点，本机是
radeonsi），一个线程不停地创建、销毁与根上下文共享的上下文，另一个线程在共享组里的上下文中不停地用不同尺寸
`glBufferData`（尺寸与用法都不变时 Mesa 只丢弃内容，换尺寸才释放旧存储）。开着 gallium 的线程化上下文时，释放存储会持
`Shared->Mutex` 遍历 `Shared->Contexts`、读每个上下文的 `st->pipe`；未修的 Mesa 在 `st` 赋值之前就把新上下文挂上了链表。

    gcc -O2 -Wall -o mesasharerace mesasharerace.c -lEGL -lOpenGL -lpthread
    ./mesasharerace [秒数]        # 默认 30 秒；期间没出错则退出码 0

本机（RX 6900 XT，radeonsi，2026-09-30）：

| Mesa | 结果 |
|---|---|
| 系统 26.0.3-1ubuntu1 | 3 次全部 SIGSEGV（20 秒内） |
| 本地 26.0.3，同一构建目录去掉补丁 | 3 次全部 SIGSEGV，每次约 2.3 秒 |
| 本地 26.0.3 + 补丁 | 5 次 × 30 秒无故障（每次约 2700 个上下文、4300 万次重新指定缓冲区） |
| 本地 main（9ac80d5），同一构建目录去掉补丁 | 3 次全部 SIGSEGV，每次约 2.3 秒 |
| 本地 main（9ac80d5）+ 补丁 | 3 次 × 30 秒无故障（每次 4800–13000 个上下文、3100–3900 万次重新指定缓冲区） |

未修版在 gdb 里停在 `src/mesa/main/shared.c:477`，`_mesa_check_shared_resource_usage()` 的
`threaded_context_is_buffer_on_busy_list(shared_ctx->st->pipe, …)`，指令 `mov 0x10(%rax),%rdi`（`st` 为空，故障地址 0x10），
调用链 `glBufferData → _mesa_bufferobj_release_buffer → _mesa_release_pending_resource`；同一时刻另一线程正在创建上下文。
这与 Wine 下 Office 卡死时的故障点一致：在 Wine 里故障发生在 unix 调用中，被转成返回值，`Shared->Mutex` 就此锁死。

`GALLIUM_THREAD=0` 关掉线程化上下文，也就没有这次遍历。

运行补丁版而不动系统 Mesa：`LD_LIBRARY_PATH=<前缀>/lib/x86_64-linux-gnu
__EGL_VENDOR_LIBRARY_FILENAMES=<前缀>/share/glvnd/egl_vendor.d/50_mesa.json`，输出的版本串带 `git-<提交>`。
