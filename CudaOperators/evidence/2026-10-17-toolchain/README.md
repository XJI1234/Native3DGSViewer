# CUDA 13.3 工具链与互操作验证

2026-10-17，用户已授权安装并执行。使用NVIDIA官方13.3.0 redist清单，组件ZIP逐一核对官方SHA-256，安装收据与文件列表见 `install-receipt.json`；下载缓存位于忽略的 `out/cuda-downloads`，未修改显卡驱动、系统PATH或原CUDA11.8。

安装根：`C:\Users\21544\AppData\Local\Native3DGSViewer\toolchains\cuda\13.3.0`。包含nvcc13.3.33、CRT13.3.33、cudart13.3.29、CCCL13.3.3.3.1、libnvvm13.3.33、nvdisasm/cuobjdump13.3.29及Compute Sanitizer13.3.27。各组件LICENSE单独保存在安装根licenses目录。标准Program Files目录无写权限，未尝试权限绕过。

主机编译器MSVC19.50（VS2026，工具集14.50.35717），CMake4.3.2/Ninja，CUDA C++20、SM86。`host_config.h`明确允许MSC_VER1920–1959；真实编译和RTX3080运行已通过。CCCL首次编译要求标准预处理器，已通过正常`/Zc:preprocessor`选项解决，UTF-8通过`/utf-8`指定；未绕过版本检查或修改vendor。

复现（仓库根）：

```powershell
python CudaOperators/tools/install-cuda-toolchain.py
pwsh -NoProfile -File CudaOperators/tools/build.ps1 -Configure -Targets Native3DGSViewer.CudaProbe,Native3DGSViewer.CudaTests
ctest --test-dir out/cmake-cuda -C Release --output-on-failure -R '^Cuda'
```

脚本只设置其自身进程环境，显式指定nvcc/Toolkit，不依赖PATH中的CUDA11.8。CUDA实验目标输出到 `out/cmake-cuda/cuda/Release`。

已通过：SM86整数运算、CUB32项逆序SortKeys；按LUID选择设备；CUDA写513个共享uint再由D3D12逐项读回；错误LUID/零容量/越界拒绝；10000次双向external semaphore/fence交接（最终序列9999）；17×9共享RGBA8纹理经CUDA surface写入、D3D12按padding读回。D3D12 Debug Layer未报错。

`texture-test.log`记录CudaToolchain/CudaInterop两组通过，后者含四项测试。RED编译与修复构建日志均保留。故障设备移除、无法完成fence、长稳、整帧互操作性能仍待后续验收，当前通过不代表这些项已测。

头文件CUDART_VERSION=13030；运行时版本查询由本机驱动报告13040，构建链接路径实际固定为本安装根的cudart_static.lib。保留两者，不将驱动报告值称为已安装13.4开发工具链。
