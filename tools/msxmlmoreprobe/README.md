# msxmlmore：msxml 文档还有哪些地方和 Windows 不一样

接着 `tools/msxmlstreamprobe`、`tools/msxmlimportprobe`，把剩下的未知点一次问完。不联网；流都是探针自己的对象
（堆上分配、不释放：msxml 可能在加载返回后还持有它）；methods 段在 %TEMP% 写一个小 XML 文件，用完删掉；valuespaces 段
让 msxml 去取 %TEMP% 下一个不存在的文件（看下载失败报什么）。
文本里 ASCII 以外的字符一律写成 `\uXXXX`。

```
msxmlmore [interfaces] [reasons] [pending] [blocks] [entities] [epilog] [validation] [methods] [spaces]
          [entityrefs] [valuespaces]                                               （服务会话即可）
```

| 段 | 问什么 |
|---|---|
| interfaces | 各文档类（DOMDocument、2.6、3.0、自由线程 3.0、6.0、自由线程 6.0）对 17 个接口的 QueryInterface |
| reasons | 14 种坏文档与 6 种流读失败：parseError 的代码、reason 原文、行、列、filepos、srcText |
| pending | 流回答 `E_PENDING`（一次、两次、五次、数据中间）时 load 的返回、readyState、何时再读、流的引用 |
| blocks | 每次 `Read` 请求多少字节（流每次最多给 0/1/7/100/4095/5000 字节时） |
| entities | DTD 内部实体的引用在 DOM 里的节点结构：先关掉 validateOnParse，再在声明了元素的 DTD 下校验着读 |
| epilog | 根元素之后能有什么，不能有的各报什么错、报在哪里；`<?xml` 出现在别处与大小写变体 |
| validation | 26 个文档：每种 DTD 校验错误在加载时（validateOnParse）的代码与位置，及不校验加载后 validate() 的结果 |
| methods | IMarshal、IProvideClassInfo、IOleCommandTarget、IServiceProvider、IPersistMoniker 各方法的回答 |
| spaces | 不校验时，各种内容模型（EMPTY、ANY、#PCDATA、元素、混合、未声明、无 DTD）下文档保留哪些空白 |
| entityrefs | 实体引用的更多细节：DTD 里实体节点的子节点、text、xml；空白与 xml:space；属性里的引用；哪些修改被拒；克隆；XPath 与 XSL 模式下的选择；经引用的校验；实体文本里的错误 |
| valuespaces | normalize 合并什么；node() 里有没有 DOCTYPE；属性与文本的值和 text 的空白；resolveExternals 的默认值与外部实体的加载；外部实体与 notation 的标识与 xml |

## 结论（Windows 11 29671，results/windows-29671.txt；Wine 为 results/wine-def826471c0.txt）

接口：
- MSXML 3.0 与自由线程 3.0 **不提供** `IXMLDOMDocument3`，6.0 提供（altars-up `37ad0e3e213` 照此实现，3.0 文档的 IDispatch
  随之改为 IXMLDOMDocument2 的）。
- 各版本都提供 `IPersistMoniker`、`IProvideClassInfo`、`IOleCommandTarget`、`IServiceProvider`（第 169 批测了各方法，
  altars-up `460f1366ef0` 照此实现）：`IProvideClassInfo` 给版本的 coclass——3.0 各类（含自由线程）是“DOMDocument”
  {F6D90F11-…}，6.0 各类是“DOMDocument60”{88D96A05-…}，没有 `IProvideClassInfo2`；`IOleCommandTarget` 不支持任何命令、
  不认识任何命令组，接受 `OLECMDID_STOP`；`IServiceProvider` 没有站点时一律 `E_NOINTERFACE`，6.0 把输出置 NULL、3.0 不碰；
  `IPersistMoniker::Load` 不带绑定上下文是 `E_INVALIDARG`、文件 moniker 绑不到流是 `E_FAIL`，失败也留着 moniker，
  `GetCurMoniker` 交回它时**不加引用**（调用方若 Release 会在 Windows 上同样出错——探针第一次就这样崩了）。
  `GetClassID` 是创建时的类：3.0 一律是不分版本的 F6D90F11，自由线程文档是自由线程类 F6D90F12、88D96A06（Wine 原来对
  Msxml2.DOMDocument.3.0 答 F5078F32）。类型库里的 coclass 名原为 “domDocument”（widl 名字表不分大小写，先遇到了
  ownerDocument 的参数名），`42daa0e2cf3` 按 SDK 改了参数名。
