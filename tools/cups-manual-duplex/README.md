# 系统打印（CUPS）里的手动双面

让没有双面器的打印机在 Linux 原生程序里也能选“双面打印（长边/短边翻转）”，打出来和
Wine 里的 Word 一样；而且**一台打印机只显示为一台**：就在它自己的队列上做，不另建队列。

例子是 HP LaserJet Tank MFP 1005w。它的 IPP 属性（`ipptool get-printer-attributes` 实测）：

- `sides-supported = one-sided`：没有双面器；
- `media-source-supported = auto,manual,tray-1`：有手动进纸；
- `document-format-supported` 里没有 PDF，`overrides-supported` 不存在，所以不能逐页换纸源，
  只能分成两个作业；
- 同时以 `_ipp._tcp` 和 `_ipps._tcp` 广播；IPPS 在 631 端口，TLS 可用；
- Validate-Job 带 `sides=two-sided-long-edge` 时回 `successful-ok-ignored-or-substituted-attributes`，
  并把 `sides` 列为不支持（只校验，不建作业，2026-10-06 实测）。

## 原理

Windows 的 HP 驱动和 Wine 的 wineps.drv（`dlls/wineps.drv/printproc.c`，另见
`docs/office365-under-wine.md`“手动双面”一节）都**不模拟暂停**：

- 驱动只负责重排页序和方向：奇数面一遍，偶数面一遍；
- 第二遍带 IPP `media-source=manual` 提交，由**打印机自己**亮绿箭头，等人放回纸叠、按键。

这里把同一套机制放进打印机自己的 CUPS 队列：

| | Wine（打到没有 Duplex 的队列） | 本方案（同一个队列，PPD 加了 Duplex） |
|---|---|---|
| 何时自己做双面 | PPD 没有 `*Duplex` 时（`manual_duplex_wanted()`） | 作业带 `sides=two-sided-*` 或 `Duplex=DuplexNoTumble/DuplexTumble` |
| 第一遍 | `page-set=odd` | 本作业：预过滤器只放行奇数面 |
| 第二遍 | `page-set=even outputorder=reverse InputSlot=Manual` | 同一队列上的新作业，选项相同，优先级 100 |
| 长边装订 | 在 PostScript 里把每面转 180° | `qpdf --rotate=+180`，在排版之后、光栅化之前转，横向页也成立 |
| 奇数个面 | pdftopdf 给偶数半补一张空白页，倒序后排在最前 | 相同 |
| 份数 | `copies` 两遍都给 | pdftopdf 先做好份数：双面时强制逐份，每份补到偶数面，再拆两遍 |
| 只有一面 | 仍发一遍空白页、走手动进纸 | 直接单面打印 |
| 谁把第二遍变成手动进纸 | ipp backend 把 `InputSlot=Manual` 映射为 `media-col.media-source=manual` | 相同 |

**纸叠平移放回。** 出纸面朝下；把整叠拿出来，不翻面、不调头，原样放回纸盒；纸盒从最上面取纸。由此：

- 最后印的那张在最上面、最先进纸，所以第二遍要**倒序**；奇数个面时最上面那张没有反面，
  就由补上的空白页去配。
- 先出机的那条边放回后变成最后进纸，所以不转的反面与正面是“短边翻”对齐：
  **长边装订要转 180°，短边装订不转**。

5 页、长边装订时：第一遍依次打印 1、3、5；第二遍依次打印空白、4、2，内容都转 180°。

```
任何程序 ──► 打印机自己的队列（PPD = 原 PPD + Duplex 选项 + 预过滤器）
               pdftopdf ──► manualduplex（预过滤器）──► gstoraster ──► URF ──► dnssd/ipp backend ──► 打印机
                              │ 单面作业：原样放行
                              │ 双面作业：放行奇数面（第一遍）；
                              └─ 再以 lp -q 100 往同一队列提交全部面（第二遍，带标记，原样放行）
```

预过滤器由 PPD 的 `*cupsPreFilter: "application/vnd.cups-pdf 0 manualduplex"` 指定：cupsd 把它插在
消费 `application/vnd.cups-pdf` 的过滤器之前（`scheduler/job.c`），也就是 pdftopdf 之后、gstoraster 之前。
PDF 输入的链是 pdftopdf → gstoraster，PostScript（Wine）是 gstopdf → pdftopdf → gstoraster，两条都经过它。
它看到的是 pdftopdf 排好的“纸面”：页码范围、n-up、缩放、方向、份数都已做完。

