# C++ 的时区数据库：`tzdbprobe.exe`

`std::chrono::get_tzdb()`、`current_zone()` 以及每次经 `time_zone` 的换算，最后都落到 `msvcp140_atomic_wait.dll`
的 `__std_tzdb_*` 导出；Windows 上它们的数据来自 `icu.dll`。Word 每次启动都要一次数据库和当前时区。
Wine 原来给的是 Windows 时区的显示名（没有一个是程序会按名字要的 IANA 名），`__std_tzdb_get_sys_info` 是桩，
第一次换算就让进程中止。

探针打印：数据库的版本、条数和一部分名字与链接；当前时区名字的形状（不打印名字本身）；几个固定时区、时刻的
`get_sys_info`（夏令时切换前后各一毫秒、1874 年、没有切换的时区、半小时夏令时、链接、大小写不对和不存在的名字）；
闰秒；以及 `icu.dll` 自己报的数据版本和默认时区是否可得。

- `results/tzdbprobe.wine.txt`：altars-up `6ef87791215` 的输出。它与在同一个 Wine 的 `icu.dll` 上改用 Office 自带的
  微软 `msvcp140_atomic_wait.dll` 时逐行相同（那份 DLL 属于 Office 安装，只在临时前缀里用过，不入库）。
- Windows 的待测（winref）。

注意 Wine 的 `icu.dll` 要用 C++17 的 PE 交叉编译器才会构建（configure 在没有时静默跳过它和 libc++、dmsynth 等）；
见 `docs/office365-under-wine.md` 的相应一节。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror tzdbprobe.c -o tzdbprobe.exe`。
