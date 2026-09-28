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
