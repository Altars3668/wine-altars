# COM 激活的原生对照

`missingdll.c` 看进程内服务器加载不了时 COM 返回什么：注册在 `HKCU\Software\Classes\CLSID` 下、`InprocServer32` 指向不存在的文件；指向一个不是 DLL 的文本文件；由活动上下文（清单里的 `<file><comClass/></file>`）声明、文件不存在；以及根本没注册。注册表项用完即删。

    scripts/build-probe.sh tools/comprobe/missingdll.c tools/comprobe/missingdll.exe ole32 advapi32 uuid
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh tools/comprobe/missingdll.exe > tools/comprobe/missingdll.win.txt

`missingdll.win.txt` 是 winref（build 29671）上的输出。

## 测出的契约

- 服务器加载不了时，`CoCreateInstance` 与 `CoGetClassObject` 都返回加载器的错误：文件不存在 `HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND)`（0x8007007e），不是 DLL `ERROR_BAD_EXE_FORMAT`（0x800700c1）；注册表与活动上下文两种来源相同。Wine 原先一律 `E_ACCESSDENIED`（wine-src `048eaaf` 改正）。
- **注册在 `HKCU\Software\Classes` 下的类，Windows 的 COM 能找到**（HKCR 是 HKCU 覆盖 HKLM 的合并视图）；Wine 的 HKCR 只是 `HKLM\Software\Classes`，这类注册一律 `REGDB_E_CLASSNOTREG`。advapi32 的 `test_classesroot` 在第一项就 `todo_wine` 并跳过整段。Office（C2R）的类全在 HKLM，这套 prefix 里 HKCU\Software\Classes 根本不存在，所以眼下不影响 Office，但按用户安装的软件（及其 COM 加载项、文件关联）都依赖它。

## 背景：OfficeClickToRun 服务里 msoxmlmf.dll 加载失败不是 Wine 的错

Office 运行时 `err:ole:apartment_add_dll couldn't load in-process dll "...\Common Files\Microsoft Shared\ClickToRun\msoxmlmf.dll"` 来自 OfficeClickToRun.exe：它用 urlmon 绑定 C2R 目录里的 Manifest.xml，urlmon 为 `.xml` 去创建 InfoPath 的 XML MIME 过滤器 `{807583E5-5146-11D5-A672-00B0D022E945}`。注册表里这个类指向 vfs 下确实存在的 `MSOXMLMF.DLL`，但 OfficeClickToRun.exe 的内嵌清单里写着

    <!--Com classes that we explicitly want our executable to only try and load from our folder -->
    <file name="msoxmlmf.dll"><comClass clsid="{807583E5-...}" .../></file>

而它自己的目录里没有这个文件——微软用免注册 COM 故意让服务加载不到这个过滤器（`grooveex.dll` 等同理）。Windows 上同样加载失败，只是不打日志。

# ProcessUrlAction 的原生对照

`urlaction.c` 用 `CoInternetCreateSecurityManager` 得到的安全管理器，对 Internet 区域里策略为允许、询问、拒绝的几个动作各问一次 `ProcessUrlAction`，都带 `PUAF_NOUI`；再按 Click-to-Run 服务的问法问 file URL 的 `URLACTION_SHELL_FILE_DOWNLOAD`，以及带 `PUAF_ISFILE` 的普通路径和空 URL。

    scripts/build-probe.sh tools/comprobe/urlaction.c tools/comprobe/urlaction.exe urlmon ole32 uuid

- 策略为询问（QUERY）时，带 `PUAF_NOUI` 返回 **S_FALSE**（不能问就是不允许），策略值照常写回；Wine 原先返回 E_FAIL。
- 允许的返回 S_OK，拒绝的 S_FALSE；file URL 与带 `PUAF_ISFILE` 的路径都是本机区域 0；空 URL 是 E_INVALIDARG。这些 Wine 本来就对。
- **`PUAF_WARN_IF_DENIED` 遇到拒绝时，即使同时给了 `PUAF_NOUI`，Windows 也要弹“被拒绝”的警告**：在 ssh 的服务会话里这次调用永远不返回（探针里因此不再问这一项）。
- Office 一次会话里的 45 条 `SecManagerImpl_ProcessUrlAction Unsupported arguments` 全是 Click-to-Run 服务对 `C2RManifest.*.xml` 的 file URL 问 `URLACTION_SHELL_FILE_DOWNLOAD`（0x1803）、带 `PUAF_NOUI`，策略允许，结果本来就对。

