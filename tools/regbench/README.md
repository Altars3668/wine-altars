# 经 HKEY_CLASSES_ROOT 的注册表操作耗时：`regbench.exe`

Windows 上 `HKEY_CLASSES_ROOT` 是用户 Classes 叠在机器 Classes 之上的合并视图，每次调用都重新判断用哪一侧。
Wine 的 kernelbase 实现了这层合并之后（altars-up 上 `kernelbase: Merge the user's classes into HKEY_CLASSES_ROOT`），
这里量它比直接走 `HKLM\Software\Classes` 多花多少：打开存在与不存在的类键、读默认值与不存在的值、在经 HKCR
打开的句柄下再开子键、`RegQueryInfoKey` 数子键、把 `CLSID` 与根整个枚举一遍，以及走 COM 自己查找路径的
`CLSIDFromProgID`。只打印次数与耗时，不打印键名。

用法：`regbench.exe [次数]`（默认 2000）。构建：`scratchpad/build-probe.sh tools/regbench/regbench.c
tools/regbench/regbench.exe advapi32 ole32`。

`results/` 里是在 Office 的前缀（`~/.wine-c2r-up`，`HKCU\Software\Classes` 下没有子键）上、同一台机器的两次结果：
`regbench.wine-unmerged.txt` 是合并之前的 kernelbase，`regbench.wine-merged.txt` 是现在的。这台机器上一次
wineserver 往返约 **15 µs**，所以结果基本就是“每个操作几次服务器调用”：

| 经 HKCR 的操作 | 合并前 | 合并的第一版 | 现在 |
|---|---|---|---|
| 打开并关闭一个类键 | 31 µs | 59 µs | 55 µs |
| 打开不存在的类键 | 15 µs | 44 µs | 35 µs |
| 读一个值 | 15 µs | 73 µs | 35 µs |
| `RegQueryInfoKey`（CLSID，1022 个子键） | 30 µs | 14.6 ms | 79 µs |
| 枚举 CLSID，每项 | 14.7 µs | 103 µs | 39 µs |

合并的第一版每次操作都用 `NtQueryKey` 取键名、再按路径把用户侧和机器侧各开一遍，`RegQueryInfoKey` 还要把机器侧
每个子键拿到用户侧去查一遍。现在每个句柄的路径和所在侧只查一次，机器侧直接用句柄本身，只有“用户侧有没有”
每次重查，多出一次服务器调用；计数只遍历两侧中较小的一侧。

这层开销在 Office 里主要落在 Click-to-Run 的 `integrator.exe` 上：它在每次启动时经 HKCR、带 `KEY_WOW64_32KEY`
打开约两万个键。64 位进程要 32 位视图时，kernelbase 逐级打开并检查每层的 `Wow6432Node`（服务器只替 WoW64 进程做
这种重定向），合并前它就要 51 万次 `NtOpenKeyEx`、约 18 秒；第一版在用户侧再完整走一遍，变成 101 万次、28 秒，
Excel 的自动化回归随之慢了 8～11 秒。现在用户侧先看路径的第一段在不在用户的 Classes（或其 `Wow6432Node`）里，
不在就不走，Office 三个应用的回归耗时与合并前一致（旧 75/74/69 秒，新 74/77/75 秒；单独冷启动 Word 旧
14.2/14.9/15.2 秒、新 13.8/14.9/14.8 秒）。
