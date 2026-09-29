# 窗口站与桌面的安全描述符

`desktopsdprobe.exe` 以 SDDL 打印进程窗口站与线程桌面的所有者、组与 DACL（按 `SE_WINDOW_OBJECT` 与 `SE_KERNEL_OBJECT` 各读一次），
再各新建一个不带安全属性的桌面与窗口站读它们的，最后是令牌的默认 DACL。本机域 SID 只打印为 `S-1-5-21-<machine>`，登录 SID 为
`S-1-5-5-<logon>`。

起因：PowerPoint 插入 3D 模型时由 mso.dll 里 Chromium 式的沙箱代理建受限令牌，读线程桌面的 DACL 以加入沙箱令牌的 SID，拿到 DACL
不查 NULL 就交给 `GetAclInformation`；Wine 的桌面没有 DACL，PowerPoint 在 `RtlQueryInformationAcl` 里空指针崩溃。

`results/`（Windows 11 build 29671）：

- `desktopsd.service.win.txt`：ssh 进来的服务会话。窗口站与 Default 桌面都有 DACL；新建桌面的 DACL 正是窗口站里带 OBJECT_INHERIT
  的 ACE（去掉继承标志、通用权限映射成桌面权限）；新建窗口站被拒（5）。
- `desktopsd.interactive.win.txt`：登录用户的交互桌面（`scripts/winrun.sh --desktop`）。WinSta0 给登录 SID、受限代码、SYSTEM 全部权限，
  给 Administrators 与两个应用包 SID 较少的权限，每项一条给窗口站本身、一条（OI CI IO）留给桌面继承，另有 DWM、UMFD 等系统账户；
  新建桌面同样继承窗口站的可继承 ACE；新建窗口站复制调用进程所在窗口站的 DACL，所有者与组是用户。

Wine 的实现见 wine-src `bfd4e06c`；`user32` 的 `test_default_security` 在 Windows 两种会话与 Wine 下都通过。

构建：`scripts/build-probe.sh tools/desktopsdprobe/desktopsdprobe.c tools/desktopsdprobe/desktopsdprobe.exe advapi32 user32`。
