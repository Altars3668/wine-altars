# Office 前缀更新诊断：原生验证与修复

2026-10-03；原生参考是 Windows 11 build 29671、zh-CN。不是删除 FIXME 或让桩返回成功：先量 Windows 行为，再补 Wine 底层、回归测试和应用路径。工具在 `tools/prefixupdateprobe`，最初 `*.wine.txt` 留作修前 baseline，修后另存 `*.wine-after.txt`。第 152 批直接使用当前源码重编的交付探针。

## 第一组：系统查询、RAS、已挂载用户配置

- `561b5f519c4`：SystemLeapSecondInformation 只接受精确结构大小；kernelbase/kernel32 导出 GetSystemLeapSecondInformation，并读回查询结果。原生短/正好/较大缓冲区契约已量，不对原生传野指针或 NULL 做崩溃测试。
- `f870e871d0d`：RasEnumConnectionsA/W 接受五代合法结构大小，短缓冲区、非法大小和空枚举按原生；RasConnectionNotificationA 转 W，验证 event、通知 flags 和连接句柄。Wine 没有 RasMan，原始守卫探针会跳过；另用单元测试和原生测量验证 API，不能把“跳过”写成实测了实际拨号。
- `7aedff067fb`：GetProfileType、LoadUserProfileA/W、UnloadUserProfile 对已挂载普通 profile 使用 TokenUser SID 对应的 HKU 键，校验结构及用户名，关闭真正的键句柄。不再把预定义 HKCU 当作加载成功。未挂载 hive、roaming profile 不在覆盖中。

## 第二组：默认 COM security、RPC SD 与 owner

提交：`5807145cef4`（server token/owner），`a50f8b1b8be`（COM security/blanket），`ada7777e5e6`（RPC 接口 SD）。

### 真正的权限规则

- CoInitializeSecurity 记录进程默认配置和初始化时机；真实封送锁定隐式默认配置，重复或过晚调用返回原生错误。CoQueryProxyBlanket 的本地代理结果按实际参数返回。复杂非 NULL SD、自定义认证服务、复杂 EOAC 模式没有因为默认路径通过而变成全面实现。
- RpcServerRegisterIf3 保存并释放接口 SD，在服务端分派前按客户端令牌检查，拒绝先于安全 callback。正确规则不是“必须有执行位 1”：原生用 MAXIMUM_ALLOWED，有效集合是 `STANDARD_RIGHTS_ALL | 1`。READ_CONTROL、WRITE_DAC、SYNCHRONIZE 也能使调用通过；仅位 2/4 不行。deny 位 1 后 allow GA 仍可能允许；deny 位 1 后仅 allow 位 1 则拒绝。
- 所有者只隐含 READ_CONTROL | WRITE_DAC，不隐含 DELETE/WRITE_OWNER/SYNCHRONIZE；普通 deny ACE 不会在 MAXIMUM_ALLOWED 下吃掉这两项。出现 OWNER RIGHTS SID（S-1-3-4、SDDL OW）ACE 后，自动 owner 权关闭，由该 ACE 控制。
- 初期看到“管理员在空 DACL 下也能调用”，不是普遍管理员绕过，更不是 SD 被忽略：参考机 SSH 管理员令牌的默认 owner 是 Administrators，仍可用 owner 控制权。禁用 Admin 组、显式 SYSTEM owner 的空 DACL 都能原生拒绝，因此没有保留管理员硬编码放行。
- 底层修复保留复制 token 的 owner、区分 primary group、过滤 owner 组后回到 token user，补默认 DACL，并落实 OpenThreadToken 的 OpenAsSelf。文件合成 DACL 显式给 DELETE，不能依赖并不存在的 owner 隐含 DELETE 权。

### 测量器不能代替测量对象

