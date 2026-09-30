# WIC 的 GIF 编码器与元数据：`gifencprobe.exe`

PowerPoint 导出动画 GIF 时经查询写入器给编码器设循环次数（NETSCAPE2.0 应用扩展）与注释、给每帧设延时和处置方式。探针编码
两帧 GIF，打印每次调用的结果、设置前后各查询写入器能枚举的名字与各块写入器持有的块、第一帧上错误类型的值、块里没有的项和
GIF 没有的块的结果，再逐块走读输出的字节并用 WIC 解码器读回。

`results/gifencprobe.wine.txt`：altars-up（`01c911684cf` 补上元数据写入链）的输出；Windows 的待测（winref），用来核对空块列表、
不设 /grctlext 时是否写 GCE、几种错误码等未实测的选择。
