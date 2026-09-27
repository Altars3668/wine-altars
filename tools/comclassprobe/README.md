# COM 服务器异常退出后的类注册

`tools/comclassprobe/comclass.c` 创建自己的无注册表 COM 类。父进程启动子进程，子进程以 `REGCLS_MULTIPLEUSE` 注册 `IClassFactory`；父进程先成功跨进程取得类对象，然后直接结束子进程（故意不调用 `CoRevokeClassObject`），最后再次请求同一 CLSID。只使用临时进程、进程内对象和本机命名事件，不读写 Office 文件或真实账户。

构建与运行：

```sh
scripts/build-probe.sh tools/comclassprobe/comclass.c tools/comclassprobe/comclass.exe ole32
WINEPREFIX=<独立测试前缀> wine-src/build-wow64/wine tools/comclassprobe/comclass.exe
```

旧版 `/opt/wine-altars` 的 `rpcss.exe` 实测：`registered 0`、`before exit 0`、`after exit 0x800706ba`，退出码 6；已退出进程的类对象仍被 RPCSS 解组。修改后的 build-tree RPCSS 在**重启独立测试前缀的 wineserver 后**实测两次：`registered 0`、`before exit 0`、`after exit 0x80040154`，退出码 0；服务失效注册被清理，调用方得到未注册错误而非旧服务器 RPC 失败。此项只断言退出后应失败且不再返回 `RPC_S_SERVER_UNAVAILABLE`，原生 Windows 精确错误码仍需在 winref 可连通时使用同一 PE 测量。不要通过仅重启 RPCSS 后的一次成功激活来判定修复：重启本身会清空失效注册，必须在同一服务生存期先注册、让服务器异常退出、再查询。

本轮真实 Office 触发：PowerPoint 第一次 COM 创建/保存 PPTX 后，其 `closed` 标记未落盘；下次 `CreateObject("PowerPoint.Application")` 返回 VB 429。`+ole,+seh` 定位到 `rpc_get_local_class_object` 从 RPCSS 读到旧的多次使用类对象，`CoUnmarshalInterface` 返回 `0x800706ba`。修补位置是 Wine 提交 `044f721` 的 `programs/rpcss/rpcss_main.c`：注册时取得本机 RPC 调用者 PID 并打开可等待的进程句柄；取类对象时非阻塞检查服务进程，若已退出则删除旧项并查找其它注册。无法取得句柄时保留既有行为并记录警告，不假报类对象有效。替换 `/opt/wine-altars` 的 64 位 `rpcss.exe`、重启 Office 测试前缀后，探针连续两次通过；PowerPoint 在**同一 RPCSS 生存期**连续两次 COM 激活、创建并保存本地 PPTX，两个 ZIP/OOXML slide1 文本都验证通过。两次 PowerPoint 进度都只到 `saved`，未见 `closed` 标记；重复激活通过不等于退出回调已实现，也不能从此推断 Outlook 或全部 Office 生命周期恢复。
