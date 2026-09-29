# 系统注册了哪些 WIC 像素格式：`wicformatprobe.exe`

枚举 `WICPixelFormat` 组件，每个格式一行（按 GUID 排序）：位深、通道数、数值表示、是否支持透明、每个通道的掩码，
以及厂商、作者、名称、版本。Wine 只注册了 Windows 列表的一部分（`dlls/windowscodecs/regsvr.c`），两边输出一比就是缺的
格式和注册它们要的数据。缺 `8bpp Alpha` 曾让 Word 的 PDF 导入失败（altars-up `6fbec143386`）。

`results/wicformatprobe.wine.txt`：Wine 的 31 个格式；Windows 的待测（winref）。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror wicformatprobe.c -o wicformatprobe.exe -lole32 -luuid -lwindowscodecs`。
