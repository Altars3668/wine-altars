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
