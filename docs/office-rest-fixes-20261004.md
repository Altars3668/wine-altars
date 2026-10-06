# Office 剩余功能修复：第一批

2026-10-04，接续用户“全部修上”的任务。本页只记录已实施、实测的第一批；复杂 COM/RPC 安全、RAS/profile、MSXML 外部 grammar、TSF 和其它待核实诊断仍须继续，不将本批写成全部完成。

## 原生测量与实现

原生参考仍为 Windows 11 build 29671。第 154–158 批均使用自有对象、纯内存证书存储或临时 loopback 监听器，不修改系统证书、FIPS 设置、用户文档或网络配置。

- **ImpersonateLoggedOnUser**：原生对本进程生成的 anonymous/identify/impersonation/delegation token 与 primary token 都接受。Wine 与原生一致，保留实现，只将过时的一次性 FIXME 改为 TRACE；不能根据旧文档猜测拒绝低模拟级别。
- **BCryptGetFipsAlgorithmMode**：读 Vista 的 FipsAlgorithmPolicy\Enabled，兼容旧 Lsa 值，按 DWORD 非零返回策略状态；wine.inf 提供不覆盖现值的禁用默认项。参数检查不变。隔离 Wine 测试保存、修改、恢复策略，原生仅只读查询。这里不是宣称宿主密码库获得 FIPS 认证。
- **AI_FQDN**：补公共常量 0x20000，输出 canonical name；与 AI_CANONNAME 同用原生是 WSAEINVAL，不能只忽略 flags。numeric host 自动返回 AI_NUMERICHOST；AI_CANONNAME numeric 做 reverse lookup，AI_FQDN numeric 保留字面 canonical。主机名与地址数量随环境不同，不逐字比较。
- **CertAddCertificateLinkToStore**：link 的 REPLACE_EXISTING_INHERIT_PROPERTIES、NEWER_INHERIT_PROPERTIES 原生拒绝 E_INVALIDARG；USE_EXISTING 返回旧 link，不把 incoming 属性拷进去。正常新 link 仍共享 source 内容和属性。缺失属性的输出长度清零。普通 context-copy 的属性继承不变。
- **WsAbortServiceProxy / WsAbortChannel**：CREATED、CLOSED 状态保持；OPEN 取消后进入 FAULTED，可 close/reset。真正中断 I/O 后才等待调用持有的锁，不只是存状态或回成功。
- **WinHTTP 下层关闭**：原来 request 关闭没有 handle-closing 回调，worker 持有引用时网络继续阻塞；关闭同步 ReceiveResponse 原生应即时取消并返回 12017。新增关闭钩子、连接指针发布/脱离保护和活动 I/O 计数。只中断活动 I/O，不取消空闲或已转给 WebSocket 的共享连接。

## 失败尝试与回归

只补 Web Services 状态不够：阻塞调用持有 proxy/channel 锁，取消等锁导致直到服务器放行才返回。自有 received/release 事件探针把这条路径分离出来；底层 WinHTTP 也需修复。

第一版 WinHTTP 每次 close 都中断 netconn，导致已升级 WebSocket/空闲连接回归：notification 超时，winhttp 多项失败。改成只取消活动 I/O 后完整回归恢复 0 失败。保留失败日志，不把初版测试误报成功。

Winsock 首轮完整测试因 www.kernel.org 临时解析失败产生 4 项错误；未改运行时与新运行时的同一 DNS 探针随后都成功。网络恢复后完整测试重跑 0 失败，不通过改宿主 DNS 或屏蔽断言掩盖。

## 验证

| Wine 模块回归 | 测试项 | 失败 |
|---|---:|---:|
| webservices proxy | 177 | 0 |
| webservices channel | 449 | 0 |
| bcrypt | 23541 | 0 |
| advapi32 security | 3857 | 0 |
| crypt32 cert | 653 | 0 |
| crypt32 store | 711 | 0 |
| winhttp notification | 2413 | 0 |
| winhttp winhttp | 4663 | 0 |
| ws2_32 protocol（复测） | 7728 | 0 |

仍有模块已有 todo/skipped，上表不是“所有行为已实现”。

原生隔离子回归：proxy abort 30、channel abort 30、FQDN 20、certificate links 53 项，均 0 失败。阻塞探针确认服务器已收到请求后，取消在服务器放行前完成；WinHTTP 返回 12017，Web Services 返回 0x803d0004。不是仅看探针退出码。

原子部署到 dist-up，替换双架构 DLL、Winsock Unix SO 和 wine.inf；未更新 /opt，不运行删除旧安装的脚本。真实 Word、Excel、PowerPoint 再次创建、编辑、保存并关闭，实际自动化退出码均为 0，三份 OOXML ZIP/核心 XML 有效。

## 后续仍在推进

- NtSetInformationKey 的控制标志等信息类；不能把日志 class2 凭猜测写成虚拟化类。
- TSF text-change、attrs-change、range/edit notifications；Linux 凭据后端；缓解策略16的实际执行。
- 受限 CPU 集合下 Wine 的 CPU 枚举/亲和性一致性，不修改宿主 CPU 保护策略。
- 非 NULL COM security、认证服务和 EOAC、RPC AppContainer SD。
- RAS/RasMan 实际连接、profile hive/roaming 生命周期。
- MSXML inline schema、entity resolver、外部参数实体/DTD。

已知 ClickToRun msoxmlmf 清单加载失败不再算缺口。以上待办不是本批已完成项目。
