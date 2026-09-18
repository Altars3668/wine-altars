# WAM ABI 对照探针

直接调用 `Windows.Security.Authentication.Web.Core` 的 WinRT 接口，不依赖
PowerShell 对 `System.__ComObject` 的成员投影。不修改 Office 的授权判断。

## 构建

```sh
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -municode \
  -o tools/wamprobe/wamprobe.exe tools/wamprobe/wamprobe.c \
  -lruntimeobject -lole32 -lbcrypt
```

## 四种模式

```text
wamprobe.exe control
wamprobe.exe plain
wamprobe.exe office
wamprobe.exe office-silent
```

- `control`：构造带固定测试字符串的 `WebTokenResponse` 并读其集合；不查账户、不联网。
- `plain`：向 Microsoft account provider 静默请求 Office 的 SSL scope，不附加属性。
- `office`：相同请求，附加实际 Office 交互阶段观察到的 15 个属性；每个属性先写入再读回核对。
- `office-silent`：附加后续静默请求的 10 个属性，不带 `Client_uiflow`、`fl`、`lw`、`noauthcancel`、`signup`。

后三种在原生 Windows 上是**真实的账户凭据操作**，须在获准的用户交互会话中运行。
`office` 也使用静默获取 API，其名称表示属性配置，不代表探针会弹出交互窗口。
Windows SSH 服务会话不等于交互会话，不能用其中的 `ERROR_NO_TOKEN`
否定 broker 在桌面中的行为。程序使用 MTA，轮询 `IAsyncInfo`，最多等待 120 秒。
此工具不自动创建计划任务，也不修改 PowerShell 执行策略。

标准输出只有 HRESULT、集合键名、长度、身份摘要、状态和失败数，不包含 token、
账户名或属性值。`failures=0` 仅表示本次调用链/结构探测通过，**不表示 Office
已登录、许可证有效或已激活**。Wine 固定样本模式没有自动过期检查或刷新，尤其不能
把其 `ResponseStatus=Success` 当作服务器验证。

## scope 与私有导出

```text
wamprobe.exe plain|office|office-silent|control [private-output-directory [scope]]
wamprobe.exe office C:\path\to\private-output
wamprobe.exe office-silent - "service::officeapps.live.com::MBI_SSL_SHORT openid profile"
```

第三个参数是预先建立并限制访问权限的输出目录；`-` 表示不导出。
第四个参数可指定 scope，默认值为
`service::ssl.live.com::MBI_SSL_SHORT openid profile`。

导出保存原始 token、账户字段和属性值，全部视作凭据材料。不要提交到仓库、
上传为报告、写入私有笔记或复制到聊天。保留必要时间后删除。

每次导出的非秘密请求 manifest：

- `request-scope`：精确的请求 scope，不能只依赖清洗后的目录名。
- `request-client-id`：Office client ID。
- `request-profile`：本次模式名称，如 `office-silent`。

每个 response 导出：

- `response-N-token`：原样 UTF-8 `WebTokenResponse.Token`，不是抽取后的 access ticket。
- `response-N-account-id` / `response-N-account-name`：同一次响应关联的账户。
- `response-N-properties` / `response-N-account-properties`：原始 UTF-16 字符串 map。

map 格式为 little-endian `u32 count`，每项依次是 `u32 key_chars`、UTF-16LE key、
`u32 value_chars`、UTF-16LE value。不带 NUL，不推断和补造任何字段。

## Windows 批量采集

`probe.ps1 -RunDirectory <新目录>` 可从获准的 Windows 交互终端运行。目录中需先放入
`wamprobe.exe`；脚本限制 ACL 为当前用户与 SYSTEM，然后分别导出 `plain`、`office`
结果，最后写 `completed` 标志。运行目录必须是专用的新目录，不能复用已有输出。
脚本本身不负责取得交互会话权限；调用方不得绕过执行工具的权限拒绝。

追加 `-AllScopes` 后，改用 `office-silent` 依次请求以下九个 scope：

```text
service::officeapps.live.com::MBI_SSL_SHORT openid profile
service::ssl.live.com::MBI_SSL_SHORT openid profile
openid service::https://graph.microsoft.com/.default::DELEGATION profile
https://consentservice.microsoft.com/checkin/UnifiedUserConsent.Read openid profile
https://substrate.office.com/.default openid profile
service::ads.arcct.msn.com::MBI_SSL openid profile
service::messaging.engagement.office.com::MBI_SSL_SHORT openid profile
service::outlook.office.com::MBI_SSL openid profile
service::substrate.office.com::MBI_SSL openid profile
```

子目录名将 scope 中不属于 `[A-Za-z0-9._-]` 的字符逐个替换为 `_`。
每个成功的单响应目录有八个文件（三个请求 manifest 加五个响应文件）。
任一 scope 退出非零，最终 `completed` 就是 `1`；全部成功才是 `0`。
本轮九 scope 任务使用参考机既有的 `RemoteSigned` 策略，无需 `-ExecutionPolicy Bypass`。

## Wine 固定样本诊断

- `WINE_WAM_CAPTURE_DIR`：使用 **Windows 路径**指向已有私有主账户导出目录。
- `WINE_WAM_CAPTURE_ROOT`：可选，指向 `-AllScopes` 的根目录，按 scope 查找样本。

