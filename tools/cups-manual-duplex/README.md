# 系统打印（CUPS）里的手动双面

让没有双面器的打印机在 Linux 原生程序里也能选“双面打印（长边/短边翻转）”，打出来和
Wine 里的 Word 一样。

例子是 HP LaserJet Tank MFP 1005w。它的 IPP 属性（`ipptool get-printer-attributes` 实测）：

- `sides-supported = one-sided`：没有双面器；
- `media-source-supported = auto,manual,tray-1`：有手动进纸；
- `document-format-supported` 里没有 PDF，`overrides-supported` 不存在，所以不能逐页换纸源，
  只能分成两个作业。

## 原理

Windows 的 HP 驱动和 Wine 的 wineps.drv（`dlls/wineps.drv/printproc.c`，另见
`docs/office365-under-wine.md`“手动双面”一节）都**不模拟暂停**：

- 驱动只负责重排页序和方向：奇数面一遍，偶数面一遍；
- 第二遍带 IPP `media-source=manual` 提交，由**打印机自己**亮绿箭头，等人放回纸叠、按键。

这里把同一套机制搬到 CUPS 层：

| | Wine（打到原队列） | 本方案（打到 `_Duplex` 队列） |
|---|---|---|
| 何时自己做双面 | PPD 没有 `*Duplex` 时（`manual_duplex_wanted()`） | 作业带 `sides=two-sided-*` 或 `Duplex=DuplexNoTumble/DuplexTumble` |
| 第一遍 | `page-set=odd` | 相同 |
| 第二遍 | `page-set=even outputorder=reverse InputSlot=Manual` | 相同 |
| 长边装订 | 在 PostScript 里把每面转 180° | `qpdf --rotate=+180`，在真队列排版之前转，横向页也成立 |
| 奇数个面 | 由 pdftopdf 给偶数半补一张空白页，倒序后排在最前 | 相同（同一个真队列） |
| 份数、逐份 | `copies`、`collate` 两遍都给 | 相同 |
| 只有一面 | 仍发一遍空白页、走手动进纸 | 直接单面打印 |
| 谁把第二遍变成手动进纸 | 真队列的 ipp backend 把 `InputSlot=Manual` 映射为 `media-col.media-source=manual` | 相同 |

**纸叠平移放回。** 出纸面朝下；把整叠拿出来，不翻面、不调头，原样放回纸盒；纸盒从最上面取纸。由此：

- 最后印的那张在最上面、最先进纸，所以第二遍要**倒序**；奇数个面时最上面那张没有反面，
  就由补上的空白页去配。
- 先出机的那条边放回后变成最后进纸，所以不转的反面与正面是“短边翻”对齐：
  **长边装订要转 180°，短边装订不转**。

5 页、长边装订时：第一遍依次打印 1、3、5；第二遍依次打印空白、4、2，内容都转 180°。

```
原生程序 ──► <队列>_Duplex（PPD = 真 PPD + Duplex 选项；过滤只跑 pdftopdf）
                 └─ backend manualduplex：单面作业原样转交；双面作业拆成上面两个作业
            ──► <队列>（不改动：pdftopdf → gstoraster → URF → ipp backend）──► 打印机
Word/Wine ──► 原队列：Wine 自己打两遍
          └─► _Duplex 队列：PPD 有 Duplex，Wine 只发一个带 sides 的作业，由这里拆
```

## 文件

| 文件 | 作用 |
|---|---|
| `manualduplex` | CUPS backend（Python 标准库 + `qpdf` + `lp`）。设备 URI：`manualduplex:/<真队列>[?slot=Manual]` |
| `mkppd.py` | 由真队列的 PPD 生成前端 PPD：加 Duplex（None/DuplexNoTumble/DuplexTumble），过滤换成 PDF 直通，`*cupsManualCopies: False` |
| `install.sh` / `uninstall.sh` | 安装与回退 |
| `test/run-tests.sh` | 不费纸的端到端测试（见下） |
| `test/mkpdf.py`、`test/urfsheets.py`、`test/oracle.py` | 测试文档、URF 解码（页码、朝向、纸源）、按单面结果推算双面应有结果 |
| `test/test_backend.py` | backend 选项解析等单元检查 |

## 安装与回退

```sh
tools/cups-manual-duplex/install.sh                 # 默认：系统默认队列 → <它>_Duplex
tools/cups-manual-duplex/install.sh --default       # 同时把新队列设为系统默认
~/.local/state/cups-manual-duplex/uninstall.sh --name HP_LaserJet_Tank_MFP_1005w_Duplex
```

`install.sh` 只新增两样东西：`/usr/lib/cups/backend/manualduplex`（需 sudo，0755，由 CUPS 以
lp 用户运行）和一个队列（lpadmin 组即可）。原队列不改；它的 PPD 和 `lpoptions` 仍会备份到
`~/.local/state/cups-manual-duplex/`，回退脚本也会复制到那里。