# Windows 拼写检查 API 的原生对照

`spellcheck.c` 量 `MsSpellCheckingFacility.dll` 的 `SpellCheckerFactory`（`{7AB36653-1796-484B-BDFA-E74F1DB7C1DC}`，Windows 8 起）。Office（PowerPoint 一次会话 4 次）会去创建它；Wine 没有这个 DLL，`CoCreateInstance` 失败后 Office 回退到自带的校对工具。接口按 SDK 在探针里声明，每个都按 IID 做了 QI，所以 IID 也一并核对过了。

    scripts/build-probe.sh tools/comprobe/spellcheck.c tools/comprobe/spellcheck.exe ole32 uuid

- winref 上支持的语言：en-CA、en-LR、en-PH、en-US、zh-Latn-CN-x-ext；`IsSupported` 不分大小写，"en"、"en-GB"、"zh-CN" 都不支持，空串 E_INVALIDARG，不支持的语言 `CreateSpellChecker` 也是 E_INVALIDARG。
- 检查器的 Id 是 `MsSpell`，没有选项（`OptionIds` 为空，`GetOptionValue` E_INVALIDARG），支持 `ISpellChecker2` 与 `IUserDictionariesRegistrar`。
- `Check` 按出现顺序给错误：拼错的词是 `CORRECTIVE_ACTION_GET_SUGGESTIONS`，重复的词（“the the”的第二个）是 `DELETE`，替换串是空串而不是 NULL；空文本 E_INVALIDARG。`ComprehensiveCheck` 在这些例子上结果相同。
- `Suggest("helo")` 给出有序候选（hello、halo、helot…），对拼对的词也给近似词，对无意义的串给空列表；`Ignore` 之后同一检查器不再报该词。

Wine 若要实现，宿主机有 `libhunspell-1.7.so.0` 与 en_US/en_GB 词典，可以 dlopen 其 C API；建议的顺序与措辞以此为准，候选词本身因词典不同不必逐字相同。

# COM 激活过滤器的原生对照

`actfilter.c` 用 `CoRegisterActivationFilter`（Windows 8 起）注册一个记录每次调用的 `IActivationFilter`，再按 COM 提供的每条途径激活对象：`CoCreateInstance`/`CoGetClassObject` 普通进程内类、COM 自己的全局接口表、`CoRegisterClassObject` 注册的类、根本没注册的类；从存储、流、文件、类 moniker、文件 moniker、`clsid:` 显示名、`OleCreate`、`OleCreateFromFile`、`OleCreateFromData`（数据对象打印被问到的每一步）；自定义 OBJREF 指名的解组器；标准列集为 combase、oleaut32、actxprxy 各自代理的接口建的存根与（另一线程上的）代理；另一线程上的激活。然后让过滤器拒绝一个类、用另一个类顶替、顶替后再拒绝顶替者、回答 S_FALSE，最后注册第二个过滤器、`OleUninitialize` 后重新初始化。

    scripts/build-probe.sh tools/comprobe/actfilter.c tools/comprobe/actfilter.exe ole32 uuid shell32 user32

`actfilter.win.txt` 是 winref（build 29671）上的输出。Wine 实现后同一探针只剩下面“内部类”与 `OleCreateFromFile` 两处差异；`dlls/ole32/tests/activation.c` 把这些契约写成了一致性测试，Windows 与 Wine 上都 0 失败。

## 测出的契约

