# Office 前缀更新行为探针

这里只测量 Windows API，不修改 Wine。`results/*.wine.txt` 是测试前缀中的 Wine 输出，**不是 Windows 11 的行为规格**；本轮未在真实 Windows 上运行。

## 各探针量什么

- `leapsecond.exe`：`NtQuerySystemInformation(206)` 的短、正好和较大缓冲区，返回长度及全部缓冲区字节；若导出存在，另测 `GetSystemLeapSecondInformation`。
- `ras.exe`：先只读查询 RasMan，只有确认运行才枚举连接并测通知的 event、flags 和长度语义；只打印连接个数，不打印连接名。
- `userprofile.exe`：`GetProfileType`、当前令牌和已挂载配置文件上的 `LoadUserProfileW` 参数校验，真实加载后的键形状及卸载；恢复本进程原有特权。
- `cosecurity.exe`：各自在子进程测 Office 参数、重复调用、COM 初始化前、封送后、非法参数及显式默认值；通过 STA→MTA 的真实 IStream 代理查 blanket。
- `rpcsd.exe`：同进程 ncalrpc 回声，比较默认 SD、Everyone、空 DACL、SYSTEM 和不同访问位；分别用未设置认证和显式 WINNT 认证的 binding，打印服务器实际认证信息。
- `storageprop.exe`：访问权为 0 打开 C: 和 PhysicalDrive0，测设备描述、存在查询、seek penalty 和错误尺寸/枚举值；序列号只打印长度。
- `msxmlparser.exe`：DOMDocument60 的 NewParser=false/true，分别测内部实体、DTD 校验开关、异步本地 load、畸形 XML 数值错误和本地外部实体；每组再测 XMLSchemaCache60、schemas、XSD 合格/不合格实例与 validate()。

## 安全与结果判读

- 每个 `main` 首先设置禁止错误窗口的 `SetErrorMode`，27 秒看门狗只结束本探针；正常末行是 `done`。超时退出码 124 **不算通过**，即使末行也是 `done`。
- 不直接写注册表，不创建、删除或请求启动服务，不拨号，不访问网络。RPC 仅 ncalrpc；XML 的外部实体只指向本探针在当前目录新建的本地文件。XSD 中的 `http://www.w3.org/2001/XMLSchema` 只是命名空间，不是加载地址。
- 不故意传入 NULL 或野指针做参数破坏测试；文档允许的可选参数、任务明确要求的默认 NULL SD 和 INVALID_HANDLE_VALUE 仍按 API 约定使用。
- `userprofile` 仅在当前用户的 HKU 键已存在时加载，且每次成功后立即请求卸载。若卸载失败或成功却没有句柄，停止后续加载；避免叠加配置文件引用。特权只在本进程令牌中调整，结束前恢复。
- `cosecurity` 只影响自己的进程、子进程和线程。默认无参数运行全部场景；每个子进程最多等待 3.2 秒，超时仅结束该子进程，整组仍受看门狗约束。
- `rpcsd` 的端点带随机数，不注册端点映射器；异常捕获器先用 RPC_S_ACCESS_DENIED 自检，然后才调用客户端。每个场景停止监听、等待结束并注销接口。
- `msxmlparser` 使用随机文件名与 CREATE_NEW，绝不覆盖现有文件；异步等待最多 1.5 秒，正常返回前释放 DOM 并删除三个输入文件。目录不可写时记录失败，不改用其它目录；硬超时或外部强杀仍可能留下输入文件。
- 不打印用户名、机器名、SID、卷 GUID、序列号内容或个人路径；键只打印 `\REGISTRY\USER\<sid>` 形状，XML 不打印 parseError.reason。
- `gle=3735928559` 表示调用保留了预置哨兵 `0xdeadbeef`，不是探针确认的系统错误。每次需要的 GetLastError 都先取出，再输出。
- 本地异步文件可能在 load 返回前完成；`immediate_ready=4` 不能单独证明系统没有异步能力。设置读回相同也不证明两种解析器执行语义相同。
- GCC 不理解 `__try1` 内联汇编的 SEH 范围，可能把不返回的分支移出保护区。RPC 的分支放在非内联辅助函数里，保护区只保留必经调用；不要去掉异常自检或通过忽略崩溃“修复”它。

