# mftwebpprobe：Office 判断能否解码 WebP 的那个 MF 变换

Word 插入 WebP 前调 `MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_ALL, {Video, 7693e886-…})`——子类型就是 WIC WebP
解码器的 CLSID。找到了才用 WIC 解码；找不到，`AddPicture` 不报错却什么也不插（取形状属性时 E_FAIL，`InlineShapes.Count` 不变）。

    scripts/build-probe.sh tools/mftwebpprobe/mftwebpprobe.c tools/mftwebpprobe/mftwebpprobe.exe mfplat ole32 advapi32 uuid mfuuid

`results/mft.win.txt` 是 Windows 11 build 29671 的结果：唯一的匹配是 Store 的 WebP 图像扩展包（Microsoft.WebpImageExtension）登记的
“WebpImageExtension”，同步 MFT，输入 Video/WebP，输出 Video/P010；激活属性里是包名、包里的 `mswebp_store.dll` 与激活类
`WebpDecoder.CWebpWICDecoder`，没有 CLSID。激活出来的变换除 `GetAttributes` 外每个方法都返回 `E_NOTIMPL`——它只是个标记。

Wine 的对应物在 windowscodecs 里（wine-src `2330bd8`），以 `MFTRegister` 登记，方法的返回值与 Windows 相同。顺带查出 Wine 的
`MFTEnumEx` 给注册表里的变换的激活对象不带名字、只带过滤了的那一侧类型（`3adedf3` 修好；Windows 上的 H.264 解码器两者都有）。
