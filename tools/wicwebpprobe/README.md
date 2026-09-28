# wicwebpprobe：WIC 的 WebP 解码器，以及各组件信息

Word 插入 WebP 图片走 WIC。探针列出系统的全部解码器，给出每个编解码器的四个 `DoesSupport*`、每个组件的签名状态与
SpecVersion、元数据读取器/写入器的三个标志，再解码 `samples/` 里的每个 WebP（有损、无损、各带不带透明、奇数尺寸、两种动画，
以及截断、损坏、截断的动画、只有文件头四个坏文件）：容器、帧数、尺寸、像素格式、分辨率、像素校验和、转 BGRA 后的校验和、元数据块与
查询读取器、缩略图/预览/调色板/颜色上下文、越界的帧、`QueryCapability`，以及 ANIM/ANMF 两个元数据读取器的登记信息。

    python3 tools/wicwebpprobe/samples/make-samples.py      # 需要带 WebP 的 Pillow；samples.h 由这些文件生成
    scripts/build-probe.sh tools/wicwebpprobe/wicwebpprobe.c tools/wicwebpprobe/wicwebpprobe.exe ole32 windowscodecs shell32 shlwapi uuid
    wicwebpprobe.exe            # 无参数时把内嵌样本写到临时目录逐个解码，用完删掉

`results/wic.win.txt` 是 Windows 11 build 29671 的结果。wine-src `2330bd8` 之后，WebP 部分与 Wine 的输出逐行相同，只有一处：
Windows 的查询读取器枚举 ANIM/ANMF 块时给 NULL 名字（再用它取值得 E_INVALIDARG），Wine 给 `/{guid=…}`，这个怪癖没照抄。

量到的：

- 这版 Windows 的 WIC 里有 “Microsoft Webp Decoder”（在 windowscodecs.dll 里）、HEIF、Raw、JPEG XL 解码器与 HEIF、JPEG XL 编码器；
  Wine 只补了 WebP。
- WebP 解码结果与 libwebp 逐字节相同：静态图等同 `WebPDecodeRGBA`，动画帧是 `WebPAnimDecoder` 合成后的整幅画布，非预乘 32bppRGBA。
- 错误码是 Media Foundation 的：坏的静态图解码时 `MF_E_INVALID_FILE_FORMAT`，越界帧 `MF_E_INVALIDINDEX`，截断的动画创建时
  `E_UNEXPECTED`。它背后是 WebP 图像扩展包（见 `tools/mftwebpprobe`）。
- 所有组件的签名状态都是 `WICComponentSigned`；`MatchesMimeType` 在 Windows 上也是 `E_NOTIMPL`。
