# jsonprobe：Windows.Data.Json 在文档没说的地方怎么做

Office 的升级计划对话框（OSF 的 WebViewDialog）用 `JsonObject` 传参数；Wine 的实现在文档没写的地方都得选一个答案。
第一次在 Windows 上跑 Wine 的一致性测试就发现好几处选错了（成员顺序、TryParse 失败时的输出……），还因为一个 Windows
不检查的 NULL 输出指针让测试进程崩了。这个探针把悬而未决的问题直接问一遍，**不传任何未经验证的 NULL 输出指针**，
可以放心在 Windows 上跑，输出与 Wine 逐行 diff。

```
jsonprobe          （服务会话即可：scripts/winbatch.sh tools/jsonprobe/jsonprobe.exe -- jsonprobe.exe）
```

各段：

| 段 | 问什么 |
|---|---|
| order | Parse、SetNamedValue、替换、删除后再加、重复键、多键之后，Stringify 与迭代的成员顺序 |
| failed parses | Parse / TryParse 失败时输出参数的状态与内容 |
| errors | 缺名、类型不符、越界、迭代器越过末尾或对象改动后的 HRESULT |
| identity | 取回的值是不是放进去的那个对象 |
| formatting | 数字、字符串的 Stringify 写法与 Parse 读法 |
| limits | 数组/对象的嵌套深度：Parse 与 Stringify 各自的上限 |
| statuses | `JsonError.GetJsonStatus` 的映射 |
| split | `IMapView.Split` 对各大小视图的结果 |
| more | 二分出 Parse 的深度上限、科学记数阈值、各种 NaN、下溢、哪些改动使视图/迭代器失效 |

## 结论（Windows 11 29671，见 results/windows-29671.txt）

- 对象按**加入顺序**保存与写出；替换保留原位，删除后再加排到最后，重复键取后值、占前位。迭代是哈希表顺序（Wine 不复刻，用加入顺序）。
- `IMapView.Split`：16 个成员以内不拆（两半都 NULL），更多时第一半固定 16 个。
- 任何改动（含替换成同一个值；不含删除不存在的键，后者返回 S_OK）使之前的视图、迭代器返回 `E_CHANGED_STATE`；
  迭代器走到末尾后再 `MoveNext` 是 `E_BOUNDS`。数组同样。
- TryParse 失败仍给出值：空对象 `{}`、空数组 `[]`、null；Parse 失败不碰输出参数。数组对 NULL 输出指针返回 `E_INVALIDARG`，
  对象与值返回 `E_POINTER`。
- 数字：`%.15G` 能读回同一个数就用它，否则 `%.17G`，指数去前导零（`1E+21`、`1E-5`、`0.33333333333333331`、`-0`）；
  NaN/Inf 是旧 CRT 写法 `1.#QNAN`、`-1.#IND`、`1.#SNAN`、`1.#INF`。字符串的控制字符写成大写 `\u001F`。
- 太大或下溢到 0 的数字是 `WEB_E_INVALID_JSON_NUMBER`（非零的次正规数可以）。
- Parse：任何值（含标量）嵌套深度不能超过 512；Stringify：数组/对象 1024 层。

`results/wine-132f61412fa.txt` 是 altars-up `132f61412fa` 的输出：除迭代顺序外与 Windows 逐行相同。