1. `RPC_IF_SEC_NO_CACHE` 加 NULL callback 原生返回 87。注册失败后没有执行服务器，不能拿“没有调用”推断 ACL。最终探针先量合法 flags/callback 矩阵，再做独立进程的 SD 场景，记录服务器真实执行次数。最初 `rpcsd.win.txt` 保留为无效注册证据，不冒充 ACL 规格。
2. Wine 的异常框架在 Wine 中有效，不保证原生 x64 SEH 有效。第 137 批旧捕获器自检出现未处理异常、退出 5，不能报成预期拒绝或成功。后续回归改成 `__C_specific_handler`/x64 unwind SEH，自检成功后才测 RPC；仅捕获低位 RPC 状态，不吞掉访问违例。静态认证身份缓存也会污染同进程切换身份，使用新客户端或动态 identity tracking。

第 146 批原生隔离 owner 断言 71 项、RPC 安全断言 421 项，均 0 失败。不能在参考机跑完整 advapi32 security 或 RPC server 套件；本轮只跑 `security owner_access`、`server test security`，不走防火墙等无关修改路径。

第 152 批最终探针：18 组 SD 的受限客户端判决符合矩阵；另外用真实 callback 测 deny-all，确认拒绝前没有回调/服务器执行，用普通管理员测 SYSTEM owner 的空 DACL，仍拒绝。所有探针实际退出 0，无安全超时；预期拒绝以已捕获 `call_exception=0x5` 输出，不把进程退出 0 本身当作“ACL 一律允许”。

## 第三组：存储属性、MSXML 与 libxml 布局

提交：`a3e23b62b7f`（libxml entity 公共布局），`f7832cb392c`（storage 查询），`b160c3b0494`（MSXML）。

### 宿主存储元数据与缓冲区契约

Linux sysfs 提供实际总线、设备类型、removable、queueing、型号/固件/序列号、rotational 和 discard。目录型 C: 用 `stat().st_dev` 找宿主块设备，分区提升到整盘。没有可映射物理设备时明确报告 Wine virtual disk/BusTypeVirtual，不能伪装成 SCSI，也不能把参考机的 NVMe 产品和序列号照抄过来。DeviceType 是 SCSI 类型（磁盘 0、光盘 5），不是 FILE_DEVICE_DISK。

IOCTL_STORAGE_QUERY_PROPERTY 的原生边界：

| 项目 | 实测行为 |
|---|---|
| 输入小于 12 字节 | ERROR_BAD_LENGTH（24） |
| 设备描述符输出为 0 或任意短长度 | 成功，返回 `min(outsize, 完整长度)` |
| seek/trim 输出小于 8 字节 | ERROR_INSUFFICIENT_BUFFER（122） |
| seek/trim 输出 8–11 字节 | 成功，只返回 8 字节 header |
| seek/trim 输出至少 12 字节 | 成功，完整结构 12 字节 |
| 支持的 PropertyExistsQuery | 成功，0 字节 |
| 非标准/exists query type | ERROR_NOT_SUPPORTED（50） |
| 未知 property | ERROR_INVALID_FUNCTION（1） |

清零 padding，不泄漏栈字节。第 148/152 批边界探针与第 151 批原生 storage_property 子测试互证。完整设备描述符长度和硬件身份随机器不同，不能要求这些字节跨机器相同。

### 解析器选择、实际验证与错误生命周期

NewParser=false 走 MSXML3 SAX，true 走 MSXML6 SAX；DOM 私有定位特性不污染公开 SAX。

