# SPPC 调用契约对照

此工具只调用指定 SLC/SPPC API，不安装、激活、重置或卸载许可证，也不输出密钥、
认证材料、设备绑定、到期时间或宽限数值。`consume*`、`isolation` 和历史失败测试
会执行权利评估，**不是纯只读操作**，必须明确授权并传入 `--evaluate`。

## 构建

```sh
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -municode \
  -o "$CLAUDE_JOB_DIR/tmp/sppcprobe.exe" tools/sppcprobe/sppcprobe.c \
  -lole32 -lversion
```

程序从 Windows system directory 加载 DLL，记录版本和查询入口真正归属的模块；
不依赖 Office 私有 ordinal。当前 MinGW 缺少部分 SLC ABI，编译时使用仓库的
`wine-src/include/slpublic.h`。

```text
sppcprobe sppc|slc <case> [--evaluate]
```

- 查询/上下文：`fresh`、`explicit`、`closed`、`invalid`、`outputs`、`failure-outputs`、`cross`。
- 明确启用评估后：`consume`、`consume-sku`、`isolation`、`failure-history`、`invalid-consume`。

每个原生异常用例放在独立临时进程中；查询可能启动服务或写入缓存/事件，不能把
“不输出秘密”说成“完全无副作用”。`probe-failures=0` 表示仪器获得了可解析的结果，
不是所有 API 都返回 S_OK，更不是产品已获得许可。

## 已测原生行为（2026-09-07）

本轮 x64 Windows 的 sppc.dll/slc.dll 版本资源为 6.2.29648.1000。原生 slc 查询入口
实际位于 SPPC.DLL；两个 DLL 的上下文可交叉查询。以下均实际运行，不以文档推测：

| 情形 | HRESULT / 结果 |
|---|---|
| 新句柄，App/SKU 都 NULL | `0xC004F002`，输出保持调用前值 |
| 两次 SLOpen | 两个独立句柄 |
| NULL handle | `E_INVALIDARG` |
| 非 NULL 无效/已关闭 handle | `0xC0030005` |
| NULL count/array 输出指针 | `E_INVALIDARG`，其他输出不变 |
| 非 NULL 查询 right name | `0xC004F016` |
| 未知 App 或 App 下未知 SKU | `0xC004F015` |
| 只给 SKU、App 为 NULL | `E_INVALIDARG`（包括评估后） |
| 显式查询之后再 NULL/NULL | 不产生消费历史，仍为 `0xC004F002` |
| 消费单个未授权 SKU | `0xC004F013`；其后可查询一条未授权结果 |
| 已有历史后消费未知 SKU | `0xC004F015`；历史被清除 |
| 已有历史后消费时 App 为 NULL | `E_INVALIDARG`；旧历史保留 |
| 在一个句柄消费 | 另一个句柄仍无历史 |

参考机整个 Office App 的评估返回 S_OK，但结果包含 32 条未授权记录和一条通知
记录，没有 LICENSED；这不意味着可以复制其状态，也不意味着所有未授权评估都
应返回 S_OK。Wine 当前目录没有权利后端，返回有依据的未授权结果，不产生通知、
宽限或已授权状态。

官方 `SLGetLicensingStatusInformation` 的 NULL/NULL 是“上次权利评估结果”，
**不是全目录枚举**。早期 `sku-contract` 的全零 SKU 实验是缺陷复现，不能据此
把新句柄默认行为改成枚举所有产品。

## 安装元数据导出与验证

`export-catalog.ps1` 只查询 Office 的 `ID`、`ApplicationID`、`Name`，附上实际
ProductReleaseIds、Office 版本与 UTC 来源时间；保留 UTF-8 BOM 以兼容 PowerShell 5.1。
输出文件使用 CreateNew，拒绝覆盖。它不导出激活状态、密钥或宽限期。

Linux 上先验证，不写 prefix：

```sh
python3 scripts/sppc-catalog.py metadata.json \
  --licenses-dir "$HOME/.wine-altars-office/drive_c/Program Files/Microsoft Office/root/Licenses16" \
  --target-products O365HomePremRetail --target-version 16.0.20208.20000
```