`uninstall.sh` 只删除设备为 `manualduplex:` 的队列；如果安装时用了 `--default`，会恢复原默认；
没有别的队列再用这个 backend 时，才删掉 backend。

如果真队列的 PPD 重建过（例如重跑 `scripts/printer-use-ipp.sh`），要重跑 `install.sh`，
让前端 PPD 跟上。

## 用法

- **Edge / Chromium**：打印时目标选“HP LaserJet Tank MFP 1005w (2-sided)”，打开“双面打印”，
  选择长边或短边翻转。Chromium 读 PPD 的 Duplex 选项，CUPS 也会公布
  `sides-supported = one-sided,two-sided-long-edge,two-sided-short-edge`。
- **GTK**：在“页面设置”里的“双面”中选择；**LibreOffice**：在打印机属性里设置 Duplex。
- **在打印机上**：先打印奇数面。第二个作业到达后，打印机亮起手动进纸提示（绿箭头）。
  把出纸盒里的整叠纸拿起来，不翻面、不调头，原样平移放回纸盒，然后按键。

前端作业会立刻完成；真队列上会出现属于你的两个作业，名为“标题 (1/2)”和“标题 (2/2)”。
要取消，就在真队列上取消这两个作业。

## 与 Wine / Word 的关系

- 原队列没有任何改动。Word 打到原队列时，仍由 Wine 自己打两遍，与以前完全相同。
- Wine 会把新队列也同步进前缀（`winspool` 的 `init_unix_printers` 会同步所有 CUPS 队列）。
  新队列的 PPD 有 Duplex，wineps 因而认为打印机有双面器，`manual_duplex_wanted()` 返回假，只发一个带
  `sides=two-sided-*` 的作业，由这里的 backend 拆成两遍。因此**不会出现两层都做手动双面**，也不会丢掉双面。
  测试 `wine-front` 用 Wine 的票据格式验证过这一点，`wine-own` 验证了原队列上的 Wine 两遍与本方案逐面相同。
- Word 自己的“手动双面打印”会发出两个单面作业；backend 收到单面作业时原样转交。
- Wine 每次同步都会把 Windows 默认打印机设成 CUPS 的默认队列。所以用 `--default` 后，Word 的默认打印机
  也会变成新队列；结果仍然正确，只是双面改由这里来拆。

## cups-browsed

cups-browsed 只管理自己建的队列，即 printers.conf 里带 `Option cups-browsed true` 的队列，
例如 `HP_LaserJet_Tank_MFP_1005w_FBDBF2`。用 lpadmin 手工建的队列不在其列：原队列和 `_Duplex`
都没有这个标记，所以不会被覆盖。CUPS 也不会自动重建手工队列的 PPD。

## 测试（不费纸，不碰真打印机）

```sh
tools/cups-manual-duplex/test/run-tests.sh            # 经 cupsd 调用已安装的 backend
tools/cups-manual-duplex/test/run-tests.sh --direct   # backend 未安装时：cupsfilter + 手动运行 backend
python3 -I tools/cups-manual-duplex/test/test_backend.py
```

`run-tests.sh` 会依次完成以下步骤：

1. 起一个只在本机的假打印机：`ippeveprinter -r off -P <真 PPD>`。它是单面打印机，纸源为 auto/manual/tray-1，不做 DNS-SD 广播。
2. 用真队列的 PPD 建临时队列 `mdtest-target` 指向假打印机，再建临时前端队列 `mdtest-front`。
3. 打印测试文档：第 N 页顶边从左角起有 N 个实心方块。
4. 解码假打印机收到的 URF，逐面读出页码、朝向（第一个方块落在哪个角）和光栅页头里的 MediaPosition，
   再查假打印机记录的作业 `media-source`。
5. 结束后删除临时队列，结束假打印机。

测试共覆盖 21 种情形：

- 单面；
- 长边、短边，5 页和 4 页，以及经 PPD 选项 `Duplex=` 提交；
- `copies=2` 逐份与不逐份；
- 只有 1 页；
- 横向页；
- 中文标题；
- 指定纸盒（第一遍 tray-1，第二遍 manual）；
- n-up、page-ranges、orientation-requested、fit-to-page，这几项与同样选项的单面结果比对；
- Wine 两遍与 Wine→前端队列；
- 前端队列的 `sides-supported` 与作业属主。

## 限制

- 规则依赖这台打印机的走纸方式：出纸面朝下、纸盒从顶部取纸、纸叠平移放回。走纸方式不同的打印机，
  页序和旋转规则也不同。
- 打印机必须有手动进纸提示（PPD 里有 `InputSlot Manual`）。没有的话，backend 无法像 Wine 那样弹对话框，
  `install.sh` 会拒绝安装。
- 多文件作业中，cupsd 对每个文件各运行一次 backend，所以每个文件各自打两遍，各自手动进纸一次。
- 非 PDF（raw）作业原样单面转交。
