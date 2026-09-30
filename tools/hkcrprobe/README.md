# HKEY_CLASSES_ROOT 合并视图的细节：`hkcrprobe.exe`

在 HKLM\Software\Classes 与 HKCU\Software\Classes 下各建一个同名类键（各有独有与共有的值和子键），经 HKCR 打印：句柄标记、
各值查询结果（取自哪一边）、`RegQueryInfoKey` 的计数、值与子键的枚举顺序、哪些子键打得开；用户注册的类在 32 位视图下是在
`Classes\Wow6432Node\CLSID` 还是 `Classes\CLSID\Wow6432Node` 下；HKCR 被 `RegOverridePredefKey` 换成普通键时打开的句柄是什么；
以及 `HKEY_USERS\<sid>_Classes` 是否就是用户的类（不打印 SID）。建的东西都会删掉；写 HKLM 要提升的进程。

`results/hkcrprobe.wine.txt`：altars-up（`01664598d93` 起合并视图）的输出；Windows 的待测（winref），用来核对查询回退、合并计数与
覆盖行为这几处未实测的选择。
