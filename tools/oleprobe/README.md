# oleprobe

Word 的“插入 → 对象”只做三件事：把 ProgID 解析成 CLSID、创建这个类、向它要
`IOleObject`。注册表看着齐全而对象插不进去时，失败一定落在这三步中的某一步，
但 Word 只会给一句笼统的错误。这个探针把三步分开报 HRESULT。

```
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -municode \
    -o oleprobe.exe oleprobe.c -lole32 -loleaut32 -luuid
i686-w64-mingw32-gcc  ... -o oleprobe32.exe ...          # 32 位对照

wine oleprobe.exe                       # 默认查三个公式对象
wine oleprobe.exe Equation.AxMath        # 或者指定 ProgID
```

进程内和本地服务器两种激活方式分开试：一个 32 位产品的 `LocalServer32`，
64 位客户端本来就能跨进程驱动，所以`进程内 0x80040154 + 本地服务器 0x0` 是
正常结果，不是毛病。

编成两个位数是有意的——差异才说明问题出在哪个注册表视图。

## 这个仓库里它测出来的结论

MathType 的 `Equation.DSMT4` 和 AxMath 的 `Equation.AxMath`，在 64 位客户端里
`CLSCTX_LOCAL_SERVER` 都返回 `S_OK` 并给出 `IOleObject`。所以 64 位 Word 插入
这两种公式对象的通路是通的。

顺带证伪了一个想当然：AxMath 是 32 位安装程序装的，它的 CLSID 只写在
`HKLM\SOFTWARE\Classes\WOW6432Node\CLSID` 下，按 Windows 的规矩 64 位客户端
应该找不到。把它镜像进 64 位视图、再删掉、再镜像回去跑三轮，结果**三轮都成功**
——Wine 的 `HKLM\Software\Classes` 不按位数分视图，所以这类镜像是多余的，
加了只会让 prefix 偏离源安装。镜像已撤销。
