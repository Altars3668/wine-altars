# MLang 的 RFC 1766 名字表：`rfc1766probe.exe`

打印 `IMultiLanguage2::EnumRfc1766(0409)` 的整张表、注册表 `HKCR\MIME\Database\Rfc1766` 的副本，
以及 `Rfc1766ToLcidW`、`GetLcidFromRfc1766`、`LcidToRfc1766W`、`GetRfc1766FromLcid` 对一批名字和 LCID 的回答。

起因：MSXML 的 `ms:string-compare`、`ms:format-date` 只接受 `Rfc1766ToLcidW` 返回 S_OK 的语言名
（`tools/msxslfuncprobe`），Windows 上 `'de'` 可以而 `'de-DE'` 不行。

`results/rfc1766probe.win.txt`（Windows 11 build 29671，zh-CN）的要点：

- 名字不是从区域数据推出来的，而是一张 157 项的固定表，按枚举顺序取第一个匹配（不分大小写）：
  `es` 是 040a（0c0a 也叫 `es`），`sr` 是 0c1a，`no` 是 0414（`nb-no` 也是 0414），`pt` 是 0816、`pt-br` 是 0416，
  Kyrgyz 叫 `kz`，Sutu 叫 `sx`。
- 表里没有的名字：总长不超过 5 个字符且含 `-` 时，按第一个 `-` 之前的语言（2 或 3 个字母）查表，返回该项的 LCID 和
  **S_FALSE**（`de-de`→0407、`en-`→0009、`kok-x`→0457）；6 个字符以上一律 E_FAIL（`en-usa`、`kok-in`、`zh-hans`）；
  E_FAIL 时不写出参。
- `Rfc1766ToLcidA` 只看前 5 个字符：`en-usa` 当作 `en-us`（S_OK），`zh-hans` 当作 `zh-ha`（S_FALSE）。
- LCID→名字：表里有就用表里第一个名字（0c0a→`es`、081a→`sr`）；没有就是 ISO 639 语言加 ISO 3166 国家的小写
  （0007→`de-de`、0492→`ku-iq`、007f→`iv-iv`）；0、0400、0800、1000 取用户默认区域；无效 LCID 为 E_FAIL。
- 注册表副本有 122 项，值形如 `en-us;@%SystemRoot%\system32\mlang.dll,-4386`。**Wine 没有这份注册表**，
  也没有这些字符串资源，尚未补。

Wine（`d0e66a45375` 起）除注册表副本外逐行一致；mlang 的测试已按上述行为补充，Windows 上 0 失败。

构建：`scripts/build-probe.sh tools/rfc1766probe/rfc1766probe.c tools/rfc1766probe/rfc1766probe.exe ole32 oleaut32 mlang advapi32`。
