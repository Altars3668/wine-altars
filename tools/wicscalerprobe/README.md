# WIC 缩放器的各种插值模式：`wicscalerprobe.exe`

Wine 的 `IWICBitmapScaler` 只会最近邻，线性、三次、Fant、高质量三次都记一条 `unsupported mode` 的 FIXME 后退成最近邻；
Excel 每次启动都用三次插值缩放几次。探针对几张 7×5 的小图（非预乘与预乘 BGRA、BGR、8 位灰度、8 位调色板图）
按缩小、原样、放大、放大一倍四种尺寸（灰度、BGR、调色板图只用缩小和放大）在五种模式下缩放，打印 `Initialize` 的
结果、缩放器给出的像素格式和全部输出像素（十六进制），并拿内部一行的单独拷贝对照整幅拷贝。核函数、像素中心、
舍入和透明度的处理都能从中读出。

`results/wicscalerprobe.wine.txt`：Wine（五种模式输出相同）；Windows 的待测（winref），据此实现。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror wicscalerprobe.c -o wicscalerprobe.exe -lole32 -lwindowscodecs -luuid`。
