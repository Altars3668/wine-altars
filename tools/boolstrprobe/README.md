# 布尔值转字符串

`boolstrprobe.exe` 对 10 个 locale（zh-CN、zh-TW、en-US、de-DE、fr-FR、ja-JP、ru-RU、用户与系统默认、0）与 4 种标志（0、`VARIANT_LOCALBOOL`、`VARIANT_ALPHABOOL`、两者）调 `VarBstrFromBool` 与 `VariantChangeTypeEx`，再做一次 `VarCat("x", True)`。

`results/boolstr.win.txt`（Windows 11 build 29671）：中文（简、繁）在 `VARIANT_LOCALBOOL` 下仍是 “True”/“False”，俄语 True 是 “Истина”；Wine 原来是“真/假”与“Правда”，于是中文系统上 VBScript 的 `CStr(True)`、`"x" & True` 都成了“真”。修正后（wine-src `fcc0548e`）两边逐行相同。
