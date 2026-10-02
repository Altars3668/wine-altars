# startupgapsprobe：Word 启动时打出的那一串 fixme，逐个在 Windows 上量

Word（Microsoft 365，16.0.20326）在 Wine 里启动一次，会打出几十条 `fixme`：桩函数、半实现、不认识的标志。它们不一定
让 Word 出错，但每一条都是 Wine 与 Windows 行为不同的地方。这里的探针把 Word 问的那些调用原样问一遍，再问一些周边，
在 Windows 11 build 29671（winref 笔记本）上量出规则，然后 Wine 按结果实现，同一个探针在 Wine 下的输出与 Windows 逐行对比。

    x86_64-w64-mingw32-gcc -O2 -Wall -o etw.exe etw.c -ladvapi32        # 其余探针链接的库见各自的 #include
    WIN_HOST=... WIN_USER=... scripts/winbatch.sh etw.exe -- 'etw.exe'    # 在 Windows 上跑，SSH 会话里
    scripts/winrun.sh --desktop pointer.exe                               # 要登录用户桌面的（pointer）

`results/*.win.txt` 是 Windows 上的输出。探针只打印结果，不打印个人数据：用户名、计算机名在输出前替换或只给长度，
网络名只给长度，SID 打印成 `<machine>-RID`，产品策略（含部分产品密钥）不保存；写日志文件的探针把文件放在临时目录、
用完删除、不打印路径。没有一个探针写系统事件日志（不调用 `ReportEvent`）。

## 各探针

| 探针 | 量什么 | 结果 |
| --- | --- | --- |
| `reginfo` | `RegQueryInfoKey` 给的安全描述符大小 | `reginfo.win.txt` |
| `nlm`、`netnotify`、`netchange`、`qi` | 网络列表管理器、IP Helper 通知、Office 问的接口 | `nlm.win.txt`、`qi-nlm.win.txt` |
| `arr`、`arr2` | 应用重启与恢复、WER 注册、Restart Manager | `arr.win.txt` |
| `d3dopts`、`textparams` | D3D11 特性、GDI 兼容纹理的 `GetDC`、D2D 与 DirectWrite 渲染参数 | `d3dopts.win.txt`、`textparams*.win.txt` |
| `pointer` | 指针设备函数（要用户桌面） | `pointer.win.txt` |
| `ws`、`ws2`、`ws3`、`ws4` | Web Services 的错误串、读取器故障、SSL 绑定 | `ws*.win.txt` |
| `etw`…`etw5`、`etlwrite`、`etlread` | 事件跟踪会话、提供程序、日志文件、消费者、事件源 | `etw*.win.txt`、`etlread-tracerpt.win.txt` |
| `misc`、`fileusers`、`unsigned` | 其余小函数（第 8 项） | `misc.win.txt` |

## 事件跟踪（ETW）

Word 启动时为自己开一个私有的进程内会话：`StartTraceW("v2_WINWORD:…")`，模式 `0x80020802`
（`ADDTO_TRIAGE_DUMP | PRIVATE_IN_PROC | PRIVATE_LOGGER | CIRCULAR`），循环日志文件
`%TEMP%\Outlook Logging\WINWORD_<版本>-<时间>-32.etl`，最大 10 MB，系统时钟；然后对九个提供程序 `EnableTraceEx2`。
Wine 原来的 `StartTrace` 什么也不做、返回成功，`EnableTraceEx2` 也一样，提供程序永远不被启用。现在会话由 ntdll
（`dlls/ntdll/etw.c`）在进程内维护，sechost 的 `StartTrace`/`ControlTrace`/`EnableTraceEx2`/`OpenTrace`/`ProcessTrace`/
`CloseTrace`/`QueryAllTraces` 调它；Word 的日志文件真的被写出来，Windows 的 `tracerpt` 能读 Wine 写的日志
（`etlread-tracerpt.win.txt`）。量到的规则：

- **参数顺序**：`BufferSize` 小于结构 → 24；名字放不下 → 24；空句柄/空属性、偏移落在结构里 → 87；`SEQUENTIAL|CIRCULAR` →
  87；内核跟踪的 GUID → 87；私有又实时、私有又有 `FlushTimer` → 87；既非实时也非内存缓冲又没有日志文件 → 161；循环而
  `MaximumFileSize` 为 0 → 87；非私有会话要管理员或 Performance Log Users（受限令牌 → 5）；同名 → 183（先于打开文件）。
  私有但不在进程内的旧式记录器：Windows 等提供程序的进程来启动它，一分钟后返回 1460 且名字一直被占（Wine 不等，直接返回）。
- **返回的属性**：`ADDTO_TRIAGE_DUMP` 被清掉；私有会话缓冲 4 KiB（可改）、最少 16（不可改）、最多 38（有文件）或 16；实时会话
  64 KiB、16、38，并加上 `PERSIST_ON_HYBRID_SHUTDOWN`（0x800000）；时钟 0 变 1。私有会话句柄是 `0x1000000 | 记录器号`。
