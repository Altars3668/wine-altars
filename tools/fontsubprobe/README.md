# fontsub 子集探针

Office 嵌入字体时调用 `fontsub.dll` 的 `CreateFontPackage` 取子集：Word 导出 PDF 用 `TTFCFP_FLAGS_SUBSET | TTFCFP_FLAGS_GLYPHLIST`（字形列表）、平台 3、编码 0xFFFF；保存时嵌入字体用 `TTFCFP_FLAGS_SUBSET`（字符列表）。这里的工具先量 Windows 怎么做，再把同一批字体交给 Windows 的 fontsub 与 Wine 的 fontsub 各做一遍、逐表比较。规则与结论见 `docs/office365-under-wine.md` 的“嵌入字体的子集”一节。

    scripts/build-probe.sh tools/fontsubprobe/fontsubprobe.c tools/fontsubprobe/fontsubprobe.exe
    scripts/build-probe.sh tools/fontsubprobe/subsetfile.c tools/fontsubprobe/subsetfile.exe shell32
    scripts/build-probe.sh tools/fontsubprobe/fontcmp.c tools/fontsubprobe/fontcmp.exe      # fontdiff、namedump、eblcdump、os2info 同样

## fontsubprobe

`fontsubprobe [字体文件=字体名...]`（缺省 Arial、Times New Roman、宋体的 TTC 与等线）对每个字体以各种方式调 `CreateFontPackage`：Office 那样的字形列表、字符列表、不带子集标志、SUBSET1 加 DELTA 再 `MergeFontPackage`、压缩标志、超出字形数的字形、空列表、NULL 列表、TTC 的面，以及每个参数出错（各在子进程里跑，Windows 对 NULL realloc 会崩溃）。每个包打印返回值、大小、各表长度、maxp 字形数、hhea 长度量数、有轮廓的字形、cmap 各子表及保留字符映射到的字形、post 版本、OS/2 字符范围，并让 GDI 装载子集、核对字符仍映射到原字形。`fontsubprobe.win.txt` 是 Windows 自带字体的结果，`fontsubprobe-open.win.txt` 是 Liberation Sans 与 DejaVu Sans（传入文件时字体名写在 `=` 之后）。

## 逐表对照

- `subsetfile <字体> <输出> <面> <文本>|@<文件>|- [glyphs]`：把子集写成文件。不带 `glyphs` 时文本（或 UTF-16 文件的内容）是字符列表；带 `glyphs` 时文本是十进制字形号，文件里是 16 位字形号；`-` 表示 NULL 列表、个数 3。TTC 自动加 `TTFCFP_FLAGS_TTC`。设了环境变量 `FONTSUB_DLL` 时从那个 DLL 取 `CreateFontPackage`。
- `fontcmp <a> <b>`：哪些表不同、第一个不同的字节，以及两边表的物理顺序。
- `fontdiff <原字体> <面> <a> <b>`：对不同的表给细节：OS/2 字符范围、LTSH、hdmx、hmtx/vmtx 的长度量数与各字形度量、cmap 子表布局、loca 格式与字形长度、name 里位置不同的记录。
- `namedump <字体> <面>`：name 表记录按字符串在存储区的位置排列。
- `eblcdump <字体> <面> <字号或 -1> <每个字号的子表数>`：EBLC 各字号的索引子表格式、字形范围与有位图的字形。
- `os2info <原字体> <面> <子集>`：两者的 OS/2 字符范围与各 cmap 子表实际映射的范围。

`compare.cmd` 在 Windows 上把 48 组输入各跑两遍：一遍用系统的 `fontsub.dll`，一遍用改名为 `wfontsub.dll` 的 Wine 的 x86_64 `fontsub.dll`（Wine 的 PE 版 fontsub 只导入 kernel32、ntdll 与 ucrtbase，Windows 上能直接加载），再用 `fontcmp` 比较，有差异时写 `detail-*.txt`、`names-*.txt`。运行前把这些放进 `%TEMP%`：`subsetfile.exe`、`wfontsub.dll`、`fontcmp.exe`、`fontdiff.exe`、`namedump.exe`；UTF-16LE 文本 `mix.u16`（“Hello, PDF 123! 中文字体测试，微软雅黑。日本語のテキスト”）、`emoji.u16`（U+1F300–U+1F64F 与 U+1F680–U+1F6CF）、`cjk300/1500/2000/4000.u16`（从 U+4E00 起的前 N 个汉字）、`g2000.u16`（字形 1072–3071 再加 100 与 22080 的 16 位字形号）；以及自由字体 Amiri、Andika、DejaVu Sans、Gentium Plus Compact、Go、Lato、Liberation Sans、Ubuntu Medium、Ubuntu Mono、Font Awesome、IPA Gothic、AR PL UMing（`uming.ttc`）。Windows 自带字体直接从 `C:\Windows\Fonts` 读，其子集只在那台机器上比较、不拷回；自由字体的子集可以取回本地分析。`compare.win.txt` 是 2026-09-28 wine-src `1bf949b` 的结果：44 组所有表都与 Windows 相同，其中 12 组逐字节相同，其余只是表的物理顺序不同；另外 4 组是 name 里三个字符串的位置（DejaVu、Gentium、Segoe UI Emoji 两组）和 Gentium 的 Mac cmap（Windows 的缺陷，见文档）。
