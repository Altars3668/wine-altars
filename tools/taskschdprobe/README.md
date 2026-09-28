# 计划任务对象模型与 XML 往返的原生对照

Office Click-to-Run 用 `ITaskFolder::RegisterTask(xml)` 注册十来个计划任务（自动更新、功能更新、性能监视……）。`taskdump.c` 不注册任何东西，只在内存里对 `tasks.h` 中的每份 XML 做 `ITaskService::NewTask` + `ITaskDefinition::put_XmlText`，然后用 getter 打印对象模型看到的一切——注册信息、主体、设置（含 IdleSettings、NetworkSettings、`ITaskSettings2`）、每个触发器（含 IRepetitionPattern 与按类型的属性）、每个动作——再打印 `get_XmlText` 写回的 XML；接着通过对象模型构造一个含 11 种触发器的任务；最后看 5 份非法 XML 的返回码。

`tasks.h` 是 winref（build 29671）上 `Export-ScheduledTask` 导出的 `\Microsoft\Office\` 下 10 个任务，只含众所周知的 SID（SYSTEM、Users、INTERACTIVE），由脚本生成（导出用的只读 PowerShell 见 git 历史里的说明）。

    scripts/build-probe.sh tools/taskschdprobe/taskdump.c tools/taskschdprobe/taskdump.exe ole32 oleaut32 taskschd uuid
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh tools/taskschdprobe/taskdump.exe > tools/taskschdprobe/taskdump.win.txt

## 测出的契约（节选，全部见 `taskdump.win.txt`）

- 主体的 getter 把 SID 解析成账户名：`UserId` S-1-5-18 → `SYSTEM`（LogonType 5），`GroupId` S-1-5-32-545 → `Users`、S-1-5-4 → `INTERACTIVE`（LogonType 4）；XML 里写回 SID。
- `CalendarTrigger` 的 `ScheduleByDay`/`ByWeek`/`ByMonth`/`ByMonthDayOfWeek` 在对象模型里分别是 Daily/Weekly/Monthly/MonthlyDOW 触发器；`IMonthlyTrigger::DaysOfMonth` 是 LONG（Wine 的 IDL 原来写成 short）。
- `get_XmlText` 首行是 `<?xml version="1.0" encoding="UTF-16"?>`，节的顺序是 RegistrationInfo、Triggers、Principals、Settings、Actions（与 `Export-ScheduledTask` 的顺序不同），空元素写作 `<Actions />`，设置里默认值也写出。
- 新建的任务：ExecutionTimeLimit `PT72H`，IdleSettings `PT10M`/`PT1H`/StopOnIdleEnd，DisallowStartIfOnBatteries 与 StopIfGoingOnBatteries 为真，Priority 7，Compatibility 2。
- `put_XmlText` 的错误：未知元素 `SCHED_E_UNEXPECTEDNODE`（0x80041316），XML 不完整 `SCHED_E_MALFORMEDXML`（0x8004131a），非法时间值 `SCHED_E_INVALIDVALUE`（0x80041318），缺 Actions `SCHED_E_MISSINGNODE`（0x80041319）。

Wine 原先：主体、触发器集合、动作集合、空闲/网络设置的 getter 全是 `E_NOTIMPL`，没有 `ITaskSettings2`，读 XML 时触发器与动作被跳过，存下的任务是 `<Triggers/>`、`<Exec/>`、空的 `<Principal>`。与 `taskdump.win.txt` 相差 1719 行。wine-src `56fadec` 之后逐行一致（其中“XML 不完整”一项还要 xmllite `8b58e3c`：它原先在未闭合的文档末尾反复返回最后一个节点）；Office 重新注册的任务存下来有全部触发器、Users 组主体、命令与参数。

## 已注册的任务：`regtasks.c`

`regtasks.c` 读 winref 上 `\Microsoft\Office` 里已注册的任务：路径、`IRegisteredTask::get_Xml`、定义里的 URI、`System32\Tasks` 下文件的头几个字节与大小。账户 SID、用户名与计算机名打印成占位符。

    scripts/build-probe.sh tools/taskschdprobe/regtasks.c tools/taskschdprobe/regtasks.exe ole32 oleaut32 advapi32 taskschd uuid

- `get_Xml` 返回的**不是** `get_XmlText` 的格式，而是服务端自己的序列化：节的顺序是 RegistrationInfo、Principals、Settings、Triggers、Actions；设置只写非默认的几项（顺序 DisallowStartIfOnBatteries、StopIfGoingOnBatteries、ExecutionTimeLimit、MultipleInstancesPolicy、RestartOnFailure、StartWhenAvailable、RunOnlyIfNetworkAvailable、IdleSettings、UseUnifiedSchedulingEngine）；`RunLevel` 为 LeastPrivilege、触发器 `Enabled` 为真时都不写；时长规范化（Office 给的 `PT03M` 存成 `PT3M`）。
- 注册时在 RegistrationInfo 末尾补上 `<URI>任务路径</URI>`，读回的定义 `get_URI` 也是它。
- 文件是 UTF-16LE 带 BOM，以 `<?xml` 开头，比 `get_Xml` 的文本大几百字节（文件里还有 `get_Xml` 不返回的东西）。

Wine 的 `get_Xml` 是定义的 `get_XmlText`，文件是 UTF-8 带 BOM、开头一行 Wine 的注释、去掉 XML 声明，也不补 URI——这些还没有对齐。

## 定义的其余部分、类型库与脚本：`taskparts.c`、`tasktlb.c`、`taskscript.vbs`

    scripts/build-probe.sh tools/taskschdprobe/taskparts.c tools/taskschdprobe/taskparts.exe ole32 oleaut32 advapi32 taskschd
    scripts/build-probe.sh tools/taskschdprobe/tasktlb.c tools/taskschdprobe/tasktlb.exe ole32 oleaut32

`taskparts.c` 测对象模型里其余的面：注册信息的 SecurityDescriptor、各部分自己的 XmlText、四种动作及其 XML 双向、Windows 10 加的
IExecAction2/IPrincipal2/ITaskSettings3/IMaintenanceSettings（默认值、XML 形式与位置、各自要求的版本）、各集合的枚举器、以 IDispatch
驱动对象。`tasktlb.c` 导出系统注册的 TaskScheduler 类型库（每个类型的种类、GUID、标志，每个方法的 DISPID、调用种类、参数名与标志），
也可给它一个 DLL 路径读那一个。`taskscript.vbs` 用 `Schedule.Service` 走一遍脚本路径（在 winref 上 `cscript //nologo` 运行）。

