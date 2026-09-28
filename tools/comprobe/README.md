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