- **每次激活都先问过滤器一次，再去找类**：`CoCreateInstance(Ex)`、`CoGetClassObject`、COM 自己的 GIT、`CoRegisterClassObject` 注册的类都问；没注册的类也先问一次，然后才 `REGDB_E_CLASSNOTREG`；另一线程（MTA）上的激活同样问。
- `pReplacementClsId` 进来时**总是 GUID_NULL**。过滤器返回失败，激活就以该 HRESULT 失败（E_ACCESSDENIED 原样返回）；S_FALSE 等成功码视为放行。过滤器写入另一个类时，COM 用同一类型再问一次这个替换类（入参仍是 GUID_NULL），放行后激活的是替换类；替换类被拒则整个激活以其 HRESULT 失败。写回类自身或 GUID_NULL 就是不替换。
- **类型**（`ACTIVATIONTYPE`，按位）：直接的 `CoCreateInstance`/`CoGetClassObject`、`OleCreate`、自定义 OBJREF 的解组器都是 0；类 moniker 绑定、**`OleLoadFromStream`** 是 FROM_MONIKER（1）；`OleLoad` 是 FROM_STORAGE（4）；`OleCreateFromData` 是 FROM_DATA（2）；`CoGetInstanceFromFile` 是 FROM_FILE（0x10）；文件 moniker 绑定是 FROM_MONIKER|FROM_FILE（0x11）；`OleCreateFromFile` 里从存储载入是 0x10。替换类被问时类型与原来相同（经类 moniker 顶替时两次都是 1）。FROM_STREAM（8）在这些途径里都没出现。
- **代理/存根工厂**：combase 自带的标准代理（IPersist、IClassFactory 等，`{00000320}`）不问；其他工厂（oleaut32 的 PSDispatch `{00020420}`，IServiceProvider 的）只在进程第一次载入时问一次，类型 0，之后同一工厂建存根、在另一线程上建代理都不再问。
- **注册**：NULL 是 E_INVALIDARG；同一个过滤器再注册 S_OK；另一个过滤器是 `CO_E_NOT_SUPPORTED`（0x80004021），原来的照旧；COM 不 AddRef 过滤器，也从不 QueryInterface 它；`OleUninitialize` 之后再初始化，过滤器仍在。
- Windows 还会问两个内部类，Wine 不产生这些对象，不仿：`{00000346-0000-0000-C000-000000000046}`（注册表里没有；出现在 `OleLoad`、`OleCreate`、`OleCreateFromData`、`CoGetInstanceFromFile`、文件 moniker 这些涉及存储的途径里，类型与随后那次相同），以及解析 `clsid:` 显示名时的类 moniker 类 `{0000031A}`（类型 1）。
- `OleCreateFromFile` 对不是 OLE 服务器的文件，Windows 建的是打包对象（Packager `{F20DA720-C02F-11CE-927B-0800095AE340}` 及几个外壳类），成功；Wine 的 `OleCreateFromFile` 直接绑定文件 moniker，这里 E_NOINTERFACE。缺的是打包器这条路，与过滤器无关。
- `OleCreateFromData` 在 Windows 上先 `EnumFormatEtc`，再 `QueryGetData("Embedded Object", TYMED_ISTORAGE)`，没有就直接 `GetDataHere("Embed Source", TYMED_ISTORAGE)`；`OleQueryCreateFromData` 只 `EnumFormatEtc`。Wine 原先在进程从没用过 OLE 剪贴板时，这两处拿到的格式号都是 0（格式只在建剪贴板对象时才注册），`OleQueryCreateFromData` 因此回答 S_FALSE、`OleCreateFromData` 根本找不到嵌入对象。

## 探针自身的一个坑

最初的 `name()` 返回同一个静态缓冲区，而一条 `printf` 里调用了它两次：参数从右往左求值，后一次把前一次覆盖，于是凡是以 GUID 字符串打印的类都显得“替换参数预置为类自身”，与以名字打印的类（GUID_NULL）矛盾。换成两个轮流使用的缓冲区后，Windows 上 45 次调用的入参全是 GUID_NULL。量出与直觉不符的结果时，先查探针自己的格式化。

## Office 用它做什么（续见下两节）

