# 设置与查询文件信息失败时的状态块、返回码，分配大小与大小写敏感：`setinfoprobe.exe` / `setinfoprobe32.exe`

Wine 的测试要求 `NtSetInformationFile` 失败时不写调用方的 `IO_STATUS_BLOCK`（改名、链接、完成端口几处 `io.Status` 仍是
0xdeadbeef）。探针把同样的问题问到测试没覆盖的地方，并且编一个 32 位版本，看 64 位 Windows 上经 wow64 转接时是否一样：

- 各可设置类给 0 与 1 字节的缓冲区（全零）：返回码，状态块是否被写；
- 不能设置的类（0、`FileStandardInformation`、`FileAllInformation`、0xffff）；
- 缓冲区够长但失败的请求：只读句柄设属性与文件长度、无 DELETE 的句柄设删除、改名到已存在的名字、完成端口；
- 成功的请求（状态块应为 0/0）；
- `NtQueryInformationFile` 失败时的状态块：缓冲区过短、只能设置的类、越界的类；
- `FileAllocationInformation`：对 4 字节文件设 1 MiB、1000、2、0、-1，再经 `SetFileInformationByHandle(FileAllocationInfo)`
  设 64 KiB，每步后打印文件长度与分配大小；只读、仅追加的句柄与目录；
- `FileCaseSensitiveInformation`：对目录查询、设置 1、0、2，无写属性权限的目录句柄，以及对文件设置。

状态块在每次调用前填满 0xbe 字节，“io untouched” 表示调用后仍是如此。文件放在 `%TEMP%` 下的新目录里，结束时删掉。

依据与已做的实现（altars-up）：

- `de83556c28c`：设置失败（NT_ERROR）时不写状态块，wow64 自己判定的错误也不写（之前把一个栈地址当 Information 写回）。
- `24d39f8e334`：缓冲区过短一律 `STATUS_INFO_LENGTH_MISMATCH`（ntdll 与 kernel32 的现有测试、[MS-FSA] 2.1.5.15 各节）；
  改名的名字长度为 0、奇数或超出缓冲区是 `STATUS_INVALID_PARAMETER`（[MS-FSA] 2.1.5.15.12）。
- `1ca7435ae08`：实现 `FileAllocationInformation`（[MS-FSA] 2.1.5.15.1，NTFS 按簇对齐，小于文件长度时截断到对齐后的值），
  设置分配大小与文件长度都要 `FILE_WRITE_DATA`，都不适用于目录。
- `5a94d3a043f`、`c4b07d0df2a`：改名后同一文件的其他句柄跟着新名字；目录下有打开的文件时拒绝改名目录。

`results/setinfoprobe.wine.txt`、`results/setinfoprobe32.wine.txt`：Wine（上述提交之后）。Windows 的待测（winref），据此决定：

- 不能设置与越界的类在 Windows 上是 `STATUS_INVALID_INFO_CLASS` 还是别的（Wine 现为 `STATUS_NOT_IMPLEMENTED`，wow64 为
  `STATUS_INVALID_INFO_CLASS`）；
- 查询失败时 Windows 是否也不写状态块（Wine 现在写），以及查询只能设置的 `FileEndOfFileInformation`（Wine 返回成功）；
- 分配大小的具体数值：缩小已有的预留时 Windows 会把分配收回到对齐值，Wine 保留预留（在 Linux 上释放会改掉修改时间）；
- `FileCaseSensitiveInformation` 的设置（Wine 未实现）。

构建：

    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror setinfoprobe.c -o setinfoprobe.exe -lntdll
    i686-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror setinfoprobe.c -o setinfoprobe32.exe -lntdll
