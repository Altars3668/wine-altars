# propsys 转成字符串时各种值的写法：`propvarstrprobe.exe`

Wine 的 propsys 测试（Windows 实测）只定下了几种：`TRUE` 写作 "1"，0.125f、0.456 照写，FILETIME 与 DATE 写作
`yyyy/mm/dd:hh:mm:ss.mmm`（FILETIME 不换时区；DATE 只到秒），向量的各元素之间用 "; "。altars-up 按这些把
`PropVariantToStringAlloc`（因而 `PropVariantToString`、`PropVariantToBSTR`）和 `VariantToString` 改成共用一个逐值格式化的
实现；Excel、PowerPoint 启动时会拿一个字节 SAFEARRAY（`VT_ARRAY|VT_UI1`）调 `VariantToString`，Wine 仍返回 `E_NOTIMPL`，
因为没有依据。

探针打印三个函数对这些值的结果：`VARIANT_TRUE`/`FALSE`、大、小、不精确和特殊的 double、不精确的 float、带小数秒与负值的
DATE、CY、DECIMAL、ERROR、字节与字符串的 SAFEARRAY（以及 `VariantToPropVariant` 把字节 SAFEARRAY 变成什么）、空向量与
字节、布尔、double、字符串向量。

`results/propvarstrprobe.wine.txt`：Wine（上述改动之后：布尔按数值写，浮点按 `%.7g`/`%.15g`，CY、DECIMAL、ERROR 与
SAFEARRAY 失败）。Windows 的待测（winref），据此补齐。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror propvarstrprobe.c -o propvarstrprobe.exe -lpropsys -loleaut32 -lole32`。