- 自由线程 3.0 与所有 6.0 文档的 `IMarshal` 是聚合的自由线程封送器——Wine 有意不做：这里的 DOM 不加锁，别的套间会直接
  并发调用它。2.6 在 Windows 11 上未注册（Wine 注册了）。

解析错误（altars-up `29dd3a40a14`、`56757fe71dc`）：
- 未定义的实体（`0xC00CE002`）、文本里的控制字符（`0xC00CE508`）、两个根元素（`0xC00CE555`）Windows 都拒绝；`<a` 是
  “元素未关闭”`0xC00CE55E`；空的 `loadXML("")` 记 `XML_E_MISSINGROOT`。行、列、filepos 指向出错处，srcText 是出错的那一行。
- 根元素之后：结束标记 `0xC00CE552`（报在名字上）、DOCTYPE `0xC00CE57E`（报在声明的名字上）、`]]>` `0xC00CE55C`、
  末尾单独的 `<` `0xC00CE562`、控制字符 `0xC00CE508`；引用与 CDATA 节是 `0xC00CE556`，报在 `&`、`&#`、`<![CDATA[` 之后；
  其余文本报在开头。
- 不在开头的 XML 声明（序言里注释之后、正文里、根元素之后都一样）：`<?xml ` 是 `0xC00CE557`（报在名字上），`<?xml?>`
  是 `0xC00CE507`（报在名字后），`<?XML` 是 `0xC00CE576`（`7d7e2f14ce6`）；`<?xml-stylesheet ...?>` 正常。
- reason 是带参数的中文原文，如“结束标记 'b' 与开始标记 'a' 不匹配。”；非 XML 的错误用系统消息（“未指定的错误”“拒绝访问。”）。
  3.0 与 6.0 措辞略有不同。Wine 仍是英文、不带参数，个别是 “error”——还没做。
- 流读失败时 load 返回 `S_FALSE`，唯独 `E_ACCESSDENIED`（0x80070005）直接返回该错误（`bc4d55d3ccb`）。

