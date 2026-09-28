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

Wine 原先：主体、触发器集合、动作集合、空闲/网络设置的 getter 全是 `E_NOTIMPL`，没有 `ITaskSettings2`，读 XML 时触发器与动作被跳过，存下的任务是 `<Triggers/>`、`<Exec/>`、空的 `<Principal>`——Office 每次启动都重新注册一遍。与 `taskdump.win.txt` 相差 1719 行。
