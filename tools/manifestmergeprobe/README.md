# Click-to-Run 的清单合并在 MSXML 里是否保留前缀：`merge.js`

Click-to-Run 把各产品清单（`Microsoft Office\PackageManifests\AppXManifest.90160000-….xml`）的 `appv:Extension`
并入 common 清单，写成 App-V 使用的 `Microsoft Office\AppXManifest.xml`。本脚本用 MSXML DOM 重做一次：载入 common 清单与
Excel 的产品清单，按命名空间选出扩展点，分别以直接移动（appendChild）、`cloneNode(true)` 后追加、`importNode` 三种方式
并入，打印并入节点的 `nodeName`/`prefix`/`namespaceURI`、序列化结果，并把结果重新解析后按 appv 与 appx 命名空间计数。

用法：把前缀里的 `PackageManifests\AppXManifest.common.16.xml` 与 `AppXManifest.90160000-0016-0000-1000-0000000FF1CE.xml`
复制到一个临时目录（不要提交微软的文件），然后

    cscript //nologo merge.js <该目录的 Windows 路径> [Msxml2.DOMDocument.6.0|Msxml2.DOMDocument.3.0]

`merge.wine-msxml6.txt`（altars-up，含 `e7313aa729c`“Keep the prefixes of a node and its attributes in a clone”）：移动与
克隆都保留 `appv:` 前缀，重新解析后 631 个扩展点都在 appv 命名空间（630 个 common 加并入的 1 个）。9 月 20 日用当时的
Wine 安装的前缀里，合并出的 2342 个产品扩展点全部没有前缀、落在 appx 命名空间，App-V 因此从不集成 Excel 等的文件关联，
Click-to-Run 每次启动都“修复”；`scripts/fix-c2r-merged-manifest.py` 修复这样的前缀（见 `docs/office365-under-wine.md`）。
脚本里的 `JScript` 取不到 `importNode`（两种 MSXML 版本的 DOMDocument 都没有），那一项打印空错误。
