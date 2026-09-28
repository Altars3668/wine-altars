# 模块字符串表

`stringtableprobe.exe <dll> [first] [last]` 以纯资源方式（`LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE`）映射模块，按当前界面语言对每个 ID 调 `LoadStringW`，一行一条输出 `id<TAB>文本`（UTF-8，制表符、换行、反斜杠转义）。不运行模块的任何代码。

用途：取 Windows 自己的本地化文本来核对 Wine 的翻译——vbscript.dll 与 jscript.dll 的中文错误描述就是这样取来的（两者共享的条目在 Windows 上用词一致）。oleaut32.dll 没有字符串表（本地化的 True/False 另有来源，见 `tools/boolstrprobe`）。

    scripts/build-probe.sh tools/stringtableprobe/stringtableprobe.c tools/stringtableprobe/stringtableprobe.exe user32
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh tools/stringtableprobe/stringtableprobe.exe vbscript.dll

输出含微软的本地化文本，只作对照，不提交全文。