DTD 校验（altars-up `72207003cd2`、`e7a811081f3`、`452c34eb44f`）：Windows 边读边校验，报遇到的第一个错误，位置就是它的
SAX 定位器在相应事件上给的位置——
- 元素不该出现在那里、未声明、属性不该有或缺必需属性（依此先后）：报在该元素起始标记的末尾；
- 不该有的文本（EMPTY 元素里连空格也算）：报在文本开头，`0xC00CE018`；
- 缺子元素：报在结束标记的名字上，一个都没有是 `0xC00CE011`，不全是 `0xC00CE012`；
- 错误的子元素 `0xC00CE014`，重复的 ID `0xC00CE200`，ID 值不是名称 `0xC00CE504`，单名称类型的值里有多个名称 `0xC00CE574`；
- IDREF 找不到 ID：报在文档末尾，没有 filepos 和 srcText（`0xC00CE00E`）。
- 不校验加载后 `validate()` 报同样的错误，位置全为 0；没有 DTD 时 `0xC00CE224`。
- 3.0 与 6.0 唯一的差别：EMPTY 元素里被丢掉的空格，6.0 的 `validate()` 仍报 `0xC00CE018`，3.0 不报（`37bd26f7822`）。
- spaces 段：不校验时，两个版本都丢掉 EMPTY 与 (#PCDATA) 元素里只有空白的内容，ANY、元素、混合、未声明、无 DTD 的保留
  元素之间的空白；只剩被丢空白的元素写出来是 `<r>\r\n</r>`（Wine 原写 `<r/>`，`767ba961357`）。

`E_PENDING`（`d3a2b898381`）：第一次 `E_PENDING` 后立即再读一次；连续第二次仍是 `E_PENDING` 就挂起——load 返回 S_OK、
结果为真，readyState 停在 3，没有根元素，文档持有流直到被释放，之后不再读。数据中间出现一次 `E_PENDING` 同样重读一次后继续。

读块：先请求 4095 字节，之后是 MSXML 分词缓冲剩余的大小（不限量时 4095、6144、4117…；每次给 1 字节时 4095、8190、8189…），
6.0 每次给 1 字节时前八次是 4094、4093…。逐字节复刻要模拟它的分词器，功能上没有意义，不做。加载 VT_UNKNOWN 时 Windows
先对源对象 QI 两次 `IXMLDOMDocument`（`IPersistStreamInit::Load` 一次）。

实体引用（第 169、170 批；altars-up `303abd68397`、`def826471c0`）：
- DTD 读完时，MSXML 把每个内部实体的替换文本**单独**解析成该实体声明节点（doctype 的子节点）的子节点：没用到的实体里引用了
  未声明的实体也报错（`0xC00CE002`，行列与 srcText 都是相对替换文本的，如 “&zz;” 的 1:2）。实体节点的 text 像元素一样去掉
  首尾空白；xml 是 `<!ENTITY e "x">`，只写文本子节点和引用（`<b>y</b>` 写成 `""`，注释和 PI 不写）。
- 正文与属性值里的引用是只读的 entityref 节点，子节点是声明节点子节点的副本：所以引用所在处的 xml:space 不起作用，
  只含空白的实体（`" "`）引用后没有子节点，在属性里值也是空串；只有文档的 preserveWhiteSpace 才保留。字符引用与预定义实体
  并入相邻文本。引用前后被丢的空白让序列化像元素一样换行缩进（`<r>\r\n\t&e;\r\n\t<b/>\r\n</r>`），`a&w;b` 的 text 是 “a b”。
- 引用本身的子节点表不能改（appendChild/insertBefore/replaceChild/removeChild/put_text 都是 `E_FAIL`），里面的节点也不能改
  （put_nodeValue、put_data、appendData、setAttribute……）；引用本身可以移走、换父节点。深克隆：3.0 的副本可改、6.0 仍只读；
  浅克隆：3.0 没有子节点、6.0 重新带上实体的子节点。`createEntityReference` 带上声明的子节点（只读），未声明的没有子节点、text 是空串。
- XPath：6.0 透过所有引用，3.0 只透过不在别的引用里的那一层（里层引用连同其内容都看不见）；从引用本身出发的查询，6.0 一律
  `E_FAIL`，3.0 的 XPath 选其内容 `E_FAIL`。校验看引用代表的内容，错误位置在引用处（`&` 之后）。属性值里的实体带来 `<`
  报 `0xC00CE506`，位置在起始标记结束处。
- 剩下的只有 reason 原文。

值与声明（第 170 批）：normalize 合并相邻文本（含子元素里的），CDATA 不并、空文本保留；3.0 的 XSL 模式下 `node()` 包含
DOCTYPE，XPath 不含；属性的 text 在不保留空白时去掉首尾空白（nodeValue 是原值），文本节点的 nodeValue 也是原值；
3.0 的 resolveExternals 默认为真、6.0 为假，打开时 DTD 结束处就加载所有外部实体（用不用都加载），失败时 3.0 报
`0x800C0006`（URLOpenBlockingStream 本身是 `0x800C0005`，是 MSXML 3 换的码）、6.0 报 `0xC00CE009`；外部实体与 notation
的 SYSTEM/PUBLIC 是声明时的原样，`publicId`/`systemId`/`notationName` 缺的那个回 S_FALSE。altars-up `bad663f91a6`、
`303abd68397`、`def826471c0`。
