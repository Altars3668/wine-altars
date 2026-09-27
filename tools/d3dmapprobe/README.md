# D3D11 非阻塞 Map 原生对照

用 `scripts/build-probe.sh tools/d3dmapprobe/d3dmap.c tools/d3dmapprobe/d3dmap.exe d3d11` 构建，`scripts/winrun.sh --desktop` 在 winref Windows build 29671 运行**同一 PE**；`d3dmap.win.txt` 留存该次原生结果。探针只创建自己的 WARP 设备、动态缓冲区及临时纹理，不读用户文件，不保存 GPU 内容。

已确定的契约：`WRITE_DISCARD`/`WRITE_NO_OVERWRITE` 搭配 `D3D11_MAP_FLAG_DO_NOT_WAIT` 返回 `E_INVALIDARG`；未知标志 bit 2 也返回 `E_INVALIDARG`，这些失败都把输出数据指针清零。空闲 staging 纹理的 `READ`、`WRITE`、`READ_WRITE` 配合 `DO_NOT_WAIT` 均能成功；GPU 复制尚未完成时可能返回 `DXGI_ERROR_WAS_STILL_DRAWING`，不得改成默默等待后返回成功。

Wine 的 D3D11 入口现在验证标志并把 `WINED3D_MAP_DONOTWAIT` 交给 wined3d；命令流在资源尚有排队访问时立即返回“仍在绘制”。同一 PE 的**确定性**输出逐行匹配 winref。连续 Copy→Map 的“busy/ready”次数受两个系统的工作线程和 GPU 时序影响，不拿一次统计强求逐行一致；Wine 实测 busy 或迅速成功，另用高精度临时探针观察到成功 Map 在受测软件后端没有长时间阻塞。

最初的专项测试为 Wine/winref 各六项、0 失败；普通 `d3d11` 全套曾在 480 秒超时，**不能**声称全套通过。后续加了 Copy→Flush→非阻塞重试→阻塞回退→内容与再次映射的回归测试；断言数随实际忙碌次数变化。本机数轮复测 GL 为 21 项、Vulkan 为 13～21 项，均 0 失败、0 跳过。winref 此次域名不可解析，新增测试**尚未在原生 Windows 重跑**，不能把之前的六项基线当作新测试结果。

`wined3d_resource_is_busy` 只判断命令流队列。新增后端路径在 GL 检查 BO 的 command fence（必要时提交、flush，再以零超时轮询），Vulkan 在 HOST_READ barrier 后提交并查询 `vkGetFenceStatus`；发现未完成时返回 `DXGI_ERROR_WAS_STILL_DRAWING`，不把失败 Map 计入映射次数或把输出指针当作有效数据。本机 Vulkan 用 `WINEDEBUG=+d3d` 实际观察到 `VK_NOT_READY` 分支；GL 已编译并通过工作流测试，但本机没有强制触发 GL GPU-fence 忙碌分支。Vulkan 记录 HOST_READ barrier 的 command-buffer id，防止快速重试不断提交新的 barrier 而永远不能完成。

**仍非全面非阻塞保证**：资源在进入 BO Map 前可能先通过 `wined3d_texture_load_location()`、缓冲区位置转移或驱动映射进行数据准备；无 GL fence 的设备也无法可靠查询 GPU 完成。尚未证明这些路径在所有 GL/Vulkan 驱动严格非阻塞，更不能将此项称为完整的 D3D11/最新 Windows 功能支持。
