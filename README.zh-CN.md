# wine-altars

**在 Linux 上运行 Microsoft 365，并且是真实登录。** 一套叠在上游 Wine 之上的补丁，加上围绕它的脚本与实测记录。
它能运行 Word、Excel 和 PowerPoint（Click-to-Run，64 位），并让你用自己的微软账号、自己的订阅登录——
登录流程和授权方式与 Windows 上完全相同。

[![CI](https://github.com/Altars3668/wine-altars/actions/workflows/ci.yml/badge.svg)](https://github.com/Altars3668/wine-altars/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Altars3668/wine-altars?include_prereleases)](https://github.com/Altars3668/wine-altars/releases)
[![License: LGPL-2.1+](https://img.shields.io/badge/license-LGPL--2.1%2B-blue)](LICENSE)

English: [README.md](README.md)

> **这里的“激活”指什么。** 你用自己的微软账号登录，Office 照 Windows 上一模一样的方式，对着你的订阅为自己授权。
> 本仓库没有破解、没有序列号生成器、没有 KMS 模拟，也不复制任何许可证。没有订阅，Office 在这里和在 Windows 上一样不会
> 给自己授权。微软不支持在 Wine 上运行 Office；这是一个独立项目。

## 现状

以 Windows 11（build 29671）为参照，在 Wine 11.19 加本补丁系列上实测：

| | |
|---|---|
| **可用** | Word、Excel、PowerPoint 能启动、编辑、保存、打印；在 Office 自己的窗口（WebView2）里登录微软账号，并对个人版 Microsoft 365 Family/Personal 订阅完成授权；Word 的开始页、功能区、对话框和弹出面板；在 Word 里嵌入 Excel 工作表并就地编辑；经 CUPS 打印（无双面器的打印机上的手动双面已实现，用捕获的打印输出验证过，尚未用真纸验证）；经 XIM 的中文输入；保存到 OneDrive |
| **已检查** | 脚本化功能普查：Word 34 项、Excel 72 项、PowerPoint 45 项操作通过（多种格式保存、导出 PDF 与 XPS、图表、加密保存、比较文档等），另有每个应用的“保存再打开”回归。每一项怎么测的见 [`docs/`](docs) |
| **未覆盖** | Outlook、OneNote、Access、Publisher、Teams 不在测试范围内。工作或学校（Entra ID）账号没有测过，只测过个人微软账号。就地更新 Office 没有测过，稳妥的做法是用新的前缀重装 |
| **已知毛病** | 非正常退出后，Office 下次启动会提示进入安全模式（选“否”）。在没有 GPU 的 Xvfb 上 WebView2 的内容渲染成全黑，那是测试显示环境的问题，不是 Office 的 |

这是研究性质的软件：很多地方做对了，仍然会让你意外。[`docs/`](docs) 下的笔记记录了测过什么、哪些结论后来被推翻、现在停在哪里。

## 快速开始

你需要一份 Microsoft 365 订阅（脚本默认安装 *Family/Personal* 产品）、带 X11 或 Xwayland 且 GL 驱动正常的 x86-64 Linux，
以及大约 10 GB 磁盘。发布版在 Ubuntu 24.04 上构建，因此需要 glibc 2.39 或更新。

```sh
# 1. Wine：取最新发布版（也可以自己构建：docs/building.md）
curl -LO https://github.com/Altars3668/wine-altars/releases/latest/download/wine-altars-linux-x86_64.tar.xz
sudo tar -C /opt -xf wine-altars-linux-x86_64.tar.xz
export PATH=/opt/wine-altars/bin:$PATH

# 2. 本仓库的脚本
git clone https://github.com/Altars3668/wine-altars.git && cd wine-altars

# 3. 前缀、WebView2 运行时、Office、登录开关。大部分时间在等微软的 CDN。
export WINEPREFIX=$HOME/.wine-office
scripts/office-setup.sh all

# 4. 启动 Word，它要你登录时，用自己的微软账号登录
scripts/office-setup.sh launch word
```

第 3 步是四条命令（`prefix`、`webview2`、`install`、`signin`），也可以一条一条跑；`scripts/office-setup.sh status`
会告诉你哪些已经完成。运行 `install` 即表示你接受微软的许可条款——脚本把 `AcceptEULA` 交给了微软自己的安装程序。

**登录。** 第一次启动时 Word 会显示“登录以设置 Office”；同一个入口也在 *文件 → 账户 → 登录*。**用鼠标点它**——Office 的按钮
对无障碍接口的“按下”请求只回报成功、什么也不做，真实点击才有效。随后会打开一个标题为 *Sign in*（登录）的窗口（一个 WebView2
浏览器），照常输入账号、密码和二次验证，Word 就带着授权起来了。*文件 → 账户* 里订阅显示为已激活，和 Windows 上一样。结果存在前缀里，
重启后仍然有效。

逐步说明——每一步做了什么、出问题时怎么办——见 [`docs/getting-started.zh-CN.md`](docs/getting-started.zh-CN.md)。

### 为什么登录需要“帮一把”

从原版 Wine 到 Office 自带的登录窗之间隔着三件事。上面的步骤全部处理了它们，没有一件碰授权本身：

1. Office 用功能开关决定使用它的两套登录实现中的哪一套，而这里两个开关的默认值都偏向错误的一边。默认情况下 Office 退回到内置
   浏览器引擎，微软的登录页（脚本构建的单页应用）被渲染成一个空的 `<div>`，窗口一片空白或者一闪就没。`signin` 在前缀里翻转这两个开关
   （`scripts/enable-native-signin.sh`）：WebView2 开关打开，“经账户 broker 登录”开关关闭。
2. 前缀里必须有 WebView2 运行时（`webview2`）。微软的安装器外壳是 32 位程序，所以这个 Wine 带着它的 32 位一半一起构建。
3. Wine 的账户 broker（`windows.security.authentication.onlineid`）以前会*声称*自己能服务微软账号，随后又拿不出票据，
   于是 Office 把之后每一个失败都当成 broker 故障上报，不去打开自己的登录窗。现在它对服务不了的账号直接让位
   （[`0024-onlineid-stand-aside-…`](patches/altars-up/0024-onlineid-stand-aside-when-this-broker-holds-no-token.patch)），Office 于是回到自己的登录。

从另一台机器拷来的身份缓存只会更糟：它们被那台机器的 DPAPI 密钥封住，这里谁也打不开，Office 会说“无法访问你的账户”，且不再给出登录表单。
请从空的用户配置开始。

## 仓库结构

| 路径 | 内容 |
|---|---|
| [`patches/altars-up/`](patches/altars-up) | **代码本体**：叠在上游 Wine `wine-11.19` 之上的 578 个补丁，按顺序，可直接 `git am`；`BASE` 写明上游提交 |
| [`scripts/`](scripts) | `office-setup.sh`（快速开始）、`build-from-series.sh`（取源码、打补丁、构建）、前缀与测量辅助脚本、PE/PDB/WinRT 检视脚本 |
| [`tools/`](tools) | 约 190 个小探针。每个都向 Windows 和 Wine 问同一个问题，并把两份答案并排保存（`*.win.txt`、`*.wine.txt`）；Wine 补丁引用它们 |
| [`docs/`](docs) | 实验笔记，以及单项修复背后的记录——[索引](docs/README.md) |
| [`patches/office`](patches/office)、[`patches/mstsc`](patches/mstsc)、[`patches/mesa`](patches/mesa)、[`patches/ported`](patches/ported) | 按发现顺序保存、附带推理的补丁；继承来的 RDP 客户端补丁系列；一个 Mesa 修复；移植自其他树的工作——见 [`patches/README.md`](patches/README.md) |
| [`.github/workflows/`](.github/workflows) | CI 与发布构建 |

这个项目的要点是方法：笔记里的每一条结论，都是一次寄存器读取、一次内存读取，或者线路上的字节，在 Windows 和 Wine 上各测一遍，
测它的探针就在仓库里。[`docs/method.md`](docs/method.md) 列出了所用的仪器和已经踩过的坑。

## 发布与 CI

* **推送标签 `v*`**（例如 `v11.19-altars.1`）：GitHub Actions 用 [`scripts/build-from-series.sh`](scripts/build-from-series.sh) 从上游和补丁系列
  构建 Wine，做冒烟测试（`wineboot`、64 位与 32 位 `cmd`），然后发布一个带构建产物、补丁系列和校验和的 release。
* **推送到 `main`** 且改动了补丁系列或构建配方：滚动的 **`nightly`** 预发布会重新构建。
* **每次推送和拉取请求**：shell 与 Python 脚本能解析、`shellcheck` 无错误、单元测试通过，补丁系列仍能应用到固定的上游提交上。

本地构建用的是同一份配方：[`docs/building.md`](docs/building.md)。

## 报告问题

Wine 日志和 Office 自己的诊断文件（`%LOCALAPPDATA%\Temp\Diagnostics`）里含有账号标识、租户 ID、设备 ID，某些模式下还有请求 URL。
**附到 issue 之前请先阅读并涂掉敏感信息**，永远不要贴令牌、cookie 或许可证文件。最有用的报告包含 `scripts/office-setup.sh status` 的输出、
Wine 版本、你做了什么，以及第一条错误。

## 许可与商标

这里的一切采用 GNU LGPL 2.1 或更高版本，即 Wine 所用的许可（[`LICENSE`](LICENSE)）。本仓库不含任何微软软件：Office 和 WebView2 运行时由微软自己的安装程序
从微软下载到你的机器上，在你自己的许可下使用。

Microsoft、Windows、Office、Word、Excel、PowerPoint、Microsoft 365、OneDrive、Edge 是微软公司集团的商标。Wine 是其各自权利人的商标。本项目与微软或 WineHQ
项目没有隶属、背书或支持关系。
