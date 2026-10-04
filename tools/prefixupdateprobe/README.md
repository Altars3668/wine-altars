# Office 前缀更新行为探针

测量 Windows API，不通过伪造返回值消除 Wine 的诊断。原生参考是 Windows 11 build 29671、zh-CN；2026-10-03 已在真实 Windows 上完成原始探针及后续权限、缓冲区、解析器对照。

## 各探针量什么

- `leapsecond.exe`：`NtQuerySystemInformation(206)` 的短、正好和较大缓冲区，返回长度及全部缓冲区字节；若导出存在，另测 `GetSystemLeapSecondInformation`。
- `ras.exe`：先只读查询 RasMan，只有确认运行才枚举连接并测通知的 event、flags 和长度语义；只打印连接个数，不打印连接名。
- `userprofile.exe`：`GetProfileType`、当前令牌和已挂载配置文件上的 `LoadUserProfileW` 参数校验、键形状及卸载；恢复本进程原有特权。
- `cosecurity.exe`：用独立子进程测 Office 参数、重复调用、COM 初始化前、封送后、非法参数及显式默认值；通过 STA→MTA 的真实 IStream 代理查 blanket。
- `rpcsd.exe`：先量注册 flags/callback 矩阵，再用独立服务器/客户端进程测 18 组 ncalrpc 接口 SD。默认客户端禁用自己的 Administrators 组；打印真实服务器执行次数，分别测未设置认证和 WINNT binding。异常捕获器先自检。
- `accessowner.exe`：直接测 `AccessCheck` 的 owner 隐含权限、MAXIMUM_ALLOWED、OWNER RIGHTS ACE、默认 owner/DACL 和禁用 Administrators 后的 owner。SID 只输出类别判断。
- `storageprop.exe`：访问权为 0 打开 C: 和 PhysicalDrive0，测设备描述、存在查询、seek penalty 和错误尺寸/枚举值；序列号只打印长度。
- `storagebuffers.exe`：C: 的输入大小 4–13 字节、设备描述符的零/短输出，以及 seek/trim 输出 0–13 字节边界。
- `msxmlparser.exe`：DOMDocument60 的 NewParser=false/true，分别测内部实体、DTD 校验、异步本地 load、畸形 XML 数值错误和本地外部实体；另测 schema cache、合格/不合格 XSD 实例及 validate()。
- `xmlreason.exe`：受控内存 XML 的旧/新 DOM 解析器、DTD 拒载、标签错误、错误码和行列 getter HRESULT；复杂 DTD 包含 notation、未解析实体和参数实体声明，不加载外部文件。
- `schemamode.exe`：XMLSchemaCache60 的 validateOnLoad=false/true，分别 add 合格/无效类型的 XSD，再真实 validate 缓存。

## 安全与结果判读

- 所有入口首先设置禁止错误窗口的 `SetErrorMode`。共用 `probe.h` 的七项原始探针有 27 秒看门狗；RPC 另有逐阶段时限。四个补充探针必须从外层设运行时限。超时退出码 124 **不算通过**，即使输出了 `done`。
- 不直接写注册表、不创建或启动服务、不拨号、不请求打印。RPC 仅 ncalrpc，不注册端点映射器。XML 外部实体只指向本探针在当前目录新建的本地文件；XSD URL 只是命名空间。
- 不故意传 NULL 或野指针做参数破坏测试；API 允许的可选参数、默认 NULL SD 和 INVALID_HANDLE_VALUE 按约定使用。
- `userprofile` 仅在当前用户 HKU 键已存在时加载，每次成功后立即卸载；若卸载失败或成功却没有句柄，停止后续加载。特权只在自己的令牌中调整并恢复。
- `cosecurity`、`rpcsd` 只管理自己创建的子进程。RPC 默认每个 SD 场景使用新进程，避免接口重注册与静态认证身份缓存污染；可用 `--case N`、`--case-callback N`、`--case-normal N` 单测同一索引。
- `RPC_IF_SEC_NO_CACHE` 加 NULL callback 原生返回 87。旧版 `rpcsd.win.txt` 中注册失败后的“无调用”**不是 ACL 结论**；最终探针确认合法注册、监听和服务器执行后才判读。
- 管理员令牌的默认 owner 可以是 Administrators。空 DACL 下 owner 的 READ_CONTROL/WRITE_DAC 会影响 RPC；不能由管理员测到的“允许”推断 SD 被忽略。最终矩阵同时测受限身份、显式 user owner、SYSTEM owner。
- `msxmlparser` 使用随机文件名与 CREATE_NEW，不覆盖现有文件；异步等待有界，正常返回前删除三个输入文件。硬超时或强杀仍可能留文件。
- 不打印用户名、机器名、SID、卷 GUID、序列号内容或个人路径。`xmlreason` 只打印固定内存样本的 reason；`msxmlparser` 不打印可能包含文件路径的 reason。
- `gle=3735928559` 是保留的哨兵 `0xdeadbeef`，不是探针确认的系统错误。成功返回时 GetLastError 可能未改变，不应把它当失败。
- 设置读回相同不证明两种解析器执行语义相同；必须看实际加载、拒载、错误位置及缓存验证。
- GCC 不理解内联汇编 SEH 范围，可能把不返回分支挪到保护区外。RPC 将必经调用放在非内联包装函数里，以真实 x64 SEH 自检；filter 仅捕获低位 RPC 状态，实际访问违例继续搜索。Wine 的异常框架在 Wine 下有效，不等于原生 Windows 下有效。
- 这些是行为探针，不是全功能认证。RPC 预期拒绝会以已捕获的 `call_exception=0x5` 输出；进程退出 0 只证明探针运行完整，权限结论仍须核对返回值和服务器执行计数。

