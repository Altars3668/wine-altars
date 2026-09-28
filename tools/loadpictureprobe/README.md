# 图片加载的错误码

`loadpictureprobe.exe` 量 VBScript `LoadPicture` 背后的错误：`OleLoadPicture` 对非图片数据、`OleLoadPictureFile` 对文本文件、不存在的文件、空名与不存在的目录，以及 `CreateFileW` 对这些名字和带冒号（流语法）的名字的错误码。

`results/loadpicture.win.txt`（Windows 11 build 29671，用户名已替换）：非图片数据 `CTL_E_INVALIDPICTURE`（Wine 原为 E_FAIL；没有 placeable 头的元文件仍是 E_FAIL）；`OleLoadPictureFile("")` 成功，得到空图片；目录不存在 `CTL_E_PATHNOTFOUND`；`CreateFileW` 对中间组件带冒号的路径报 123（`ERROR_INVALID_NAME`），`文件::$DATA` 打开文件本身——Wine 原来分别报 3 与 2（wine-src `3622d349`、`ffa8703d`）。VBScript 的 `LoadPicture` 不走 `OleLoadPictureFile`：找不到文件是 432、路径不存在或空名是 76，与存储错误（`STG_E_FILENOTFOUND`/`STG_E_PATHNOTFOUND`）的映射一致。