显式注册时必须关闭 Office；目标产品和版本由工具读取实际 prefix，不能从命令行
伪装目标身份：

```sh
python3 scripts/sppc-catalog.py metadata.json \
  --licenses-dir "$HOME/.wine-altars-office/drive_c/Program Files/Microsoft Office/root/Licenses16" \
  --register --wine "$PWD/dist-cx/bin/wine" --prefix "$HOME/.wine-altars-office"
```

- SKU 来自对应 PPD 的技术性 title，而不是根 licenseId；AppID 由两个位置互证，
  editionId 与安装来源的名称对应。
- 同 PRID、PPD 文件存在或分类器认识 GUID，都不能单独证明本机已安装/授权。
  输入必须是显式注册来源，且逐项匹配本地定义；不能把全部分发文件当作目录。
- 元数据只写 `HKLM\Software\Wine\SPPC\Applications` 的指定 Office App 子树。
- 首次创建前保存 absent 状态，写入后导出并逐项核对。重复相同注册不写入；已有
  不一致目录拒绝覆盖。失败只回滚本次已知内容，检测到未知并发变化就保留现场。
- 备份保留在目标 C: 的 `ProgramData\Wine\SPPC\Backups`；工具会打印实际路径。
- 普通 `import-office.sh` 不自动注册；可显式传 `--sppc-catalog metadata.json`，且
  目标 C2R 注册表必须已经导入。dry-run 不执行新的目录注册。

## 隔离回归

```sh
TMPDIR="$CLAUDE_JOB_DIR/tmp" python3 -m unittest discover -s scripts/tests -p 'test_sppc_*.py' -v

DISPLAY=:77 \
SPPC_TEST_WINE="$PWD/wine-src/build64-cx/wine" \
SPPC_TEST_TMP="$CLAUDE_JOB_DIR/tmp" \
python3 -m unittest discover -s scripts/tests -p test_sppc_context.py -v
```

构建目录原先使用 `--disable-tests`，需保留原架构/安装路径并启用测试，构建
`sppc.dll`、`slc.dll`、`dlls/sppc/tests/x86_64-windows/sppc_test.exe`。

**要使用构建目录 loader 测试未部署的 Wine 内建 DLL。** 将新内建 DLL 复制到
临时 system32 不保证被选用：已安装 loader 仍可能先选择 dist-cx 的旧内建版本。
当前回归使用新 prefix、启动前私有 Z: 映射、构建 loader 和对应 wineserver，
不修改用户 Office；API 断言、实际 reg.exe 注册与幂等均在这个隔离环境中验证。

## ActivePlugins 服务信息探针

`service-info.c` 是单独的五参数 API 探针，不改变原四 API 矩阵或旧 EXE 的含义。
它不消费权利、不查询 SecureStoreId/机器身份、不加载返回的插件，也不根据路径
访问文件或网络共享。服务查询本身仍可能启动服务或产生缓存/事件；原生运行需
处于获准的诊断范围内，每个异常参数用例使用独立进程。

```sh
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -municode \
  -o "$CLAUDE_JOB_DIR/tmp/service-info.exe" tools/sppcprobe/service-info.c -lversion
```

```text
service-info --selftest
service-info sppc|slc <case> --query
```

- `--selftest`：只验证合成 MULTI_SZ 的字节边界、双 NUL、尾部零填充、数量上限；
  不加载 SLC/SPPC，不把合成路径注册为插件。
- 服务查询：`active`、`active-no-type`、`missing`。
- 参数/句柄对照：`null-handle`、`invalid-handle`、`closed`、`null-name`、
  `null-size`、`null-data`。输出预置哨兵，只报告是否改变，不转储指针或未知数据。
- `ActivePlugins` 成功时先核验 MULTI_SZ 类型及完整边界，只打印插件文件名，不输出
  含用户目录的完整路径。`probe-failures=0` 表示成功取得诊断结果，不表示 API 成功、
  插件已加载或 Office 已获得许可。

