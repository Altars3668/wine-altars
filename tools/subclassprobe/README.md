# comctl32 子类化辅助函数的原生对照

`subclass.c` 在自己的线程里建一个窗口并用 `SetWindowSubclass` 子类化，列出窗口此时的属性；然后让同进程的另一个线程、以及拿着窗口句柄的第二个本程序进程，分别对它调 `GetWindowSubclass`、读辅助函数保存栈的窗口属性、在子类过程之外调 `DefSubclassProc(WM_GETTEXTLENGTH)`、再 `SetWindowSubclass` 与 `RemoveWindowSubclass`（移除所有者的子类）、`SetPropW`；每一轮之后所有者再查一次自己的子类还在不在、还被不被调用。四个函数按序号 410–413 取（comctl32 5 只按序号导出），第一个参数 `v6` 时经激活上下文用 comctl32 6。

    scripts/build-probe.sh tools/subclassprobe/subclass.c tools/subclassprobe/subclass.exe user32
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh tools/subclassprobe/subclass.exe v5 > tools/subclassprobe/subclass-v5.win.txt

`subclass-v5.win.txt`、`subclass-v6.win.txt` 是 winref（build 29671）上的输出；wine-src `8741ba9` 之后 Wine 与之逐行一致（Wine 自己的 `__wine_*` 窗口属性不列出）。

## 测出的契约（v5 与 v6 相同）

- **别的进程的窗口**：`GetWindowSubclass` 返回 FALSE、引用数据置 0；`SetWindowSubclass`、`RemoveWindowSubclass` 都返回 FALSE，什么都不改。窗口属性本身跨进程可读可写（`SetPropW` 成功），所以辅助函数必须自己判断窗口属于哪个进程——Wine 原先直接拿 `GetPropW` 取回别的进程里的指针来解引用，Excel 就地激活在 Word 文档里时对 Word 的窗口调 `GetWindowSubclass`，在 `IOleObject::DoVerb` 当中崩溃。
- **同进程的另一个线程**：`GetWindowSubclass` 看得到所有者的子类；`SetWindowSubclass` 失败（只有窗口的线程能加子类）；`RemoveWindowSubclass` 成功，且真的移除了所有者的子类（之后子类过程不再被调用）。
- `DefSubclassProc` 在子类过程之外调用（包括所有者线程自己）返回 0，什么都不调。
- 都不改 LastError。
- v5 把栈存在窗口属性 `CC32SubclassInfo` 下，**v6 用 `UxSubclassInfo`**；两个版本在同一进程里互不相见。