- SecurityDescriptor：新任务 VT_EMPTY；按原样保存，不校验，VT_EMPTY/VT_NULL 保留，其他类型转成 BSTR（字节数组即原字节）；XML 里写在
  RegistrationInfo 最后，空串写成 `<SecurityDescriptor></SecurityDescriptor>`，VT_NULL 不写；读 XML 也不校验。
- RegistrationInfo、Settings、Actions 各自的 `XmlText`：Windows 也是 `E_NOTIMPL`。
- 动作：Create 接受 Exec、ComHandler、SendEmail、ShowMessage，其余 `E_INVALIDARG`。读 XML 时 Exec 必须有 Command、ShowMessage 必须有
  Body、SendEmail 必须有 Server、From 和至少一个收件人（To/Cc/Bcc），HeaderField 必须有 Value（否则 `SCHED_E_MISSINGNODE`）；ComHandler
  的 ClassId 可省，必须是 GUID（带不带花括号都行，否则 `CO_E_CLASSSTRING`），Data 可以是 CDATA、写回成转义文本。SendEmail 总写出
  Body、HeaderFields、Attachments（没有时是空元素）。
- 兼容级别是整个定义共享的一个值：调用过 DisallowStartOnRemoteAppSession/UseUnifiedSchedulingEngine、ProcessTokenSidType、
  AddRequiredPrivilege 即升到 V2_1，Volatile 或维护设置 V2_2，HideAppWindow V2_4——设成 false 也算；`put_Compatibility` 直接覆盖，
  可以降低，降低后写 XML 就不写那些元素。读 XML 时声明了版本，超出它的元素 `SCHED_E_UNEXPECTEDNODE`；版本号只认 1.0–1.6。
- 元素位置：主体的 ProcessTokenSidType（非 Default 才写）与 RequiredPrivileges 在 RunLevel 之后；设置里 MaintenanceSettings{Period,
  Deadline, Exclusive} 与 Volatile（为真才写）在 UseUnifiedSchedulingEngine 之后、WakeToRun 之前；HideAppWindow（为真才写）在 Exec
  最后。维护设置缺 Period 为 `SCHED_E_MISSINGNODE`。
- 枚举器：Next 给出 VT_DISPATCH，不足时 `S_FALSE` 与实际个数；Skip 越界 `S_FALSE`；Clone 可用。在枚举中途增删集合会让 Windows 的
  探针进程崩溃，所以不测。
- IDispatch：对象的 IDispatch 用最派生接口的类型信息（ITaskSettings 指针用 ITaskSettings3，执行动作用 IExecAction2，触发器用具体
  触发器接口），GetTypeInfo(0) 是 TKIND_DISPATCH、标志 FDUAL|FNONEXTENSIBLE|FDISPATCHABLE；DISPID 是 SDK 的显式编号。

wine-src `13a0fba`（IDL）、`c8d033d`（schedsvc 报告 1.6）、`ff3d5e4`（taskschd）之后，`taskparts` 与 Windows 逐行一致，`tasktlb`
除 IMaintenanceSettings（put 在 get 之前）用 GetNames 读到的参数名外一致，`taskscript.vbs` 的输出逐字一致。