微软公开 SDK 指定 `SLGetServiceInformation(HSLC, PCWSTR, SLDATATYPE*, UINT*, PBYTE*)`；
类型输出可省略，`ActivePlugins` 是以双 NUL 结束的完整 DLL 路径列表，长度单位为
字节，成功结果由 LocalFree 释放。不要将其当作 GUID/布尔值，也不要为消除 F076
而虚构非空列表。`0028` 已修正 Wine 的声明和 SLC 转发，但服务查询仍返回
`SL_E_VALUE_NOT_FOUND`；参数/句柄的完整原生语义和真实插件后端都还没有实现。

`scripts/tests/test_sppc_service_info.py` 在新 prefix 内重新编译探针，运行 14 个解析
用例、3 个命令行防误用用例，以及 sppc/slc 各两次服务查询。它核验无后端时两 DLL
返回同一失败、输出不变，SLC 不再因 stub 导出中断；这不是原生 ActivePlugins 的
成功返回对照。使用前文同一 opt-in 环境执行全部 `test_sppc_*.py` 即可包含该测试。

本轮新增后的实际结果为 34 项 Python 测试通过，含原有 115 条 Win32 断言及新的
探针检查，失败与跳过均为零。`0028` 只构建和隔离验证，当前 Office 运行目录仍为
已部署的 `0027` 组合，不能把新源码、新构建和运行 DLL 混为同一版本。

依据：[SDK slpublic.h](https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/slpublic.h)。

## ActivePlugins 原生结果（2026-09-17）

参考机 Windows 10.0.29648.1000，`service-info.exe`，自测 14/14、`probe-failures=0`：

```text
query hr=0 type=7 bytes=132 data-unchanged=0 data-null=0
plugin-count=2  →  sppwinob.dll, sppobjs.dll
```

`type=7` 是 `SL_DATA_MULTI_SZ`，132 字节即两条完全限定路径的 66 个 WCHAR，双 NUL
结尾，长度以字节计，缓冲区由调用方 `LocalFree`。`slc` 查询解析到 `SPPC.DLL`。

失败契约（此前标注"待对照"，现已补齐，`sppc` 与 `slc` 结果一致）：

| 用例 | HRESULT |
|---|---|
| `missing`（未知属性名） | `0xC004F012` |
| `null-handle` / `null-name` / `null-size` / `null-data` | `0x80070057` |
| `invalid-handle` / `closed` | `0xC0030005` |

每条失败路径都不改写调用方的输出；`active-no-type` 证明类型指针可以省略。

Wine 一侧返回本模块自己的路径（`plugin-count=1`、`basename=sppc.dll`、`bytes=60`），
原因见 `patches/office/0029-sppc-active-plugins-liveness.patch`：那两个 SPP 服务插件
在 Wine 里并不存在，不能报告它们的路径。查询成功只表示许可平台可用，**不表示已
授权、已激活或已签发许可证**。

## sku-info：ApplicationBitmap 对照（2026-09-17）

```sh
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
  -o "$CLAUDE_JOB_DIR/tmp/sku-info.exe" tools/sppcprobe/sku-info.c -lole32
```

```text
sku-info sppc|slc {sku-guid} bitmap|missing|null-handle|null-sku|null-size|null-data --query
```

属性名是白名单：只查 `ApplicationBitmap`（某个 SKU 覆盖哪些应用，属于安装自带的
产品定义）和一个确定不存在的名字。不调用 `SLConsumeRight`，不做权利评估，不安装、
不激活、不重置任何东西。只在成功且长度不超过 64 字节时打印内容，因此不会把大块
未知数据倒进日志。

参考机 Windows 10.0.29648.1000 的结果：

```text
query hr=0 type=set(1) size=set(22) data-unchanged=0
value-bytes=30007800300030003000310046003100420042000000    →  "0x0001F1BB"
```

`type=1` 是 `SL_DATA_SZ`——**字符串，不是 DWORD**。失败契约：未知属性名与未知 SKU
都是 `0xC004F012`，四个空指针参数都是 `0x80070057`，失败时输出一律不变。

能读到位图只表示该 SKU 已安装注册，**不表示已授权、已激活或已签发许可证**。
