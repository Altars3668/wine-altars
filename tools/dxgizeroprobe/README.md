# 客户区为空时交换链的尺寸与 Present：`dxgizeroprobe.exe`

为 0×0 的无边框弹出窗口、最小化的重叠窗口，以及事后被改成 0×0、100×0、0×100 的窗口建 DXGI 交换链（宽高传 0），
每一步调用 `ResizeBuffers(0, 0)` 并 `Present`，打印 `GetDesc1` 给出的缓冲区尺寸、客户区尺寸与返回值。依次用
Direct3D 11 的 flip 模型（`FLIP_DISCARD`）、blit 模型（`DISCARD`）和 Direct3D 12 队列的 flip 模型各跑一遍；最后对
Direct3D 9 问同样的问题：窗口客户区为空时 `CreateDevice` 与 `Reset(0, 0)` 给出什么尺寸。

`results/dxgizeroprobe.win.txt`（Windows 11 build 29671，桌面会话）的要点：

- **DXGI**：创建与 `ResizeBuffers` 在宽或高为 0 时取客户区的对应维度，客户区缺的那一维按 **8** 像素：0×0→8×8、
  100×0→100×8、0×100→8×100。三种交换链一致，都返回 S_OK。
- **Present**：flip 模型对最小化或客户区为空的窗口照常返回 S_OK；blit 模型只要客户区为空就返回
  `DXGI_STATUS_OCCLUDED`（0x087a0001），**不论是否最小化**。
- **Direct3D 9** 不补 8：窗口客户区为空而后备缓冲区宽高传 0 时，`CreateDevice` 与 `Reset` 都返回 E_INVALIDARG，
  并把客户区尺寸（如 100×0）写回呈现参数；`Reset` 失败后 `Present` 返回 D3DERR_INVALIDCALL。显式给出尺寸
  （0×0 窗口上 64×64）则正常创建。

改动前的 Wine：`ResizeBuffers(0, 0)` 在空客户区上返回 D3DERR_INVALIDCALL（Direct3D 12 为 DXGI_ERROR_INVALID_CALL），
交换链尺寸变成 0；flip 模型在最小化窗口上 `Present` 也返回 OCCLUDED，blit 模型在未最小化的空客户区上却返回 S_OK。
dxgi 的提交 `a32fc1b75c9` 之后 DXGI 部分与 Windows 逐行一致（`results/dxgizeroprobe.wine.txt`；"restored" 一行的
客户区尺寸因窗口边框度量不同而不同）。Direct3D 9 仍是上游原样：创建时补 8×8，`Reset` 返回 D3DERR_INVALIDCALL 且不写回
参数、之后 `Present` 返回 D3DERR_DEVICELOST。它只影响 d3d9，把宽松改严只会让现在能跑的程序失败，暂不动。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror dxgizeroprobe.c -o dxgizeroprobe.exe -ld3d11 -ldxgi
-ld3d9 -ld3d12 -luuid`。在 Windows 上要用 `scripts/winrun.sh --desktop` 在已登录用户的桌面里跑：服务会话里没有 DWM，
遮挡与 Present 的结果不代表桌面。
