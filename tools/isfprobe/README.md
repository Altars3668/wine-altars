# Ink Serialized Format：`isfprobe.exe`

Office 发现墨迹平台时，会先把一段 ISF 载入一个新的 InkDisp（`IInkDisp::Load`），之后才建别的墨迹对象；Wine 的
inkobj 原先不实现 `Save`/`Load`。探针用已知的点建两笔（X、Y 与 X、Y、NormalPressure），给第二笔设非默认的绘制属性，
在笔画、绘制属性与墨迹上各加扩展属性（VT_I4、VT_BSTR、VT_ARRAY|VT_UI1、VT_R8），然后：

- 以每种持久化格式（ISF、Base64 ISF、GIF、Base64 GIF）和每种压缩模式保存，打印字节（ISF 全部；GIF 逐块列出：逻辑屏幕尺寸、各扩展块的标签与数据大小、图像描述符，以及注释扩展里是不是同一模式保存的 ISF）；
- 再载入回来，打印笔画、包数据、度量、绘制属性与扩展属性；
- 打印 Load 对空数据、`{0,0}`、版本 1、截断的流、去掉头或末字节的 base64、非数组 VARIANT、已有笔画的墨迹的回答，
  以及空墨迹保存出的字节和 Save 的参数错误。

Windows 保存出的 ISF 字节就是 Wine 解码器的测试向量。`results/isfprobe.wine.txt`：实现之前的 altars-up（全是 E_NOTIMPL）。

构建：`x86_64-w64-mingw32-gcc -O1 -Wall -o tools/isfprobe/isfprobe.exe tools/isfprobe/isfprobe.c -lole32 -loleaut32 -luuid -luser32`。

`results/isfprobe.win.txt`（winref，第 14 批）：Windows 不收 CreateStroke 的包描述，属性就加在第一笔上；默认与“最大压缩”存出
160 字节，“不压缩”172 字节，这两份就是 `dlls/inkobj/tests` 的 `test_windows_isf` 的样例。Base64 两种格式存出的是字符串
（`base64:`、编码、算进长度的结尾 null），Load 直接收字符串；流里先有一个持久化格式标签（base64 为 0x10000）。
往有笔画的墨迹里 Load 是 E_INVALIDARG。`results/isfprobe.wine.txt`：altars-up `9b92b002e77`。
