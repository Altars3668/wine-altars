# 可警报等待里排队的用户 APC 与已触发对象谁先：`waitorderprobe.exe`

kernel32 的测试（Windows 实测）定下：线程已有排队的用户 APC 时，对已触发事件做可警报的 `WaitForSingleObjectEx`
返回 `WAIT_IO_COMPLETION`，事件保持触发。上游 Wine 反过来先看对象，把这几处记作 todo。user32 的测试另定一处：
`MsgWaitForMultipleObjectsEx` 不带句柄、带 `MWMO_ALERTABLE` 时，已在等的消息优先于 APC，APC 留在队列里。

altars-up 照此改了三处：wineserver 的 `check_wait` 在看对象之前先看可警报等待的用户 APC；ntdll 的 ntsync 路径在进内核
等待之前先读线程的警报事件（本机没有 `/dev/ntsync`，这条路径未实测）；win32u 的 `wait_message` 在可警报的 WaitAny 里，
等待前的队列检查已见到输入时直接返回队列序号。

探针问测试没覆盖的情形：自由的互斥体、本线程已拥有的互斥体、被遗弃的互斥体、信号量、两个对象的 WaitAny 与 WaitAll、
`SignalObjectAndWait`、带一个事件句柄的 `MsgWaitForMultipleObjectsEx`（各种 flags、消息新旧、有无 APC），以及 STA/MTA
里的 `CoWaitForMultipleHandles`。每行打印返回值、这次等待跑了几个 APC、还剩几个在排队、对象之后的状态。

`results/waitorderprobe.wine.txt`：altars-up。Windows 的待测（winref）：本线程已拥有的互斥体是否也先跑 APC；有事件已触发
又有新消息时，flags 为 0 与 `MWMO_ALERTABLE` 是否都返回队列序号（若是，说明 user32 在所有 flags 下都先查队列，
win32u 的条件应去掉 `MWMO_ALERTABLE`）；`CoWaitForMultipleHandles` 超时时写不写 index。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror waitorderprobe.c -o waitorderprobe.exe -lole32`。
