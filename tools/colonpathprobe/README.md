# 盘符、设备与路径各段的检查顺序

`colonpathprobe.exe` 量的是：盘符或设备不存在、前面某级目录不存在时，后面有毛病的路径段（中间段带冒号、`.`、`..`、
空段、非法字符）报什么错。Win32 一侧用 `CreateFileW` 加 `GetFullPathNameW`，NT 一侧用 `NtOpenFile`；只打开、只读属性，
不建任何东西。路径里的 `?:` 换成本机不存在的盘符（从 Q 往后找）。mfplat 的源解析器测试用 `::C:\…`、`\\file:\…`
这样的名字测的就是 Win32 这一侧。

`results/colonpath.win.txt`（Windows 11 build 29671，第 121–122 批，临时目录已换成 `<dir>`）：

- **盘不存在时，后面一概不看**：`Q:\x`、`Q:\Windows:x\win.ini`、`\??\Q:\.\x`、`\??\Q:\a\\b`、`\??\Q:\a<b` 都是
  `STATUS_OBJECT_PATH_NOT_FOUND`（Win32 是 3）；只有 `\??\Q:` 本身是 `STATUS_OBJECT_NAME_NOT_FOUND`。
- `::x` 是 x 在盘 `:` 上（`GetFullPathNameW` 给 `::\x`），这个盘不存在，所以 `::C:\Windows\win.ini`、`:::::C:\…`、
  `::file://C:\…` 都是 3；`:C:\…` 是相对路径，中间段带冒号，是 123。
- **盘存在时**：空段（双反斜杠）和中间段的流（冒号）整条路径先查，前面的目录在不在都报 `STATUS_OBJECT_NAME_INVALID`；
  非法字符、`..` 要查到那一段才报 `NAME_INVALID`，前面有目录不存在就是 `PATH_NOT_FOUND`；`.` 在末段是
  `NAME_INVALID`，在中间段是 `PATH_NOT_FOUND`。
- `\\file:\C:\…`（UNC，服务器名带冒号）在这台机器上是 64；mfplat 测试里的 Windows 是 53（`ERROR_BAD_NETPATH`），
  随网络环境变，Wine 仍是 3，没动。

Wine 原来跳过对盘符的检查、拿后面各段作答（盘不存在时报 2 或 123），又把非法字符、`.`、`..` 都放在查找之前检查。
wine-src altars-up `4d7e5cfea14` 照 Windows 改后，除 UNC 一条外与 Windows 逐行相同。mfplat 全量测试原有 8 个失败：
`::` 开头的 4 个随之通过，另 4 个（`:`、`/file://` 等）早先改冒号规则时已与 Windows 相同，只是测试还标着 todo，一并去掉。
