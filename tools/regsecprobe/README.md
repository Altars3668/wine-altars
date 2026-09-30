# 注册表键安全描述符的长度：`regsecprobe.exe`

Office 启动时用 `RegQueryInfoKeyW` 要键的安全描述符长度；Wine 记 `security argument not supported` 并给 0。
探针对几个键（以及在 HKCU\Software 下临时建、随后删掉的一个）打印 `RegQueryInfoKeyW` 报的长度，以及
`RegGetKeySecurity` 在“所有者”“所有者+组+DACL”“再加完整性标签”“再加 SACL（需要特权）”下要的长度——看 Windows
报的是哪一种组合的长度。

`results/regsecprobe.wine.txt`：Wine；Windows 的待测（winref）。