只有匹配 Office client ID、MSA provider、consumers authority，以及
`api-version=2.0` / `oauth2_batch=1` / `x-client-info=1` 的请求才能读取样本。
设置 ROOT 后，还必须核对原始 scope、client、`office-silent` 采集配置和主账户 ID。
ROOT 缺样本时不得拿其他 scope 的样本补齐；目录名碰撞也必须被拒绝。
未设置 ROOT 时保留仅支持 SSL scope 的单样本兼容路径。

`0026-onlineid-match-scoped-captures-without-legacy-fallback.patch` 将“未启用采集模式”
与“已启用但拒绝请求”区分开。前者仍可走旧存储，后者不能因旧存储非空而错误成功。
ROOT 必须配合主账户 DIR；plain 请求、错误 manifest、缺失主账户目录均不得回退。

**有效期由诊断调用方负责核验。** 原样封装里的 `expires_in` 是采集时的相对期限，
不能每次读取都视为获得一个全新有效期；`id_token` 未过期也不证明 `access_token`
仍有效。本轮 Graph、consent、substrate `.default` 的一小时样本在后续复测前已过期，
因此从拟用样本集合排除，没有改写期限或把服务拒绝计为 Wine 的新缺陷。

该模式不是在线 broker、通用凭据缓存或自动刷新实现。完成诊断后移除变量，
在不再需要时清理采集文件。不要将变量设成全局默认，不另行复制或改写凭据。

## 无真实凭据的隔离回归

```sh
DISPLAY=:77 \
WAM_CAPTURE_TEST_WINE="$PWD/dist-cx/bin/wine" \
WAM_CAPTURE_TEST_TMP="$CLAUDE_JOB_DIR/tmp" \
WAM_CAPTURE_TEST_DLL="$PWD/wine-src/build64-cx/dlls/windows.security.authentication.onlineid/x86_64-windows/windows.security.authentication.onlineid.dll" \
python3 -m unittest discover -s scripts/tests -p test_wam_capture.py -v
```

`WAM_CAPTURE_TEST_TMP` 必须是现存的临时目录；非任务会话请显式指定自己的目录。
省略 `WAM_CAPTURE_TEST_DLL` 时，测试使用所选 Wine 运行目录中的 DLL。
测试自己创建全新 prefix，首次 `wineboot` 之前就把 `Z:` 映射到私有测试目录，
只写 `TEST-ONLY-…-NOT-A-CREDENTIAL` 控制字符串，不启动 Office、不调用原生 broker。
不要把这一测试驱动拿到原生 Windows 上运行。测试结束后只关闭并清理自己创建的 prefix。

覆盖正确 scope、清洗碰撞、错误 client/profile/account manifest、缺失文件、空文件、
无效 UTF-8、plain 拒绝、ROOT 缺主账户、单样本兼容及显式禁用后的 legacy 兼容。
拒绝测试特意在隔离旧存储中放入控制字符串，避免“旧目录恰好为空”掩盖错误回退。
普通 `unittest discover` 不设置 opt-in 变量时会跳过这些 Wine 集成测试；skip 不算通过。

## 已有原生证据与边界

- Windows `control`：返回非空空 map，失败数 0；强类型 PowerShell 的同一构造实验一致。
- Windows 交互 `plain`/`office` A/B：2026-09-06 经明确授权完成，两次失败数、退出码均为 0；这一批临时任务及远端十份凭据已清理。
- plain 返回 `t`/`p`；office 返回 `access_token`、`token_type`、`expires_in`、`scope`、`id_token`、`client_info` 表单编码封装。
- 原生账户 Properties 为九项非空 map；Response.Properties 在 plain 模式为空 map，在 office 模式有 `MATS`，均不是 NULL。
- 实际 Office 解析旧 ticket 时三个必需字段长度全 0；读取原生 office 封装后，长度与原生完全吻合，本机开始生成身份缓存，重启后账户页显示“已登录”。许可证尚未通过。
- 后续九 scope 原生采集全部退出 0；Wine 的九正例、两个反例曾通过。该批临时任务此前已删除，对应远端敏感导出于 2026-09-07 核对本地完整性后清理。
- 2026-09-06 的逐 scope 新 Word 启动曾被执行权限层阻止。2026-09-07 明确授权后，0026 已部署，新 Word 已实际消费九个对应响应，记录的返回状态均成功；此前缺失的 Graph、consent、substrate 等路径开始解析服务 DNS。许可弹窗仍存在，许可证文件未更新，不能据此宣称激活。
- 9 月 7 日的两批刷新均复用参考机既有探针，没有上传被拒绝的本地代码；每批九项原生请求及 72 个接收文件校验均通过。9 月 6 日九 scope 批次以及这两批的远端敏感响应均已清理，临时任务也已删除。本地仍被诊断引用的私有原件尚保留。

## PowerShell 5.1 编码

`probe.ps1` 必须保留 UTF-8 BOM。原生 Windows 默认 GBK 解码无 BOM UTF-8 时，
实测会把中文注释后的两条赋值吞入注释，而语法检查仍报告零错误。
添加 BOM 后同一临时任务才正常完成。编码检查应与实际执行引擎一致。
