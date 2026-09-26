# VisualInteractionSource 生命周期对照

`lifetime.c` 在真实 Windows 的桌面会话里测同一个 `Visual`：第一次 `Create` 成功、未移除前第二次 `Create` 返回 `E_INVALIDARG`；将旧交互源加入 Tracker 的 `InteractionSources` 后调用 `RemoveAll`，即使调用方仍持有旧源，重新 `Create` 也成功。若对第二个源只调用单项 `Remove`，再次 `Create` 仍返回 `E_INVALIDARG`。`lifetime.win.txt` 是 winref Windows build 29671 的原生输出；同一 PE 在修复后的 Wine 上逐行一致。构建可用 `scripts/build-probe.sh tools/interactionsourceprobe/lifetime.c tools/interactionsourceprobe/lifetime.exe combase user32`，原生要用 `scripts/winrun.sh --desktop`。

这组区别解释 Excel 白屏：`Mso40UIwin32client.dll` 创建工作簿时先对 SpriteVisual 创建源、加进集合、`RemoveAll`，再对**同一个** Visual 创建新源。旧 Wine 只在旧源 COM 引用归零后清理 `visual->interaction_source`；Excel 还持有一个引用，于是第二次 Create 给 `E_INVALIDARG`，Office 的 ShipAssert `0x1e440099` 故意向零写入，异常处理器挂起 Excel 的其他线程。`dlls/dcomp/interaction.c` 仅在 `RemoveAll` 清理本实现的旧源预留，保留单项 `Remove` 的原生行为。新旧 Wine 的探针差异、dcomp 全套测试及真实 Excel 的 XLSX 保存/GUI 网格都已验证；不把它外推为 Office 全功能通过。
