# 参数化 WinRT 类型的 IID

`paramiidprobe.exe` 自带一个元数据定位器（`IRoMetaDataLocator`），用 combase 的 `RoGetParameterizedTypeInstanceIID`
算各种类型实例的 IID，打印 HRESULT、IID、`RoParameterizedTypeExtraGetTypeSignature` 给的类型签名，以及定位器按什么顺序被问到
哪些名字。只做计算，不注册、不写任何东西。传 NULL 指针的情形没放进来：在 Windows 上可能直接崩。

`results/paramiid.win.txt`（Windows 11 build 29671，第 125 批）：

- IID 是签名在命名空间 `11f47ad5-7b73-42c0-abae-878b1e16adee` 下的第 5 版（SHA-1）UUID；`IVector<String>` 是
  `98b9acc1-…`，签名 `pinterface({913337e9-…};string)`。参数各以 `;` 开头，嵌套照写。
- 基本类型 14 个，名字区分大小写：`Boolean b1`、`UInt8 u1`、`Int16 i2`、`UInt16 u2`、`Int32 i4`、`UInt32 u4`、`Int64 i8`、
  `UInt64 u8`、`Single f4`、`Double f8`、`Char16 c2`、`String string`、`Guid g16`、`Object cinterface(IInspectable)`。
  `Int8`、`string`、`Byte`、`TimeSpan` 等都交给定位器。
- 定位器按前序遍历被问到每个非基本类型的名字：接口 `{iid}`，委托 `delegate({iid})`，参数化接口与参数化委托都是
  `pinterface({piid};…)`，运行时类 `rc(名字;默认接口)`，接口组 `ig(名字;默认接口)`，结构 `struct(名字;字段…)`，枚举
  `enum(名字;基础类型)`。默认接口没给 IID 时按名字再问定位器；结构字段、枚举的基础类型都按类型名解析（枚举基础类型给
  `String` 也照收）；字段里的参数化类型会把后面的字段名当作自己的参数吃掉。
- 类型不必是参数化的：单独 `String` 得 `cce04bc6-…`，签名 `string`。
- 错误：参数不够或多余 `E_INVALIDARG`；名字个数为 0、结构没有字段 `E_UNEXPECTED`；定位器的错误原样返回。失败时 IID 清零，
  附加句柄照样给（签名是半截，Windows 不补结尾零，读出来带着旧数据）。定位器第二次描述同一个名字得 `E_INVALIDARG`，以第一次为准。
- 定位器什么都不描述却返回 S_OK 时，Windows 把这个名字当成“无前后缀、带一个参数”的东西，算出无意义的 IID；Wine 报 `E_UNEXPECTED`。

wine-src altars-up `c645e671272` 实现后，Wine 下除失败情形里 Windows 的残留数据和上面最后一条外，与 Windows 逐行相同。
