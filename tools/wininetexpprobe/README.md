# WinINet 缓存怎样对待不是日期的 `Expires`：`wininetexpprobe.exe`

服务器常发 `Expires: -1` 表示“已经过期”（ASP.NET 默认如此），RFC 7234 §5.3 也要求缓存把任何无效日期当作过去的时间。
Wine 的 wininet 只把 `0` 这样处理；别的解析不了的值（记一条 ERR）会让条目落到默认的十分钟有效期上。

探针在本进程里起一个 127.0.0.1 上的小 HTTP 服务器，每个路径回一种头：`Expires` 为 `-1`、`0`、乱写、空、过去、
未来，没有头，`Cache-Control: max-age=0`（单独、再加未来的 `Expires`），以及 `max-age=3600` 加 `Expires: -1`。
对每种都按默认缓存方式经 `InternetOpenUrl` 取两次，打印缓存条目的过期时间（相对现在的秒数）、
`IsUrlCacheEntryExpired`，以及第二次请求是否到了服务器；之后删掉条目。

`results/wininetexpprobe.wine.txt` 里 Wine 的另外两处也看得出来：`max-age=3600` 只得到约 360 秒（源码按
`age * 1000000` 换算，而 FILETIME 以 100 纳秒为单位，差十倍）；第二次请求一律又去了服务器，连 2038 年才过期的条目
也是（Wine 只写缓存、从不读）。Windows 的待测（winref），据此再改。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror wininetexpprobe.c -o wininetexpprobe.exe -lwininet -lws2_32`。