作业自己的 `sides`/`Duplex` 仍会到 backend：ipp backend 先发 Validate-Job，打印机答“不支持双面”，
backend 就把 `sides` 改成 `one-sided`（`backend/ipp.c`）。Duplex 选项的各个取值不带 PostScript 代码，
所以光栅页头也是单面（测试逐页检查 URF 页头的 duplex 字节为 1）。

第二遍以最高优先级排队，所以即使后面还有别的作业在等，反面也紧跟在正面之后。

## 为什么只剩一台

以前这台打印机在打印对话框里出现三次：手工建的 `ipp://<IP>` 队列、cups-browsed 自动建的
`HP_LaserJet_Tank_MFP_1005w_FBDBF2`，以及 libcups 从网络上发现的同名打印机（被 Edge 按名字并成一条）。

- **cups-browsed** 只在“没有本地队列是这台打印机”时自建队列。它的判断（`local_printer_is_same_device`）
  只认两种设备 URI：`dnssd://<服务名>._ipp(s)._tcp.<域>/…`，或主机名与广播一致的 `ipp(s)://`。
  `ipp://<IP 地址>` 两样都不是，所以它另建了一个。
- **libcups 的 `cupsEnumDests`**（Chromium/Edge 用它列打印机）只在本地队列的设备 URI 是 `dnssd://` 时，
  才把网上发现的同一台打印机当成已列出（`cups/dest.c`）；而且打印机同时广播 IPPS 时，它会把设备改记成
  IPPS 并重新列出，所以本地队列要写 `._ipps._tcp`。

于是 `install.sh` 把队列的设备 URI 改成打印机的 `dnssd://…._ipps._tcp.local/?uuid=…`（按打印机的
`printer-uuid` 在 `lpinfo` 的结果里找，确认打印机广播 IPPS），删掉 cups-browsed 的那个队列（它不会再建），
以前版本另建的前端队列和 backend 也一并删掉。

## 文件

| 文件 | 作用 |
|---|---|
| `manualduplex` | CUPS 预过滤器（Python 标准库 + `qpdf` + `lp`），装在 `/usr/lib/cups/filter/` |
| `mkppd.py` | 由队列原来的 PPD 生成新 PPD：加 Duplex（None/DuplexNoTumble/DuplexTumble，不带代码）和 `*cupsPreFilter`，其余不变 |
| `install.sh` / `uninstall.sh` | 就地改造与回退 |
| `test/run-tests.sh` | 不费纸的端到端测试（见下） |
| `test/mkpdf.py`、`test/urfsheets.py`、`test/oracle.py` | 测试文档、URF 解码（页码、朝向、纸源、duplex 字节）、按单面结果推算双面应有结果 |
| `test/test_filter.py` | 选项解析等单元检查 |

## 安装与回退

```sh
tools/cups-manual-duplex/install.sh                 # 默认：系统默认队列，就地改造
~/.local/state/cups-manual-duplex/uninstall.sh      # 还原原来的 PPD 和设备 URI
```

`install.sh` 改的东西：

- 装 `/usr/lib/cups/filter/manualduplex`（需 sudo；cupsd 只运行属 root、不可被他人写的过滤器，以 lp 用户、
  AppArmor 的 `cupsd//third_party` 子配置运行）；
- 队列的 PPD 和设备 URI（lpadmin 组即可）；队列名、默认选项、描述、位置不变，仍是系统默认；
- 删掉 cups-browsed 为同一台打印机建的队列，以及以前版本的前端队列和 backend。

原 PPD、原设备 URI 和 `lpoptions` 存在 `~/.local/state/cups-manual-duplex/`，回退脚本也复制到那里。
重跑 `install.sh` 总是从存下的原 PPD 重新生成。如果队列的 PPD 被重建过（例如重跑
`scripts/printer-use-ipp.sh`），重跑 `install.sh` 即可。

## 用法

