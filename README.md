# wine-altars

**简体中文** | [English](README.en.md)

面向 **Linux 上的 Microsoft 365 桌面应用与 Windows 客户端互操作**的 Wine 定制补丁集。当前公开构建线基于上游 **Wine 11.19**，维护重点是 Word、Excel、PowerPoint（64 位 Click-to-Run）的安装、渲染、编辑、打印，以及通过 Office 自身的 WebView2 窗口正常登录个人 Microsoft 365 订阅。

> **不是破解或许可证绕过。** 用户必须使用自己的 Microsoft 账号和有效订阅。本仓库不提供密钥生成、KMS 模拟或许可证复制，不包含 Microsoft 软件，也不声称获得 Microsoft / WineHQ 的官方支持。

[![CI](https://github.com/Altars3668/wine-altars/actions/workflows/ci.yml/badge.svg)](https://github.com/Altars3668/wine-altars/actions/workflows/ci.yml)

## 我的改造与特色

| 改造方向 | 具体内容与证据入口 |
| --- | --- |
| **Click-to-Run / App-V 运行链路** | 围绕 Microsoft 365 安装、服务、COM / WinRT 和应用启动处理兼容缺口；不把“窗口打开了”当作完整可用。见 [实验笔记](docs/office365-under-wine.md)。 |
| **Office 自身的正常登录** | 安装 WebView2 并选择有效的登录路径；Wine broker 无法提供账户票据时正确让位，避免空白或瞬间关闭的登录窗。见 [登录辅助脚本](scripts/enable-native-signin.sh)。 |
| **界面、焦点与窗口行为** | 修正 Office 弹出面板立即关闭、下拉缓慢、窗口形状残留、跨进程 owner、圆角与 References 崩溃等问题。见 [弹出窗口记录](docs/office-popup-focus-latency-20261006.md) 和 [窗口行为记录](docs/office-window-shape-owner-json-20261006.md)。 |
| **文档、OLE 与组件互操作** | 对照 Windows 处理嵌入 Excel、MSXML / 脚本 / JSON 等应用依赖的行为差异；源码补丁与对应探针一同保存。 |
| **Linux 桌面集成** | CUPS 打印、输入法与应用集成；手动双面打印已按捕获输出验证，不把它描述成已用真实纸张验收。 |
| **Windows / Wine 双边探针** | [tools/](tools/) 按问题保存探针、源码和两侧输出；目标是解释一个具体返回值、结构或可见行为，而不是凭猜测补 stub。 |
| **可重建的上游补丁系列** | [patches/altars-up/](patches/altars-up/) 固定上游基点和应用顺序，保留补丁来源，配套导出、构建和 CI 脚本。 |
| **分步安装与维护** | `office-setup.sh` 分离 prefix、WebView2、Office 安装和登录准备，并提供状态查询；避免把宿主环境及账户缓存打包给别人。 |

这套系列也包含上游移植和继承的工作，**不能把所有补丁都算作本人原创**。当前可应用系列、发现过程中的补丁与旧 CrossOver 线的记录各有明确用途，来源见 [patches/README.md](patches/README.md)。

## 已有证据与使用边界

项目文档记录了 Word、Excel、PowerPoint 的启动、编辑、保存、导出、打印、保存后重开、OLE 就地编辑，以及个人 Microsoft 365 Family / Personal 登录验证。它们是特定构建与环境的实测记录，**不代表本次 README 修改重新验收了这些应用，也不保证所有 Office 版本或机器都可用**。

- 未覆盖 Outlook、OneNote、Access、Publisher、Teams；未验证工作 / 学校的 Entra ID 账户。
- Office 就地更新未作为可靠路径验证，升级优先采用新的 prefix 并保留数据备份。
- WebView2 需要可用的图形驱动；无 GPU 的 Xvfb 可能显示黑色内容。
- 非正常退出后，Office 下次可能询问安全模式；这不等于登录或订阅状态损坏。
- 历史实验笔记保留了被后续测量推翻的结论；阅读时应看时间、后续修正和对应探针，不能把整份笔记当作当前操作步骤。

## 开始使用

**不想自己编译？** [Releases](https://github.com/Altars3668/wine-altars/releases) 页提供 Ubuntu 24.04 构建的预编译包 `wine-altars-linux-x86_64.tar.xz`（需要 glibc 2.39 或更新）和 `SHA256SUMS`：核对校验和，解压到 `/opt`（或任何目录），把其中的 `bin/` 加进 `PATH`，然后从下面设置 `WINEPREFIX` 的那一行继续。带 `v` 标签的是正式版；`nightly` 是随补丁系列滚动更新的预发布。

需要 x86-64 Linux、X11 / Xwayland、正常 GL 驱动、Wine 构建依赖，以及自己的合法 Microsoft 365 订阅。优先按 [构建说明](docs/building.md) 准备依赖，在用户目录构建，避免默认替换系统 Wine：

```sh
git clone https://github.com/Altars3668/wine-altars.git
cd wine-altars
# 先按 docs/building.md 安装构建依赖；限制并行度，避免链接过程耗尽内存
JOBS=2 PREFIX="$HOME/.local/opt/wine-altars" scripts/build-from-series.sh
export PATH="$HOME/.local/opt/wine-altars/bin:$PATH"

# 使用独立 prefix；不要让两种 Wine 构建同时打开同一 prefix
export WINEPREFIX="$HOME/.wine-office-altars"
scripts/office-setup.sh all
scripts/office-setup.sh status
scripts/office-setup.sh launch word
```

`all` 可以拆成 `prefix`、`webview2`、`install`、`signin`。运行 `install` 会将 `AcceptEULA` 交给 Microsoft 官方安装器，意味着接受相应许可条款；脚本不代替用户登录。打开 Word 后，在 **文件 → 账户 → 登录**中完成真实账号登录及二次验证。

不要复制其他机器的身份或许可证缓存：它们受该机器 DPAPI 密钥保护，不是这里的登录方案。prefix、诊断日志和身份缓存含敏感信息，不能上传到公开仓库。

完整中文操作与排错：[docs/getting-started.zh-CN.md](docs/getting-started.zh-CN.md)；英文：[docs/getting-started.md](docs/getting-started.md)。

## 源码与文件导航

| 路径 | 用途 |
| --- | --- |
| [patches/altars-up/BASE](patches/altars-up/BASE) | 当前上游版本、提交和源码镜像。 |
| [patches/altars-up/SERIES.tsv](patches/altars-up/SERIES.tsv) | 开发树 commit ID 与公开补丁文件的对应关系。 |
| [scripts/build-from-series.sh](scripts/build-from-series.sh) | 获取上游、应用系列、配置、编译与安装。 |
| [scripts/office-setup.sh](scripts/office-setup.sh) | prefix、运行时、Office 与启动管理。 |
| [docs/building.md](docs/building.md) | 构建依赖、变量、32 / 64 位组件及 prefix DLL 同步。 |
| [docs/README.md](docs/README.md) / [docs/method.md](docs/method.md) | 实验索引与 Windows 对照方法。 |
| [tools/](tools/) | 小型行为探针与已脱敏的参考结果。 |
| [.github/workflows/](.github/workflows/) | 校验与发布配方。 |

完整 Wine 源码并未 vendoring 进此仓库；构建器按 `BASE` 获取上游并应用补丁。旧笔记中的 `wine-src` / `altars-up` SHA 不是这个文档仓库的 commit，公开系列可通过 `SERIES.tsv` 定位。

## 发布与验证

- `ci.yml` 定义脚本语法、静态检查、单元测试及补丁系列应用检查；**配置了检查不等于每次运行都已通过**。
- `release.yml` 定义 `v*` 标签发布，以及补丁 / 构建配方变更后的滚动 `nightly` 预发布。
- 下载前先查看 [Releases](https://github.com/Altars3668/wine-altars/releases) 是否已有实际产物；目前不能假定 `latest/download` 一定存在，预发布也不一定出现在 `latest` 中。
- Release 产物包含构建、补丁系列和 `SHA256SUMS`；选择准确 tag 并核对校验。Ubuntu 24.04 构建产物要求相应 glibc 兼容性。
- `office-smoke.yml`（手动触发）在干净的 GitHub runner 上用 Release 里的 Wine 走完 `office-setup.sh all` 并启动 Word，把截图和日志存为产物。它停在 Office 自己的登录提示（[截图](docs/img/word-first-start.png)），不替用户登录；每次运行是否通过，以该工作流的运行记录为准。
- README 中的命令会构建程序或修改 prefix；阅读文档本身不意味着已完成安装、订阅登录或应用验收。

## 公开与私有历史

GitHub 使用已经匿名化的公开历史，Gitea 保留完整开发历史。两端 README 保持同样的说明，但 commit SHA 和部分实验记录 / 补丁元数据可能不同；**不把私有分支直接强推到公开仓库，也不为追求 SHA 一致撤销脱敏**。

## 许可证与问题反馈

本仓库采用 [GNU LGPL 2.1 或更新版本](LICENSE)，保留 Wine、移植补丁和其他组件的来源 / 授权声明。Office、WebView2 与订阅遵循 Microsoft 的独立条款；本仓库只提供从官方来源安装的辅助脚本。

反馈请附 Wine / 系列版本、复现步骤和已脱敏的首个错误。日志可能含账户、租户、设备标识和请求 URL；先阅读再提交，不上传 token、cookie、身份缓存或许可证文件。
