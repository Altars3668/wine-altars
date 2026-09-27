# D3D11 非阻塞 Map 原生对照

用 `scripts/build-probe.sh tools/d3dmapprobe/d3dmap.c tools/d3dmapprobe/d3dmap.exe d3d11` 构建，`scripts/winrun.sh --desktop` 在 winref Windows build 29671 运行**同一 PE**；`d3dmap.win.txt` 留存该次原生结果。探针只创建自己的 WARP 设备、动态缓冲区及临时纹理，不读用户文件，不保存 GPU 内容。

已确定的契约：`WRITE_DISCARD`/`WRITE_NO_OVERWRITE` 搭配 `D3D11_MAP_FLAG_DO_NOT_WAIT` 返回 `E_INVALIDARG`；未知标志 bit 2 也返回 `E_INVALIDARG`，这些失败都把输出数据指针清零。空闲 staging 纹理的 `READ`、`WRITE`、`READ_WRITE` 配合 `DO_NOT_WAIT` 均能成功；GPU 复制尚未完成时可能返回 `DXGI_ERROR_WAS_STILL_DRAWING`，不得改成默默等待后返回成功。

Wine 的 D3D11 入口现在验证标志并把 `WINED3D_MAP_DONOTWAIT` 交给 wined3d；命令流在资源尚有排队访问时立即返回“仍在绘制”。同一 PE 的**确定性**输出逐行匹配 winref。连续 Copy→Map 的“busy/ready”次数受两个系统的工作线程和 GPU 时序影响，不拿一次统计强求逐行一致；Wine 实测 busy 或迅速成功，另用高精度临时探针观察到成功 Map 在受测软件后端没有长时间阻塞。

测试：`scripts/build-winetest.sh d3d11 <新目录>` 后以 `d3d11 --map-do-not-wait` 只跑本次新增的六个断言，Wine 与 winref 均 0 失败。普通 `d3d11` 测试仍包含这组断言，但一次完整单线程 Wine 套件在 480 秒超时，**不能**声称全套通过。`wined3d_resource_is_busy` 只判断命令流队列，某些硬件/驱动的 GPU fence 可能比队列存活更久；尚未完成所有 GL/Vulkan 后端的严格非阻塞保证，不能将它称为完整的 D3D11 映射支持。
