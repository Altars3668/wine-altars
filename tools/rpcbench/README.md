# ncalrpc 往返：`rpcbench.exe`

Click-to-Run 的 App-V 注册表钩子每查一个键都要经 ncalrpc 问一次服务（`IsDuplicatedKey`），每次 Office 启动约一万次。
Windows 走 ALPC；Wine 的 ncalrpc 是命名管道，每次调用要经 wineserver 往返约十次。

`rpcbench server` 在端点 `rpcbench` 上监听；`rpcbench client [次数] [字节]` 连续调用（默认一万次、每次 48 字节，
与 App-V 的请求一样大），打印每秒调用次数与每次微秒数，然后让服务端退出；`rpcbench both [次数] [字节] [impersonate]`
自己起服务端子进程，带 `impersonate` 时服务端每次调用都模拟客户端再恢复（App-V 的服务端这样做）。

`results/rpcbench.wine.txt`（altars-up，i7 笔记本）：开销全在往返，不在数据量。原先收一个包要读三次管道（先读 0 字节
等数据，再读公共头，再读其余），每次调用 22 个 wineserver 请求、约 280 µs；wine 8caf6adef4e 起 rpcrt4 把整条消息
一次读进缓冲区，降到 14 个请求、约 170 µs（同一棵树、同一前缀新旧交替测）。Windows 上的数值（winref）作对照。

构建（stub 由 Wine 树里的 widl 生成，已一起提交）：
`widl -m64 --prefix-server=s_ -h -H rpcbench.h -c -C rpcbench_c.c -s -S rpcbench_s.c rpcbench.idl`，
`x86_64-w64-mingw32-gcc -O2 -Wall -o rpcbench.exe rpcbench.c rpcbench_c.c rpcbench_s.c -lrpcrt4`。