## 编译（Linux，仓库根目录）

需要 MinGW x64 编译器及已有 Wine 构建中的 widl。生成物全部留在本目录；无需编译任何 Wine 模块。

```bash
set -e
D=tools/prefixupdateprobe
WIDL=wine-src-up/build-wow64/tools/widl/widl
GCC=x86_64-w64-mingw32-gcc

"$WIDL" -m64 -Oif --prefix-client=c_ --prefix-server=s_ -h -H "$D/rpcsd.h" "$D/rpcsd.idl"
"$WIDL" -m64 -Oif --prefix-client=c_ -c -o "$D/rpcsd_c.c" "$D/rpcsd.idl"
"$WIDL" -m64 -Oif --prefix-server=s_ -s -o "$D/rpcsd_s.c" "$D/rpcsd.idl"

"$GCC" -O1 -Wall -Werror -o "$D/leapsecond.exe" "$D/leapsecond.c" -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/ras.exe" "$D/ras.c" -lrasapi32 -ladvapi32 -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/userprofile.exe" "$D/userprofile.c" -luserenv -ladvapi32 -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/cosecurity.exe" "$D/cosecurity.c" -lole32 -luuid -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/rpcsd.exe" "$D/rpcsd.c" "$D/rpcsd_c.c" "$D/rpcsd_s.c" -lrpcrt4 -ladvapi32 -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/storageprop.exe" "$D/storageprop.c" -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/msxmlparser.exe" "$D/msxmlparser.c" -lole32 -loleaut32 -luuid -lbcrypt
```

`-Werror` 将警告提升为编译失败；不能通过关掉警告来通过验收。`rpcsd.h`、`rpcsd_c.c`、`rpcsd_s.c` 由上述 IDL 命令生成，不手改。

## Wine 下运行（仓库根目录）

必须串行运行：指定脚本会重启测试前缀的 wineserver，并复用 C:\\probe.exe。不要换成用户日常前缀，也不要并行运行。

```bash
RUN=/tmp/claude-1000/-home-user-Sources-GitSources-wine-altars/d9224694-7a07-4bd3-b214-9ff93aa4e719/scratchpad/runprobe.sh
D=tools/prefixupdateprobe
mkdir -p "$D/results"
for X in leapsecond ras userprofile cosecurity rpcsd storageprop msxmlparser; do
    "$RUN" "$D/$X.exe" > "$D/results/$X.wine.txt"
done
```

脚本从测试前缀 C:\\ 运行，使用 Xvfb :77。脚本末尾的清理会掩盖前面的 PE 退出码，不能仅根据脚本返回 0 判定通过；应检查日志的 `done`、timeout/子进程失败标记，并独立观测实际进程退出码（本轮用 strace）。

当前 Wine 中 RasMan 不存在，因此 RAS 调用按规则跳过。LoadUserProfileW 对错误 dwSize 也返回成功、给出预定义 HKCU，随后 UnloadUserProfile 失败，因此停止后续加载；这是已记录的 Wine 桩行为，不是已完成的 Windows 参数测量。CoQueryProxyBlanket 和 schema cache 的 validate 也返回 E_NOTIMPL，均保留原值。

## Windows 11 上运行

将七个 exe 放在同一个**本地可写、仅用于探针**的当前目录，在命令提示符逐个执行下面命令。无需安装 DLL、注册 COM 或调整系统配置；不应为了 RAS 探针启动服务。若普通当前令牌没有 SeBackup/SeRestore，真实加载可能被拒绝，保留输出即可，不自动提权。

```cmd
leapsecond.exe
ras.exe
userprofile.exe
cosecurity.exe
rpcsd.exe
storageprop.exe
msxmlparser.exe
```

`cosecurity.exe` 已自动重启自己跑全部场景。单场景复查可用：

```cmd
cosecurity.exe --case office
cosecurity.exe --case second
cosecurity.exe --case before
cosecurity.exe --case marshal_first
cosecurity.exe --case bad_authn
cosecurity.exe --case bad_capabilities
cosecurity.exe --case explicit
```
