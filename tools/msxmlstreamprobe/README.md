# streamload：IXMLDOMDocument::load 与 IPersistStreamInit::Load 在流读不出来时做什么

Word 打开“引用”选项卡时从一个空文档的流里加载 XML；Wine 的 msxml3 不看 `Read` 的返回值就用它留下的计数复制，
从栈上写出几 GB，Word 崩溃（修复：altars-up `9b72583d2b6`）。这个探针让同样的问题去问 Windows 的 msxml：对几种 `Read`
行为（先给数据再 S_FALSE、立刻失败、失败但计数清零、给了数据再失败、给了半截再失败、空 S_FALSE、空 S_OK、E_PENDING），
在 MSXML 3 与 6 里分别打印 `load` 的 HRESULT 与结果、parseError、每次 `Read` 的请求大小与 `pcbRead` 的进出值。
另外读一个空文档和一个非空文档自己的 `IStream`，并从另一个文档（`VT_UNKNOWN` 与 `VT_DISPATCH`）加载。

安全：某个模式会让 `*pcbRead` 保持不变而它进来时不为 0 时，探针自己先清零并标出 `entry_nonzero_zeroed`，
调用方不初始化也复制不出垃圾。不读写文件，不联网。

```
x86_64-w64-mingw32-gcc -O1 -Wall -Werror -o streamload.exe streamload.c -lole32 -loleaut32 -luuid
```

## 结论（Windows 11 29671，results/windows-29671.txt）

- 空文档自己的流：`Read` 返回 S_OK、读出 0 字节。
- 从空文档加载：`load` 返回 S_FALSE，parseError `0xC00CE558`（缺根元素）；`VT_DISPATCH` 与 `VT_UNKNOWN` 一样可以作源。
- 流读失败：`load` 返回 **S_FALSE**，parseError 是流的错误；`IPersistStreamInit::Load` 返回该错误本身。
- 空流（S_FALSE 或 S_OK 读出 0 字节）：同上，错误是 `0xC00CE558`。
- `E_PENDING`：Windows 当作“数据还没到”，`load` 成功返回、保留流的引用以后再读（异步）。

altars-up `70197f2e665` 起，除 `E_PENDING`、每次请求的块大小（Windows 4095/8184，Wine 4096）和中文消息文本长度外，
Wine 与 Windows 相同（results/wine-70197f2e665.txt）。