- **告诉提供程序**：私有会话在调用线程上当场回调，按注册从新到旧；其他会话在另一个线程上、调用返回之后，`EnableTraceEx2` 给了
  超时就等回调做完。回调拿到的是所有启用它的会话合起来的值：级别取最大（0 当 255），任意关键字取并（0 当全 1），全部关键字取交；
  一个会话停止或禁用时，还有别的会话就回调一次“启用”给剩下的合计，没有了才回调“禁用”。`CAPTURE_STATE` 不论是否启用都回调
  （码 2），不改状态；禁用没启用的提供程序不回调。之后注册的提供程序在 `EventRegister` 里当场被告知已启用的。
  经典提供程序（`RegisterTraceGuids`）收到 `WMI_ENABLE_EVENTS`，`WNODE_HEADER` 48 字节，`HistoricalContext` 是
  `TRACE_ENABLE_CONTEXT`：记录器号、级别、私有标志 1、`EnableFlags`（任意关键字的低 32 位）；禁用时是会话句柄，停止时是 0。
- **哪些事件被收**：事件级别不高于启用级别（级别 0 都收），关键字 0 都收，否则须含任意掩码之一且含全部掩码。
  `EventProviderEnabled`/`EventEnabled` 按同样的规则看任一会话。
- **记录格式**：缓冲区头 0x48 字节（大小、已用、文件内绝对结束位置、时间、序号、处理器、状态 3、标志 0x21），记录 8 字节对齐，
  其余填 0xff；文件第一个缓冲区是头：系统头 + `TRACE_LOGFILE_HEADER`（64 位布局，0x118 字节）+ 两个名字，另一条 0x50 字节的
  系统头收尾。事件是 `EVENT_HEADER`（类型 0xc013，标志存原始值），私有会话的处理器时间是 CPU 周期计数，其他会话是线程的
  内核/用户时钟滴答；扩展项是“大小、类型、是否还有、数据大小”再跟数据：相关活动 ID（1）、提供程序特征（12）、TraceLogging
  模式（11）。`EventWriteString` 标志 4。经典 `TraceEvent` 是 `EVENT_TRACE_HEADER`（类型 0xc014）加数据或 MOF 字段收集的数据；
  `TraceMessage` 是消息头（大小、0x90、编号、选项，`POINTER64` 被加上，私有会话不留 `SEQUENCE`）再跟 GUID、时间、线程与进程、参数。
  放不下一个缓冲区的事件丢掉，返回 234 并计入 `EventsLost`。
- **查询**：名字没有偏移就放在结构后，日志文件名跟在名字后；两者重叠或放不下 → 234，什么也不写。`Wnode.HistoricalContext`
  是句柄。私有会话不按名字找（只在属性里同时带着它的 GUID 和句柄时才找得到）；没有名字 → 87。内存缓冲的会话 flush → 2。
  私有会话没有记录器线程（`LoggerThreadId` 为 0），有文件的有。`QueryAllTraces` 的计数是全部会话数，放不下时 234。
- **消费者**：`OpenTrace` 成功把最后错误清零，`NULL` → 87；实时会话打开时不检查是否存在，`ProcessTrace` 才返回 4201；读日志
  文件时用文件里会话的模式覆盖 `LogFileMode`，先交出两条 `EventTraceGuid` 的头事件，再按写入顺序交出事件，每个缓冲区一次
  `BufferCallback`，`BuffersRead` 不变；私有会话的事件带 `PRIVATE_SESSION`，消息带 `TRACE_MESSAGE` 且 `EventProperty` 是选项。
  `EVENT_TRACE` 方式下，从文件读的事件 `HeaderType` 是 1，实时的是 0；`MofData` 是扩展项加负载。实时事件按缓冲区成批交付。
- **活动 ID**：在 TEB 里。`CREATE` 造的不是任何版本的 GUID：前 6 字节是开机时的时间、接着 2 字节是处理器号、后 8 字节是每次加一
  的计数；`CREATE_SET` 返回旧的、设新的；码 0、6 或空指针 → 87。

## 事件源

`RegisterEventSource`、`OpenEventLog`、`OpenBackupEventLog` 给的是同一种句柄，各函数通用，关闭后失效（再关 →
`ERROR_INVALID_HANDLE`）。本机的名字（计算机名、`\\` 加计算机名、DNS 名、`localhost`）当本机，其他服务器 → 1722；
没有或空的源名 → 87；`Security` → 5；`DeregisterEventSource(NULL)` 返回 FALSE 且不动最后错误。`ReadEventLog` 先查缓冲
与两个大小指针（87），再查句柄（6），再查读法与方向各有且只有一个（87）。