Word 启动时就调用 `CoRegisterActivationFilter`（过滤器是堆上的对象）。普通会话里它被问几十次，全是类型 0，全部放行。插入 ActiveX 控件（`Forms.CommandButton.1`）根本走不到激活：Microsoft 365 先按策略拒绝，提示“由于您的策略设置，无法插入此对象。”（2025 年起默认禁用 ActiveX）。用 `tools/officeautomationprobe/word-embed.vbs` 嵌入 Excel 工作表时，过滤器被问到 `Excel.Sheet.12`（`{00020830}`）两次（先进程内、后本地服务器），类型 0，都放行；随后 Word 调 Excel 的一次跨进程调用返回 0x800703e6，Word 报“用于创建此对象的程序是 Excel。您的计算机尚未安装此程序或此程序无响应。”——换回没有过滤器的 combase/ole32 结果相同，是另一个既有缺陷。
这个“既有缺陷”后来查清并修掉了，见下两节与 `tools/subclassprobe/README.md`：Excel 崩溃在 comctl32 跨进程读子类栈，之后的卡死依次是跨线程发送消息时的向外调用、默认处理器的 QI、以及 CrossOver 的 shm surface 在持 USER 锁时跨进程发送。现在 `word-embed.vbs` 能嵌入工作表、保存、重开、就地激活并读到 `Worksheets(1).Name`。

# 处理别的线程发来的消息时向外调用：input_sync 的原生对照

`inputsync.c`：主线程（单线程套间）做一个对象，带就地激活用到的全部接口——`IOleInPlaceFrame`（含 `IOleWindow`、`IOleInPlaceUIWindow`）、`IOleInPlaceSite`、`IOleInPlaceActiveObject`、`IOleInPlaceObject`、`IOleClientSite`、`IOleCommandTarget`——把代理交给第二个单线程套间，后者有一个窗口。主线程向这个窗口**发送**消息，第二个线程在处理它时逐个调用每个代理的每个方法；对象记录自己是否真被调到。另外在处理投递的消息、在已 `ReplyMessage` 的发送消息里、在线程发给自己的消息里各调一次 `GetWindow` 与 `ContextSensitiveHelp`。发送方五秒后放弃，调用若在等发送方就会显示为“等到发送方放弃才送达”。

    scripts/build-probe.sh tools/comprobe/inputsync.c tools/comprobe/inputsync.exe ole32 oleaut32 user32 uuid

`inputsync.win.txt` 是 winref 上的输出；wine-src `548ca9b` 之后 Wine 与之逐行一致。

- 线程在处理**别的线程发送、且还在等回复**的消息时（`InSendMessageEx` 为 `ISMEX_SEND`），只有 IDL 标了 `[input_sync]` 的方法能调出去——Windows 用发送消息投递它们，正在 `SendMessage` 里等待的调用方线程照样处理；其余方法立即失败 `RPC_E_CANTCALLOUT_ININPUTSYNCCALL`（0x8001010D），对象不会被调到。**不论这个线程自己有没有未完成的调用**（Wine 原先只在有未完成调用时才拒绝，于是 Word 销毁承载 Excel 的窗口、等 Excel 处理 WM_DESTROY 时，Excel 在 WM_DESTROY 里调 `IOleInPlaceSite::DiscardUndoState` 并等 Word——双方永远等下去）。
- 投递的消息、`ReplyMessage` 之后（`ISMEX_SEND|ISMEX_REPLIED`）、线程发给自己的消息（`InSendMessageEx` 为 0）里，任何调用都照常。
- Windows 11 上 input_sync 的方法恰好是：`IOleWindow::GetWindow`；`IOleInPlaceUIWindow::GetBorder/RequestBorderSpace/SetBorderSpace`；`IOleInPlaceFrame::SetMenu/SetStatusText`；`IOleInPlaceActiveObject::OnFrameWindowActivate/OnDocWindowActivate/ResizeBorder`；`IOleInPlaceObject::SetObjectRects`；`IOleCommandTarget::QueryStatus`。`IOleInPlaceSite` 除继承的 `GetWindow` 外一个都不是，`IOleClientSite` 与 `IOleCommandTarget::Exec` 也不是。
- `IOleInPlaceActiveObject::TranslateAccelerator` 的代理在任何情况下都直接回 S_FALSE，不调对象（另一套间的对象在自己的消息循环里处理加速键）。