- 新路径 validating=true 遇 DTD 原生返回 `0xc00ce23d`；旧路径支持 DTD 验证。同一标签错误旧路径是 `0xc00ce56d`，新路径是 `0xc00cee3b`，行列也不同，不能仅凭 property setter 回显判断。
- schema cache/properties 克隆进解析中的临时根节点，ValidateOnParse 确实做 DTD/XSD 验证。无效整数 XSD 的错误是 `0xc00ce201`，解析时拒绝加载；失败树释放，后续 validate 空文档返回 `0xc00ce223`。手动 validate 的行列仍是 0。
- locator 是借用对象，parse 返回后可能已经释放；解析后验证需要先 AddRef，再 cleanup Release。无错误时 parseError 的 line/linepos getter 返回 S_FALSE 和 0，这是 MSXML3/6 的原生行为。
- schema cache 的 validateOnLoad=false 允许 add 无效类型 XSD，但之后 cache.validate() 真正编译并返回 E_FAIL；true 时无效 add 直接失败，空 cache.validate() 成功。延迟编译有锁和完整释放，不是空成功。
- ResolveExternals 标准/高级 setter 同步实际解析选项。外部 general entity 真读入内容，支持 base URL、UTF-8 和带 BOM UTF-16。自定义 SAX entity resolver、完整外部参数实体/外部 DTD、UseInlineSchema=true 仍是边界。
- 第 152 批复杂 DTD：旧 MSXML3/6 都接受 notation、未解析实体和参数实体声明组成的合法样本；新解析器按 DTD 验证规则拒绝，错误定位为第 2 行、第 13 列。这里没有真的加载外部 grammar。

### DTD 释放崩溃根因

Wine 为 libxml 公共节点头增加 `_private2`，xmlNode/xmlElement/xmlAttribute/xmlDtd 有，xmlEntity 却漏了。实体强转 xmlNode 后 type/doc 错位，xmlFreeDtd 最终把 `0x500000000` 当作 content 释放。补 xmlEntity 槽及五个预定义实体初始化器，并加 type/doc offset 编译期断言。不是通过删验证或一律成功绕过崩溃。

复制 DTD 还必须挂 `dtd->parent=doc`、`xmlSetTreeDoc(dtd,doc)`，否则 notation/unparsed entity 的合法复杂 DTD 会被错误拒绝。这是上下文完整性问题，不是 libxml 不支持该 DTD。

## 最终回归、部署与真实应用

- Wine 全量：kernel32 volume 649 项、MSXML domdoc 43872 项、schema 672 项、saxreader 9566 项，均 0 失败，仍有已有 todo，不将 todo 写成已实现。
- 第 151 批原生隔离回归：storage_property 54 项、newparser 97 项（3 skipped）、deferred 25 项，均 0 失败；第 152 批再以当前交付探针核对 SD、owner、buffer、复杂 DTD 和延迟 schema。
- COM security、ole32 compobj/marshal、RPC server/rpc/cstub/ndr_marshall/rpc_async、advapi32 security、ntdll file/om、kernel32 file/time、userenv、rasapi32 的本轮 Wine 扩展回归均 0 失败。ntdll info **仍有 16 个 CPU 亲和性环境失败**，改动前运行时复测相同；不能称整个扩展回归零失败，没有修改宿主 CPU 限制来掩盖。
- 原子部署到 `dist-up`，包括双架构 DLL、Unix SO 和 wineserver；`wineboot -u` 更新开发前缀。没有更新 `/opt`，没有运行会删除旧安装的 install-system-wine.sh。仅替换文件 inode，不原地覆盖 Office 已映射 DLL。
- 真实 Word、Excel、PowerPoint 经 COM 创建、编辑、保存，进度步骤完整，三个 OOXML 文件通过 ZIP/核心 XML 校验。Word 另做冷启动、新文档、输入正文和截图目视，正文/Ribbon 正常，两份测试文档无保存关闭，实际 Word 退出 0。没有读取用户邮件或发送打印任务。
- 此次 Word 冷启动本批目标诊断和 check_noexcept 都为 0，但整份日志**仍有 15 条诊断**。例如 mitigation policy、NtSetInformationKey、FIPS 查询、winsock flags、证书 link、凭据枚举需要各自核对；不据日志名字直接判成缺口。`msoxmlmf.dll` 在 ClickToRun 路径的加载失败早已实测为清单行为（原生也失败），不再列为未修项目。

本组证明的是这些原生契约和 Office 默认启动/编辑保存路径。不证明完整 RAS 拨号、所有 profile、复杂 COM security、RPC AppContainer、所有 XML 外部 grammar 或 Office 所有功能已实现，更不等于“支持最新 Windows 全部特性”。
