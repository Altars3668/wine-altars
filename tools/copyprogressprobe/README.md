# CopyFileEx 与 CopyFile2 的进度回调：`copyprogressprobe.exe`

Wine 原先对 `CopyFileEx` 的进度回调和取消标志、`CopyFile2` 的进度消息一律记 FIXME 后忽略。kernel32 的测试（Windows 实测）
只定下一种情形：第一次回调是 `CALLBACK_STREAM_SWITCH`；回调答 `PROGRESS_CANCEL` 时复制失败、错误码
`ERROR_REQUEST_ABORTED`，并在能删时删掉副本——目标先前被别的句柄以不共享删除的方式打开时副本保留，共享删除时副本被删，
可见 Windows 先以 `GENERIC_WRITE|DELETE` 打开目标、冲突时退回不带 `DELETE`。altars-up 照此实现，其余按文档
（CopyProgressRoutine：每块复制完 `CALLBACK_CHUNK_FINISHED`；`PROGRESS_STOP` 失败但留下副本，`PROGRESS_QUIET` 继续但不再回调；
取消标志置真即取消），`CopyFile2` 发 STREAM_STARTED、CHUNK_STARTED、CHUNK_FINISHED、STREAM_FINISHED 消息。

探针打印空文件、100 000 字节、3 000 000 字节三种源文件的每一次回调（原因、流号、已传/总量），`CopyFileEx` 在第一块时答
`PROGRESS_STOP`、`PROGRESS_QUIET`、`PROGRESS_CANCEL` 或置取消标志的结果与副本去留，以及 `CopyFile2` 的消息序列。

`results/copyprogressprobe.wine.txt`：Wine（按 64 KiB 一块）。Windows 的待测（winref）：块大小、空文件是否也报一块、
`CopyFile2` 的消息与 dwFlags，据此校准。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror copyprogressprobe.c -o copyprogressprobe.exe`。
