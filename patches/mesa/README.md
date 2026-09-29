# Mesa：共享上下文链表里的半成品上下文

`0001-mesa-list-a-context-among-the-shared-ones-only-once.patch` 针对 Mesa `main`
（2026-09 取的 `src/mesa/main/context.c` 与 `src/mesa/state_tracker/st_context.c`），
同样的代码也在本机装的 26.0.3 和 Ubuntu 更新源里的 26.0.8 里。**尚未提交上游。**

## 现象

上游 Wine master 下 Word 启动后偶发整体卡死（前缀报 Windows 11 时 7 次启动卡 5 次）：

```
0024:err:sync:RtlpWaitForCriticalSection section ... "?" wait timed out in thread 0024, blocked by 0210
```

主线程在 `d2d_command_list_Close → d2d_device_context_unbind` 等 D2D 工厂锁；持锁的是
dcomp 合成线程，它在 `render_default_device → D3D11CreateDevice → wined3d 探测 GL 能力`，
卡在 `wglCreateContextAttribsARB`（或 `glGenBuffers`）里；Office 自己那个 D3D 设备的
`wined3d_cs` 线程卡在 `glDeleteBuffers` 里。两条 GL 线程等的是同一个 Mesa
`gl_shared_state::Mutex`，而没有任何线程停在持锁区里。

## 根因

1. `_mesa_initialize_context()` 末尾就把新上下文挂进 `ctx->Shared->Contexts`，
   `ctx->st`、`st->pipe` 要等之后的 `st_create_context_priv()` 才赋值。
2. 另一线程在 threaded context 下释放缓冲区，`_mesa_release_pending_resource()` 持
   `Shared->Mutex` 遍历该链表，`_mesa_check_shared_resource_usage()` 读
   `shared_ctx->st->pipe`：对还在创建中的上下文就是 NULL 解引用（实测故障地址 0x10，
   IP 落在 `shared.c:477`，`mov 0xce010(%rbx),%rax` 取出 `st` 为 0）。
3. 故障发生在 `__wine_unix_call` 里，ntdll 的 `handle_syscall_fault()` 只让这次 unix
   调用返回 `STATUS_ACCESS_VIOLATION`（thunk 打一行
   `warn:opengl:glCheckFramebufferStatus ... returned 0xc0000005`），**锁就此永远锁着**，
   之后进程里任何上下文创建或缓冲区释放都会挂住。
4. 上游 Wine 自 `e0a0ab55202`（2026-04，"Share all unix-side GL contexts with a global
   context"）起，进程内所有 GL 上下文都与一个根上下文共享，所以任意线程建设备都会和
   其他设备的绘制撞上这个竞态。

## 补丁

`_mesa_initialize_context()` 里只 `list_inithead(&ctx->SharedLink)`；
`st_create_context()` 在 `st_create_context_priv()` 成功之后才持锁把上下文加入链表
（失败则直接返回 NULL，`_mesa_free_context_data()` 对自环节点 `list_del` 无害）。

## 验证

- 关掉 threaded context（`GALLIUM_THREAD=0`，出问题的遍历只在 TC 下执行）：同样的
  启动脚本 5/5 正常，日志里没有 `0xc0000005`；开着时 7 次卡 5 次。
- 本机缺 Mesa 的构建依赖（llvm-config、xcb-dri3/xshmfence 头文件、mako），打了补丁
  的 libgallium 还没有实际跑过。

## 规避

在 Mesa 修复进发行版之前，Office 进程带 `GALLIUM_THREAD=0` 启动即可避开；代价是
GL 驱动少一个工作线程，对 Office 的绘制量影响不大。

## 怎么查出来的

`tools/winegdb/README.md`：Yama 下给 Wine 进程挂 gdb 的预加载库、按 maps 手工载入符号、
从 ddebs 取 Mesa 调试信息、用 TEB 认出线程。
