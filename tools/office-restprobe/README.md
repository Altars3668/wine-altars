# Office 剩余诊断原生探针

2026-10-04 第一批源码、可执行文件及前后对照。原生参考为 Windows 11 build 29671；实现与覆盖边界见 [修复记录](../../docs/office-rest-fixes-20261004.md)。

## 探针与安全

- `smallapi`：本进程模拟令牌级别、只读 FIPS 策略、受控 localhost/numeric 名称解析。只输出 canonical 形状和 flags，不打印机器名/SID。
- `certlinks`：公开测试 DER、纯内存 store 的七个 disposition 与 source/link 属性共享。绝不打开个人或系统证书存储。
- `wsabort`：HTTP 配置型 proxy/channel 的 created/open/closed、重复 abort、close/reset；不发送消息。
- `wscancel`：自有临时 loopback 服务器确认收到请求后阻塞，测 WsAbortServiceProxy 是否在放行前完成及真实调用错误码。
- `httpcancel`：同样的自有 loopback 场景，测 WinHttpCloseHandle 对同步 ReceiveResponse 的取消。
- `dnsbaseline`：只读解析测试中固定的公开 DNS 名字，用旧/新运行时区分网络波动；不把 DNS 恢复算作代码修复。

所有入口禁止错误窗口，共用 27 秒自杀看门狗；超时 124 不算通过。预期取消返回错误是契约，不是探针失败。退出 0 只表示测量器完成，必须核对 `server_reached=1`、`completed_before_server_release=1` 和错误码。

原生不运行 crypt32 store 或 webservices channel 全套测试，它们会改变系统证书或防火墙；本轮只跑 `store links`、`proxy abort`、`channel abort`、`protocol fqdn` 的隔离子模式。FIPS 只读，不改机器策略。

## 编译

Linux 仓库根目录，MinGW x64；缺少 WsAbortChannel 声明时探针通过 GetProcAddress 查询真实导出，不降低编译告警。

```bash
D=tools/office-restprobe
GCC=x86_64-w64-mingw32-gcc
"$GCC" -O1 -Wall -Werror -o "$D/smallapi.exe" "$D/smallapi.c" -lws2_32 -ladvapi32 -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/certlinks.exe" "$D/certlinks.c" -lcrypt32 -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/wsabort.exe" "$D/wsabort.c" -lwebservices -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/wscancel.exe" "$D/wscancel.c" -lwebservices -lws2_32 -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/httpcancel.exe" "$D/httpcancel.c" -lwinhttp -lws2_32 -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/dnsbaseline.exe" "$D/dnsbaseline.c" -lws2_32 -lbcrypt
```

必须逐条检查编译退出码，任何失败不得上传候选文件。Wine 使用 `tools/prefixupdateprobe/run-wine.sh` 的专用私有目录串行运行；测试显示只用 :77/:2，禁止 Wayland 回退与日常前缀。

## 证据

- `*.wine-before.txt`：实际修前或初始实现的输出，保留不覆盖。
- `*.wine-after.txt`：最终测量，实际 PE/runner 退出码保留。
- `*.win.txt`：第 154–157 批原生受控测量。
- `native-regressions.win.txt`：原生隔离断言与真实 WINRUN-EXIT。
- `tests-cancel-summary.txt`：第一版关闭时错误中断空闲/升级连接的失败；对应 `tests-http-active-summary.txt` 是修后完整回归。
- `tests-fqdn-full-summary.txt` 保留临时 DNS 失败，`tests-fqdn-repeat-summary.txt` 是网络恢复后的重跑。
- `office-save-result.txt`：真实 Word/Excel/PowerPoint 的退出、步骤和 OOXML ZIP/XML 验证。

时间、默认主机名、地址数量以及成功时 GetLastError 副作用随环境不同，不宣称原始输出逐字相同。只修到本批已测契约，不将完整 COM/RAS/profile/TSF/MSXML 待办写成已完成。
