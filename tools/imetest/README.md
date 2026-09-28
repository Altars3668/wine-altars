# imetest：经 XIM 用真实的中文输入法向 Office 打字

`SendInput` 送进来的字符不经过 X 的输入法，所以 `tools/sendkeys` 那样的测试证明不了组字路径。这里的做法是在一个 X 显示上单独起一个输入法（fcitx5 或 ibus，拼音），用 `xdotool` 通过 XTEST 发真实的按键，再用 `tools/officeautomationprobe/word-type.vbs` 读回 Word 文档里的文字。

    tools/imetest/im-start.sh fcitx :78 [状态目录]     # 或 ibus；XAUTHORITY 照传
    XMODIFIERS=@im=fcitx  ……以 DISPLAY=:78 启动 Office（run-probe 一类的脚本要把 XMODIFIERS 传进去）
    xdotool key ctrl+space; xdotool type nihao; xdotool key space   # fcitx 要先切到拼音；ibus 直接就是拼音

`im-start.sh` 给输入法单独的 D-Bus 会话和 XDG 配置、数据、缓存目录，不碰桌面上正在用的那个输入法；停的时候按环境变量里的 `XDG_CONFIG_HOME` 找进程。`ximtest.c` 是一个原生 Xlib 客户端，按指定样式建输入上下文、打印预编辑回调与上屏的字符串，用来把“输入法的问题”和“Wine 的问题”分开。

两个输入法走的是 Wine 的两条路径：fcitx5 的 XIM 只提供 over-the-spot 与 root 样式，预编辑和候选都由它自己画在 `XNSpotLocation` 处；ibus 提供 on-the-spot（预编辑回调），这是 Wine 默认请求的样式，组字串由 Wine 的内置 IME 界面画、候选窗由 ibus 面板画，面板的位置同样来自 spot。

显示的选择：

- 开发用的无头 mutter（`:2`）上 XTEST 能用，mutter 管理焦点，最接近真实桌面；它是 Xwayland，根窗口截不到画面，用 `xwininfo -root -tree` 看各窗口的位置，或单独截某个窗口。
- 没有窗口管理器的 Xvfb（`:78`）能直接截根窗口，但焦点是 PointerRoot：Wine 忽略 detail 为 NotifyPointer 的 FocusIn，输入上下文要到第一个按键才建，这个键不经过输入法；先按一个无害的键再打字。
- libpinyin 对以 “i” 开头的输入另有模式，不出预编辑也不上屏，测的时候避开。

2026-09-28 的结果（wine-src `149ce1d`、`b7bd4c8`）：两种输入法在 Word 里都能正确上屏（`你好中文输入法` 等），候选窗在插入行下方，ibus 的组字串显示在插入点。此前 ibus 的候选面板停在屏幕左上角、组字串在 Word 窗口左下角，fcitx 在插入符第一次移动之前也画在左上角。
