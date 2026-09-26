# InteractionTracker 逐轴绑定原生探针

`binding.c` 需要线程上的 `DispatcherQueue`；用 `scripts/build-probe.sh tools/trackerbindingprobe/binding.c tools/trackerbindingprobe/binding.exe combase user32` 编译，同一 PE 用 `scripts/winrun.sh --desktop` 在 Windows 上测量。原生输出 `binding.win.txt` 来自 winref 的 Windows build 29671；不包含个人数据或包装器打印的退出码。

已实测的同步规则：`SetBindingMode` 对同一对 Tracker 取代旧模式；`GetBindingMode` 两个方向一致；对不同伙伴可把 X、Y、Scale 轴分别绑定，解绑一对不会清掉另一个轴；未定义的 bit 8 也返回成功且能读回；跨不同 Compositor 返回 `E_ACCESSDENIED`，`Get` 把输出模式写成 None。Wine 用按位的伙伴图保存这些关系，不再以 `S_OK` 伪称绑定后让所有 `Get` 返回 None。

Windows 参考桌面处于锁屏/断开状态，`TryUpdatePosition` 虽然返回成功，立即和泵消息后读到的位置仍为零。**不能把这组位置输出与 Wine 的即时更新逐字节比较，也不能由此证明原生帧动画语义。** Wine 上还验证了两个伙伴分别跟随 X/Y、同一对伙伴跟随缩放，以及解绑后不再传播。对应 `dlls/dcomp/tests/composition.c` 在没有原生帧的会话跳过动态断言，只验证可观测的同步契约。

测试脚本 `scripts/winrun.sh --desktop` 等待独立的完成文件并传回 PE 退出码；旧版只看输出文件与进程名，长测试仍在运行时就误报完成。2026-09-26 本地 composition 测试及 winref 原生 composition 测试均无失败，原生帧断言因锁屏跳过；这**不是**整个 Windows.UI.Composition 子系统已完整实现的证明。
