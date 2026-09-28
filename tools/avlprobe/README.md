# avlprobe：ntdll 的 AVL 通用表与 RtlIsNameInExpression

Office 的 App-V 子系统（`AppvIsvSubsystems64.dll`，Word 等应用都会加载）用 AVL 通用表存它的表，用 `RtlIsNameInExpression`
匹配名字。Wine 原来的 AVL 函数什么都不做（初始化不初始化，插入不分配也不返回元素，查找永远为空），枚举、删除和
`RtlIsNameInExpression` 或缺失或一调用就异常。

    scripts/build-probe.sh tools/avlprobe/avlprobe.c tools/avlprobe/avlprobe.exe
    avlprobe.exe [names|avl]

输出不含指针：节点按它存的值命名，树按“值、平衡因子、括号里的左右子树（点表示无）”打印，分配按大小和交出的指针在其中的偏移
打印，因此 Windows 与 Wine 的输出可以直接 diff。`results/avl.win.txt` 是 Windows 11 build 29671 上的结果，wine-src `c92e5b9`
之后 Wine 的输出与它逐行相同。

量到的规则：

- 元素是 `RTL_BALANCED_LINKS` 后接数据（x64 上数据在 +32），分配大小是数据大小加链接大小；比较函数的第一个参数总是调用者的缓冲。
- 树挂在 `BalancedRoot` 的右边，`BalancedRoot.Parent` 指向自己；它的 `Balance` 是再平衡时向上走的哨兵：插入前置 -1、删除前置 0，
  树变深时插入把它变成 0，树变浅时删除把它变成 -1，`DepthOfTree` 随之增减。
- 有两个子节点的节点被删除时，由它较深一侧的后继（平衡时取后继）或前驱顶替。
- 删除成功、插入（包括值已存在、`InsertFull` 遇到已有节点）清掉 `GetElement` 的游标（`OrderedPointer`、`WhichOrderedElement`，
  后者是 0 起的下标）；查找、枚举、分配失败都不动它。删掉的节点若是 `RestartKey`，它退到前驱。
- `RtlEnumerateGenericTableLikeADirectory`：`DeleteCount` 与表一致且有 restart key 时从它继续（`NextFlag` 取下一个），否则按缓冲
  定位（命中时 `NextFlag` 取下一个，未命中取后面第一个）；匹配函数返回 `STATUS_NO_MATCH` 就跳过，`STATUS_SUCCESS` 交出元素，其他任何
  状态结束并返回 NULL；交出或结束时更新 restart key 与删除计数，走到尽头两者都不动，空表把 restart key 置 NULL。
- `RtlIsNameInExpression` 与 NT 文件系统的状态集算法一致，包括它的特殊之处：`<` 匹配 `a.`、`>` 匹配 `.`、奇数长度的表达式永不匹配；
  忽略大小写时只把名字大写（有表用表，否则 `RtlUpcaseUnicodeChar`），表达式须已是大写。
