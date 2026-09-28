# D3D11 逐样本着色的插值位置

`d3d11sample.c` 在 4 倍、8 倍多重采样的目标上画满屏三角形，像素着色器把一个等于屏幕 x 的属性、以及 `SV_Position.x` 的小数部分平方后输出，resolve 后读像素 5：在像素中心求值是 0.25（64），在各样本处求值是样本上的平均（4 倍 84，8 倍 85）。四种着色器：读 `SV_SampleIndex`；属性加 `sample` 修饰且读 `SV_SampleIndex`；只加 `sample` 修饰；都不加。

    scripts/build-probe.sh tools/d3d11sampleprobe/d3d11sample.c tools/d3d11sampleprobe/d3d11sample.exe d3d11 d3dcompiler_47 dxguid uuid
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh --desktop tools/d3d11sampleprobe/d3d11sample.exe

## Windows（winref，GPU）

| 着色器 | 属性 | SV_Position |
|---|---|---|
| 读 SV_SampleIndex | 64（像素中心） | 64 |
| sample 修饰 + SV_SampleIndex | 84 / 85（样本） | 64 |
| 只有 sample 修饰 | 84 / 85 | 64 |
| 逐像素 | 64 | 64 |

即：逐样本执行本身不改变插值位置，只有 `sample` 修饰才在样本处插值；`SV_Position` 始终是像素中心。

## Wine

修之前：

- GL（wined3d GLSL）：读 `SV_SampleIndex` 时普通属性在样本处插值（84/85），应为 64；centroid、sample 插值只打 FIXME。
- Vulkan（vkd3d-shader SPIR-V）：`SV_Position` 给的是样本位置（84/85），应为 64。
- vkd3d 的 HLSL 编译器不认 `sample` 修饰（`E5030: Unknown modifier "sample"`）；上游 master 也没有。
- 另外 Vulkan 下结果随着色器的创建顺序变化，一旦保留所有着色器就恢复一致：wined3d 的图形管线缓存以 `VkShaderModule`
  句柄为键，着色器销毁后句柄被复用，新着色器命中了旧管线。探针因此在最前、最后各画一次逐像素着色器。

wine-src `0eea94d`、`7a3e3e6`、`b6df687`、`fc2489e` 之后，GL 与 Vulkan 的输出都与 Windows 逐值相同。

`dxbcdump.c` 在 Windows 上用系统的编译器编出 d3d11 测试 `test_sample_interpolation` 所用着色器的字节码。
