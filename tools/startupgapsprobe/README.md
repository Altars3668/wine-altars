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
| `misc`、`fileusers`、`unsigned` | 其余小函数（第 8、9 项） | `misc.win.txt` |
| `misc2` | 窗口的输入范围、要求签名目标的加载 | `misc2.win.txt` |
| `misc3` | 私有对象安全的设置与转换、空令牌、性能信息的长度、其余缓解选项与子进程策略、XmlWriter 属性 | `misc3.win.txt` |
| `misc4` | `SetFileShortName` 与短名（自己在当前目录建文件、用完删除） | `misc4.win.txt` |
| `comrel` | `CoDisconnectObject` 之后的 `CoReleaseMarshalData`、引用计数，ROT、拖放、GIT 的撤销与跨 apartment 取接口（只用自己的对象，ROT 登记用自己的项目名并撤销） | `comrel.win.txt` |
| `misc5` | 堆扩展信息的各级别（Word 问 0x80000000）、Cookie 的 Secure/HttpOnly/SameSite（只用 `*.invalid` 域的会话 Cookie，结束时过期） | `misc5.win.txt` |
| `evtread` | 经典事件日志：记录的布局、顺序读与定位读怎么移动、错误码，wevtapi 给经典事件的 XML 形状（字符串只给长度） | `evtread.win.txt` |
| `evtapi` | wevtapi：查询的错误码、XPath 子集、结构化查询与 Suppress、渲染、格式化、日志信息、通道与通道配置 | `evtapi.win.txt` |
| `evtquery` | 事件日志 XPath 的位置、位置上的 `!=` `<` `>`、数字与字符串的比较、存在测试（给各查询选中的条数相对总数） | `evtquery.win.txt` |
| `evtlimits` | 查询能嵌套多深、能连多少项（小步逼近上限；5000 项的 `or` 会让 Windows 事件日志服务崩溃，不要加大） | `evtlimits.win.txt` |
| `evtsub` | 订阅：已有的事件与信号、`EvtNext` 等不等、说完没有之后再问、信号谁来复位、回调在哪个线程 | `evtsub.win.txt` |
| `evtrender` | `EvtRender` 把值放在缓冲区哪里（系统、用户、路径上下文，经典事件的字符串数组，二进制，写到多远），哪些路径组合建得成上下文 | `evtrender.win.txt` |
| `evtmsg` | `EvtFormatMessage` 的消息、级别、任务、关键字（用户语言与英文、结尾 NUL）、按编号取消息、XML 的呈现部分；netevent.dll 里事件日志服务各事件的中英文消息 | `evtmsg.win.txt` |

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

## 小函数（第 8、9 项）

`misc`…`misc5` 量到、Wine 照做的规则，细节见各结果文件：

- **缓解策略**：创建选项每两位一组，1 开、2 关、3 是第三种（动态代码“线程可退出”=3、非微软二进制“允许商店”=6、
  字体“审计”=2、强制重定位“要求重定位”加 `DisallowStrippedImages`）；关掉自底向上 ASLR 连高熵一起关；影子栈开时
  上下文 IP 校验一并打开（0x105）；限制核心共享=0x10、FSCTL=4（系统调用策略）；其余选项（加载器完整性、模块篡改、XFG、
  指针认证、第 60 位）问不出变化。子进程策略只收 4 字节：1 限制（再建进程得 367）、5 限制但允许安全进程，2、4、8 无效果。
  SEHOP 默认为 1，指针认证在 x64 上是 `ERROR_NOT_SUPPORTED`。
- **私有对象安全**：所有者/组取创建者的、否则令牌的；即使创建者全给了也要令牌（1008），只有所有者没有组时是 1308。
  无创建者 DACL 时取父对象传下来的、再没有取令牌默认 DACL（通用权限映射）；自动继承把父对象的 ACE 加 `ID` 放在自己的后面。
  设置时：DACL 不给就是 NULL DACL，自动继承保留旧的继承 ACE（受保护的除外），修改里带 `ID` 的 ACE 在自动继承时丢掉、
  否则照留；所有者要令牌（没有令牌 1307）但不查是谁；SACL 与标签不要特权；未知标志忽略；成功时描述符换新指针。
- **短名**：`SetFileShortNameW` 自己取得还原特权（持有即可，不必事先启用）；句柄要有 DELETE（否则 5）；名字须合法 8.3
  （九个字符、空格、加号、两个点都 87），转大写；与目录里别的文件的长名或短名相同 183，与自己的长名相同可以；空串去掉短名，
  之后按旧短名打开 2、`FileAlternateNameInformation` 为 `STATUS_OBJECT_NAME_NOT_FOUND`。8.3 长名的文件被另给短名后，
  `FindFirstFile` 报那个短名，`GetShortPathName` 仍给长名。
- **堆扩展信息**：请求头 40 字节，缓冲不超过它是 `STATUS_INVALID_PARAMETER`；级别 0–2 给 `HEAP_INFORMATION`（共 88 字节），
  不指定堆时给全部堆的保留、提交、个数（72）；0x80000000 每个堆 160 字节（地址、保留、提交、段数、在用、空闲、空闲块数、
  大块），不指定堆时进程堆在先、其余按创建先后；放不下时 `STATUS_BUFFER_TOO_SMALL` 并给所需大小。级别 3 起与 0x80000001
  给区域与块，未做。