## 编译（Linux，仓库根目录）

需要 MinGW x64 编译器及已有 Wine 构建中的 widl。

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
"$GCC" -O1 -Wall -Werror -municode -o "$D/rpcsd.exe" "$D/rpcsd.c" "$D/rpcsd_c.c" "$D/rpcsd_s.c" -lrpcrt4 -ladvapi32 -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/storageprop.exe" "$D/storageprop.c" -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/msxmlparser.exe" "$D/msxmlparser.c" -lole32 -loleaut32 -luuid -lbcrypt
"$GCC" -O1 -Wall -Werror -o "$D/accessowner.exe" "$D/accessowner.c" -ladvapi32
"$GCC" -O1 -Wall -Werror -o "$D/storagebuffers.exe" "$D/storagebuffers.c"
"$GCC" -O1 -Wall -Werror -o "$D/xmlreason.exe" "$D/xmlreason.c" -lole32 -loleaut32 -luuid
"$GCC" -O1 -Wall -Werror -o "$D/schemamode.exe" "$D/schemamode.c" -lole32 -loleaut32 -luuid
```

`-Werror` 不能删掉来掩盖警告；IDL 生成物不手改。交付前先在 Wine 自检，再在原生 Windows 自检；不把崩溃的候选程序重复发送到参考机。

## Wine 下运行（仓库根目录）

使用已构建的 `wine-src-up/build-wow64`，不要默认为 `/opt` 里的版本。`run-wine.sh` 默认只接受 :77 或 :2，禁止 Wayland 回退，隔离 HOME、临时目录、XDG 和 WINEPREFIX；每次停止和清理的都是自己的前缀。:2 的 XAUTHORITY 必须由该 Xwayland 的 `-auth` 参数取得。没有可连接的测试显示就停止，不落到 :0。

```bash
set -e
D=tools/prefixupdateprobe
WORK=$(mktemp -d /tmp/wine-prefixupdate.XXXXXX)
for X in leapsecond ras userprofile cosecurity rpcsd storageprop msxmlparser accessowner storagebuffers xmlreason schemamode; do
    PROBE_WORKDIR="$WORK" "$D/run-wine.sh" "$D/$X.exe" > "$WORK/$X.wine.txt"
done
```

必须串行运行。脚本用锁拒绝同目录并发，保存 wineboot/cleanup 日志，输出 `PE-EXIT`、`CLEANUP-EXIT`、`RUNNER-EXIT`；清理不能把 PE 失败或超时改成成功。保留 WORK 供检查或下一轮复用，确认自己的测试会话已停止后再清理。

## Windows 11 上运行

将 exe 放在同一个**本地可写、仅用于探针**的目录，逐个执行。RPC、原始七项内置看门狗；补充探针外层需有时限。不运行完整 advapi32 security、rpcrt4 server 或 kernel32 volume 套件：它们有与本次只读测量无关的副作用。

```cmd
rpcsd.exe
rpcsd.exe --case-callback 11
rpcsd.exe --case-normal 17
accessowner.exe
storagebuffers.exe
xmlreason.exe
schemamode.exe
```

原始七项也可逐个直接运行。不为了 RAS 探针启动 RasMan；缺特权导致 profile 加载拒绝就保留结果，不自动提权。`cosecurity.exe --case office|second|before|marshal_first|bad_authn|bad_capabilities|explicit` 可单场景复查。

## 结果与修复范围

- 原始 `results/*.wine.txt` 保留修前 baseline，不覆盖。`rpcsd.win.txt` 也保留最初的无效注册记录，以免丢掉仪器失败证据。
- `*.wine-after.txt` 是已修复运行时的输出；对应 Wine `altars-up` 到 `b160c3b0494`。`rpcsd.final.win.txt` 与四个补充探针 `*.win.txt` 来自第 152 批，直接使用当前源码重新编出的交付文件。
- `native-regressions.win.txt` 保存第 146/151 批隔离的 owner、RPC SD、storage、新旧解析器和延迟 schema 编译断言，均有真实 `WINRUN-EXIT`。回归通过不能代替未覆盖的 API 场景。
- 硬件型号、描述符全长、序列号长度、默认 owner/DACL 与环境相关。存储查询比较输入/输出契约，不把参考机的 NVMe 身份照抄到 Wine。
- 已实测修复闰秒查询、空 RAS 枚举/通知参数、已挂载 profile、默认 COM security/本地 blanket、RPC 接口 SD、owner 令牌权限、存储属性及 MSXML NewParser/缓存/解析时验证。
- 尚未覆盖完整 RAS 拨号、未挂载/roaming profile、复杂非 NULL COM security、RPC AppContainer 默认 SD、MSXML UseInlineSchema、自定义 SAX entity resolver、完整外部参数实体/外部 DTD。不能据本组通过宣称 Office 的所有功能或最新 Windows 的所有特性已实现。
