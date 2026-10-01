# Mesa：共享上下文链表里的半成品上下文

`0001-mesa-list-a-context-among-the-shared-ones-only-once.patch` 针对 Mesa `main`
（2026-09 取的 `src/mesa/main/context.c` 与 `src/mesa/state_tracker/st_context.c`），
同样的代码也在本机装的 26.0.3 和 Ubuntu 更新源里的 26.0.8 里。**尚未提交上游**，提交方式受
Mesa 的 AI 规定约束，见“提交上游”一节。

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
  启动脚本 5/5 正常，日志里没有 `0xc0000005`；开着时 7 次卡 5 次（2026-09-29）。
- **最小复现 `tools/mesasharerace`（2026-09-30）**：EGL surfaceless 上一个线程反复建、删
  共享上下文，另一个线程用不同尺寸反复 `glBufferData`。同一构建目录只差这个补丁：
  26.0.3 与 main（9ac80d5）未修版都是每次约 2.3 秒 SIGSEGV（各 3/3，系统 26.0.3 同样
  3/3），停在 `shared.c:477`、`mov 0x10(%rax),%rdi`；补丁版 26.0.3 5 次 × 30 秒、main
  3 次 × 30 秒无故障。依赖在 `~/.local/opt/mesa-deps`（xcb-dri3 等的 dev 包 `dpkg -x`、
  libdrm 2.4.134 源码编译），mako 在 `~/.local/opt/mesa-venv`，补丁版装在
  `~/.local/opt/mesa-test`（26.0.3）与 `~/.local/opt/mesa-main`，不动系统 Mesa。
- Office 场景今天复现不出来了：系统 Mesa、开着 TC，Word 在 :2（radeonsi）冷启动 6/6 正常，
  多半是 dcomp 改成先建默认设备再加锁（altars-up `7bd9beccfdd`）后，“一个线程建设备、另一个
  线程在画”的重叠少了。补丁版下 Word 同样正常启动（Word 映射的是补丁版 libgallium）。

## 提交上游（未提交）

Mesa 的 `docs/submittingpatches.rst`（2026-09 的 main）对 AI 参与有明确要求：

- 提交说明、代码注释和 GitLab 上的文字要是提交者自己的话，不能由 AI 生成；
- AI 参与写代码必须披露：几乎全部由 AI 生成的用 `Generated-by: 工具 (模型)`，参与决策
  或生成一部分的用 `Assisted-by:`；
- `Co-authored-by` 只给人类合著者，不能用来标 AI。

本目录补丁的提交说明、两处代码注释和 `Co-Authored-By: Claude` 行都不合这些要求，只能当研究
材料。提交时由提交者本人：用自己的话重写提交说明（两处注释删掉或自己重写），加
`Generated-by: Claude Code (Claude Opus 5.5)`，保留 `Cc: mesa-stable`；在自己的
gitlab.freedesktop.org 账号下 fork mesa、推分支、开 MR，描述同样自己写，可以附
`tools/mesasharerace` 作复现（它的注释同样出自 AI，附上时要说明）。本机没有 freedesktop 的
登录，这一步只能由账号持有人做。

## 规避

在 Mesa 修复进发行版之前，Office 进程带 `GALLIUM_THREAD=0` 启动即可避开；代价是
GL 驱动少一个工作线程，对 Office 的绘制量影响不大。

## 怎么查出来的

`tools/winegdb/README.md`：Yama 下给 Wine 进程挂 gdb 的预加载库、按 maps 手工载入符号、
从 ddebs 取 Mesa 调试信息、用 TEB 认出线程。