# OLE 默认处理器的 QueryInterface：handlerqi 的原生对照

`handlerqi.c` 用 `OleCreateDefaultHandler` 为 ProgID 的类（默认 `Excel.Sheet.12`，本地服务器）建默认处理器，对一组接口 QI：未运行时、`InitNew` 后 `OleRun` 运行中、`IOleObject::Close` 之后各一次，并检查拿到的指针 QI(IUnknown) 是不是处理器本身。要能启动本地服务器，须在桌面会话里跑：

    scripts/build-probe.sh tools/comprobe/handlerqi.c tools/comprobe/handlerqi.exe ole32 uuid
    WIN_HOST=… WIN_USER=… WIN_PORT=… WIN_WAIT=240 scripts/winrun.sh --desktop tools/comprobe/handlerqi.exe

`handlerqi.win.txt` 是 winref 上的输出（会以嵌入方式短暂启动 Excel 再关闭）。

- 处理器自己有的（IOleObject、IDataObject、IPersistStorage、IRunnableObject、IViewObject2、IOleCache2）任何时候都给。
- 别的接口：**从未运行过**时答 E_NOINTERFACE；**运行中**转给对象（IOleWindow、IOleInPlaceObject、IDispatch、IPersistFile 都拿得到），且拿到的指针 QI(IUnknown) 得到的是处理器本身——Windows 把代理管理器聚合在处理器里；**运行过又关闭**后答 CO_E_OBJNOTCONNECTED（CrossOver 注释里说的“原生返回 CO_E_OBJNOTCONNECTED”只对这种状态成立）；IOleLink 任何时候都是 E_NOINTERFACE。
- wine-src `c3cde46` 按此实现了这三种状态的返回值；运行中返回的是对象代理本身，身份没有聚合进处理器——Wine 的 `CoGetStdMarshalEx` 还是桩、代理管理器不支持聚合，这一点仍与 Windows 不同。

# 消息过滤器被交到哪些调用：msgfilter 的原生对照

`msgfilter.c`：主线程（单线程套间）注册一个记录 `HandleInComingCall` 的消息过滤器、做一个只有 IPersist 的对象；另一个线程解组代理，向代理要一个对象没有的接口和 IUnknown（COM 经 IRemUnknown 去问对象的套间），调 `IPersist::GetClassID`，释放代理（IRemUnknown 的 RemRelease）——此时主线程在 `CoWaitForMultipleHandles` 里等；然后再解组一个代理、调一次 `GetClassID`，这时主线程只用 `GetMessage` 泵消息。

    scripts/build-probe.sh tools/comprobe/msgfilter.c tools/comprobe/msgfilter.exe ole32 user32 uuid

`msgfilter.win.txt` 是 winref 上的输出（开头一长串是 Windows 的 COM 在列集时对对象的 QI，与本题无关）。wine-src `c28e1b3` 之后，除这些 QI 外 Wine 与之一致。

- **IRemUnknown 的调用（RemQueryInterface、RemRelease）不交给过滤器**，只有应用自己的调用才交。Wine 原先都交，还把 COM 内部的 RemUnknown 对象当作 `pUnk` 递给应用——Word 的过滤器每次都去 QI 它要 Word 自己的接口 `{000209FA-…}`，于是 Office 日志里满是 `RemUnknown_QueryInterface No interface`。
- 调用类型：线程在 `CoWaitForMultipleHandles` 里等（包括等自己发出的调用）时到来的调用是 `CALLTYPE_TOPLEVEL_CALLPENDING`（4）；只在普通消息循环里时是 `CALLTYPE_TOPLEVEL`（1）。Wine 原先按“正在服务的调用数”判断。
- 另：Office 在很多对象上 QI 的 `{E19C7100-9709-4DB7-9373-E7B518B47086}`，Windows 上 NetworkListManager、WbemLocator、MXXMLWriter60、SAXXMLReader60 同样答 E_NOINTERFACE（临时探针实测），Wine 那些 FIXME/ERR 只是日志。