- **断开之后的 COM**：`CoReleaseMarshalData` 对已断开、已释放过、apartment 已结束的标准 marshal 数据一律 S_OK；marshal 一个对象的
  IUnknown 只加一个引用（另一个接口加三个：stub manager、接口、stub 缓冲）。ROT 把断开对象的登记当作不存在：`IsRunning` S_FALSE，
  `GetObject`、`GetTimeOfLastChange` 为 `MK_E_UNAVAILABLE`，`EnumRunning` 不列，`Revoke` 仍 S_OK。GIT 登记只加一个引用、不建 stub；
  本 apartment 取回的就是对象本身，别的 apartment 第一次来取时才在登记的 apartment 里 marshal（再加三个，留到断开）；断开后本
  apartment 照样取得到，别的 apartment 得 `CO_E_OBJNOTREG`，撤销 S_OK。
- **Cookie**：Secure 的只给 https（在 http 上设的也一样）；SameSite 任何值都收、不影响保存；HttpOnly 的要
  `INTERNET_COOKIE_HTTPONLY` 才设得进、取得到。
- **性能信息**：小于 312 字节 → `STATUS_INFO_LENGTH_MISMATCH`、长度 376；312 到 376 之间按给的长度填、返回该长度；更大给 376。
- **XmlWriter**：`CompactEmptyElement` 非零写 `<b/>`；一致性级别只收 0–2（否则 `E_INVALIDARG`）；布尔属性存为 0/1。
- **输入范围**：只能给本线程的窗口设；`IS_DEFAULT` 去掉；`SetInputScopes` 给了短语、正则、SRGS 就追加 -1/-2/-3；
  `SetInputScopeXML` 存为 [-4]；`TF_GetInputScope` 对无范围的窗口 `S_FALSE`，无效/已销毁窗口 `E_INVALIDARG`。
- **签名目标**：自身无签名的进程用 `LOAD_LIBRARY_REQUIRE_SIGNED_TARGET` 装任何还没装过的库都是 577（即使是微软签名的），
  已装过的直接成功；相对名加 `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` 是 87（KnownDLLs 例外，仍是 577）。

## 事件日志

`evtread`…`evtmsg` 量到、Wine 照做的规则（都只读；事件日志探针一律不调用 `ReportEvent`，查询保持在上限附近的小值）：

- **经典日志**：记录里用户 SID 在计算机名之后的下一个 8 字节边界，字符串与数据紧随其后，整条补齐到 8 字节、末尾是长度；
  顺序读从上次给出的那条接着读（两个方向都是），定位读从给的记录号开始，越界 87，读到头 38，放不下一条 122 并给所需大小；
  清空后编号接着往上走。事件日志服务启动时写 6009（版本）与 6005，停止时写 6006。
- **查询**：没有该通道 15007、没有该文件 2、两个路径标志都给 87、XPath 不给路径 15000，坏 XPath、不认识的函数、`not()` 都是
  15001；支持 `band()`、`timediff()`、`!=`、`position()`，`band()` 的参数只能是路径或数字。位置比较时是那个位置上的元素，作存在
  测试时只看第一个元素（`EventData[Data[1]]` 成立、`EventData[Data[2]]` 永不成立）；位置配 `!=` `<` `>` 的结果在 Windows 上
  自相矛盾（同一事件 `Data[3]>29670` 假、`Data[3]>29671` 真），不照做。和数字比较时，整个是数字的字符串按数值比，其余按字符串
  比（`"10.00."` 不等于 10）。括号最多 24 层、谓词最多 21 层、`or` 或 `and` 连起来的最多 23 项，超过即 15001。
- **订阅**：订阅前已有的事件立刻就能用 `EvtNext` 取到，但不置信号；`EvtNext` 从不等待（timeout 给 `INFINITE` 也一样）；说完
  `ERROR_NO_MORE_ITEMS` 之后、新事件来之前再问是 `ERROR_INVALID_OPERATION`；Windows 不复位信号；回调在订阅自己的线程上调用；
  对回调订阅调 `EvtNext` 是 6，信号与回调同时给是 87。
- **渲染**：每个值的数据从变体数组之后的下一个 8 字节边界放，总大小也补到 8 的倍数；系统上下文的字符串 Count 是字符数；
  路径 `Event/EventData/Data` 是字符串数组（先指针表、字符串紧接其后），用户上下文把这个数组拆成单个值、仍留着指针表的位置、
  Count 含结尾 NUL；二进制照给，非叶子元素是 Null；整个缓冲区都被清零（XML 只写用到的部分）。上下文的路径构成一棵树：后加的
  元素路径若已在树里（重复，或是先前路径的祖先）建上下文 87，重复的属性路径第二个给 Null。
- **格式化**：消息去掉末尾的 CRLF 后跟两个 NUL，级别、任务也是两个，关键字三个，按编号取的消息不填插入、两个 NUL；
  XML 的 `RenderingInfo` 只有 Message、Level、Task、Keywords，`Culture` 随语言；没有发布者的消息 15027，Opcode、Channel、
  Provider 15028。locale 0 用用户界面语言（winref 上是简体中文：信息、无、经典、错误、警告），0x409 是英文。
