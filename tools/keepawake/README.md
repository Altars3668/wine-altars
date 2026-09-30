# 批次期间不让 Windows 睡眠：`keepawake.exe`

参考笔记本（winref）没人用时会进入睡眠（现代待机）：网卡与 TCP 栈仍会应答，但 sshd 不发 banner；而且 ssh 会话本身不能阻止它
入睡，2026-09-30 一批测试跑到一半时它就睡了，连接因服务器无响应断开。

`keepawake.exe [秒数]`（默认 3600）在这段时间里持有“系统需要”“显示器需要”“执行需要”三种电源请求，并设
`ES_CONTINUOUS|ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED`，时间到或进程被结束（会话结束时一起结束）就释放；不改任何电源设置。
`scripts/winbatch.sh` 会自动上传它，在批处理开头后台启动、结尾结束。

构建：`x86_64-w64-mingw32-gcc -O2 -Wall -o tools/keepawake/keepawake.exe tools/keepawake/keepawake.c && x86_64-w64-mingw32-strip tools/keepawake/keepawake.exe`。