- **Edge**（Edge Dev 152 实测：临时配置文件、测试显示、队列暂时拒收作业，没有出纸）：打印机列表按**队列名**
  显示，截到约 25 个字符；选这台打印机后面板有“双面打印”，可选“单面打印”“双面打印 长边翻转”
  “双面打印 短边翻转”。依据是 CUPS 为队列公布的 `sides-supported = one-sided,two-sided-long-edge,two-sided-short-edge`。
- **GTK**：在“页面设置”里的“双面”中选择；**LibreOffice**：在打印机属性里设置 Duplex。这两项按 PPD 推断，
  未在界面上实测。
- **Word（Wine）**：见下一节。
- **在打印机上**：先打印奇数面。第二个作业到达后，打印机亮起手动进纸提示（绿箭头）。
  把出纸盒里的整叠纸拿起来，不翻面、不调头，原样平移放回纸盒，然后按键。

队列里会出现两个属于你的作业：原作业（正面）和“标题 (2/2)”（反面）。要取消就两个都取消；
只取消第一个时，反面那个仍会打印。

## 与 Wine / Word 的关系

- Wine 把 CUPS 队列同步进前缀时只取本地队列（`cupsGetDests` 不做网络发现），所以 Word 里也只有这一台。
- 队列的 PPD 有 Duplex，wineps 因而认为打印机有双面器，`manual_duplex_wanted()` 返回假，只发一个带
  `sides=two-sided-*` 的作业，由这里的预过滤器拆成两遍。因此**不会出现两层都做手动双面**，也不会丢掉双面。
  测试 `wine-queue` 用 Wine 的票据格式验证过这一点，`wine-own` 验证了 Wine 自己打两遍与本方案逐面相同。
- Word 自己的“手动双面打印”会发出两个单面作业；预过滤器收到单面作业时原样放行。

## 测试（不费纸，不碰真打印机）

```sh
tools/cups-manual-duplex/test/run-tests.sh            # 需要已安装的过滤器
python3 -I tools/cups-manual-duplex/test/test_filter.py
```

`run-tests.sh` 会依次完成以下步骤：

1. 起一个只在本机的假打印机：`ippeveprinter -r off -P <原 PPD>`。它是单面打印机，纸源为 auto/manual/tray-1，
   不做 DNS-SD 广播。真队列已改造时，原 PPD 取自 `~/.local/state/cups-manual-duplex/`。
2. 建两个指向假打印机的临时队列：`mdtest-plain` 用原 PPD（参照），`mdtest-queue` 用 `mkppd.py` 生成的 PPD。
3. 打印测试文档：第 N 页顶边从左角起有 N 个实心方块。
4. 解码假打印机收到的 URF，逐面读出页码、朝向（第一个方块落在哪个角）、光栅页头里的 MediaPosition 和
   duplex 字节，再查假打印机记录的作业 `media-source`。
5. 结束后删除临时队列，结束假打印机。

覆盖的情形：

- 单面；
- 长边、短边，5 页和 4 页，以及经 PPD 选项 `Duplex=` 提交；
- `copies=2` 逐份与不逐份（后者也逐份，见上表）；
- 只有 1 页；
- 横向页；
- 中文标题；
- 指定纸盒（第一遍 tray-1，第二遍 manual）；
- 两个双面作业前后提交：顺序是第一个的正、反面，再第二个的正、反面；
- n-up、page-ranges、orientation-requested、fit-to-page，这几项与同样选项的单面结果比对；
- Wine 自己打两遍，与 Wine 发一个带 sides 的作业；
- 队列的 `sides-supported` 与作业属主。

## 限制

- 规则依赖这台打印机的走纸方式：出纸面朝下、纸盒从顶部取纸、纸叠平移放回。走纸方式不同的打印机，
  页序和旋转规则也不同。
- 打印机必须有手动进纸提示（PPD 里有 `InputSlot Manual`）。没有的话无法像 Wine 那样弹对话框，
  `mkppd.py` 会拒绝。
- 多文件作业中，cupsd 对每个文件各运行一次过滤链，所以每个文件各自打两遍，各自手动进纸一次。
- 不经 pdftopdf 的作业（raw，或直接提交的 URF/PWG 光栅）原样单面打印。
- 作业自己要求 `outputorder=reverse` 时，pdftopdf 先倒了序，奇偶面会拆错；常见程序不发这个选项。
