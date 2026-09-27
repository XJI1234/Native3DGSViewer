# Native3DGS

Native3DGS 是原生 3D 高斯泼溅（3D Gaussian Splatting，3DGS）渲染工程。Windows 版提供 Direct3D 12 引擎、C++ SDK 和 WinUI 3 查看器；`ForAndroid/` 提供 Vulkan 1.1 引擎、原生 C SDK 与 Kotlin AAR 技术预览。两平台读取标准二进制 3DGS PLY 与 SPZ 场景，在 GPU 上完成投影和排序。

**项目状态：**模型读取、渲染核心、引擎、测试、可安装 SDK 和 WinUI 3 桌面查看器已实现。RTX 3080 已完成主要本机验证；Windows 11 23H2 的 Intel Arc 核显已实机打开 SPZ/PLY 并正常浏览。与 Spark 的图像一致性、完整查看器性能及其他 GPU 厂商的验收仍待完成，详见[验证记录](docs/engine-sdk-verification.md)和[桌面验证记录](docs/desktop-viewer-verification.md)。

当前发行说明见 [0.2.0](docs/release-0.2.0.md)；本轮资源与 Android 集成经验见[开发记忆](docs/development-memory.md)。

## 功能

- 支持标准二进制 3DGS PLY、SPZ v1-v4、0-3 阶球谐系数（SH），统一产出不可变场景数据。
- 解码在隔离的辅助进程中执行，包含格式校验、资源限制、进度、取消和结构化错误。
- 基于 D3D12 完成投影、可见性筛选、稳定的 GPU 基数排序、高斯光栅化、SH 着色及预乘 alpha 合成。
- 双精度轨道与自由飞行相机，支持平移、缩放、适配场景和重置视角。
- 支持异步加载与上传、成功后替换场景、有界事件队列、渲染统计及由宿主协同完成的设备丢失恢复。
- 提供可重定位的 CMake SDK 包，包含静态库、公共头文件、预编译着色器、解码辅助程序、示例宿主和第三方许可声明。

当前 SDK 和查看器一次显示一个本地场景。压缩 PLY 变体、SPLAT/KSPLAT/SOG、LoD、多模型、编辑、动画及远程加载属于后续工作，见[技术开发计划](docs/technical-development-plan.md)。

## 架构与技术栈

```text
PLY / SPZ 文件
    -> model-io 辅助进程：识别、解码、校验
    -> 不可变 SplatScene
    -> engine：异步请求、相机、上传、渲染表面生命周期
    -> render-core：D3D12 上传、投影/裁剪、GPU 排序、绘制、Present
    -> 宿主拥有的 DXGI 合成交换链
```

| 组件 | 技术与职责 |
| --- | --- |
| `splat-types` | C++20 不可变场景数据和共享契约，位于 `include/splat-types/`。 |
| `model-io` | PLY 按块读取并规范化，SPZ 使用 [Niantic SPZ](https://github.com/nianticlabs/spz)、zlib 和 zstd；解码运行于受资源限制的辅助进程，不依赖 D3D12。 |
| `render-core` | 使用 Direct3D 12、DXGI、HLSL Shader Model 6.0、Windows SDK DXC 和 [FidelityFX Parallel Sort](https://github.com/GPUOpen-Effects/FidelityFX-ParallelSort)，不依赖文件解码器或 WinUI。 |
| `native3dgs-engine` | 基于 C++20 协调相机、工作线程、请求事务、状态快照、事件和设备恢复。 |
| SDK 与测试 | 使用 CMake/CPack、MSVC、GoogleTest、独立 SDK 消费工程及 GPU 图像和生命周期测试。 |

引擎分别使用加载线程和渲染线程。宿主负责界面及 DXGI 合成交换链，并使用引擎提供的命令队列创建交换链。公共入口包括 `gs::engine::create_engine`、`gs::render::create_renderer` 和 `gs::io::make_model_loader`；一般应用建议从引擎 API 开始集成。[SDK 集成指南](docs/SDK-guide.md)说明命令队列所有权、绑定与解绑、调整尺寸、关闭和设备恢复流程。

## 开发环境

- Windows 11 x64；硬件 D3D12 适配器须支持 Feature Level 12.0、Shader Model 6.0 和 WaveOps。渲染器没有软件回退路径。
- Visual Studio 2026，安装 **Desktop development with C++** 工作负载（MSVC 19.50）。Release 静态库使用动态 MSVC 运行库（`/MD`）；SDK 消费工程需要匹配的编译器及 VC 运行库。
- 从源码编译 GUI 还需安装 Visual Studio 的 **WinUI 应用开发**组件；安装包用户无需 Visual Studio。
- Windows SDK **10.0.26100.0**，包含 `dxc.exe`。当前 CMake 配置显式引用该版本的 DXC 路径。
- 推荐 CMake 4.3 及以上版本（已验证 4.3.2；`CMakeLists.txt` 声明的最低版本为 3.30），另需 Git 和 PowerShell。构建使用 `Visual Studio 18 2026` 生成器。
- 首次递归获取 Git 子模块需要网络连接。依赖的固定提交与许可证见 [third_party/README.md](third_party/README.md)。

Windows 工程面向 x64；Android SDK 面向 Android 10+ `arm64-v8a`。Windows 已测试 RTX 3080 和 Intel Arc 核显；Android 已在报告 Android 15、`x86_64`、模拟 Adreno 640 的设备上通过模块与仪器测试，arm64 真机画质、热稳定性和性能仍待验证。

## Android 引擎与 SDK

Android 实现、七模块规格、任务清单和构建命令见 [ForAndroid/README.md](ForAndroid/README.md)。[Android SDK 接入指南](ForAndroid/docs/SDK-guide.md)说明系统文档选择器、隔离解码 Service、`SurfaceView` 生命周期、Kotlin 状态流和原生 C ABI。当前发行包为 `native3dgs-android-0.2.0.zip`，包含 AAR、`arm64-v8a` 原生库、公共头、SPIR-V、示例、文档、许可证和哈希清单；实际查看器 App 尚未实现。验证与限制见[Android 引擎记录](ForAndroid/docs/verification/engine-2026-09-27.md)。

## 从源码构建

在 PowerShell 中执行以下命令。已有仓库的开发者应在配置前运行 `git submodule update --init --recursive`。

```powershell
git clone --recurse-submodules https://github.com/XJI1234/Native3DGSViewer.git
cd Native3DGSViewer
cmake -S . -B out/cmake -G "Visual Studio 18 2026" -A x64
cmake --build out/cmake --config Release --parallel 8
```

可执行文件、解码辅助程序和编译后的着色器位于 `out/Release/`。也可以在 Visual Studio 中打开 `Native3DGSViewer.sln`，或使用 MSBuild 构建：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
```

该解决方案调用相同的 CMake 构建流程，并编译 `out/Release/Native3DGSViewer.GUI.exe`。

## 桌面查看器

构建后启动 `out/Release/Native3DGSViewer.GUI.exe`，或运行 `packaging/build-installer.ps1 -SkipBuild`（需要 Inno Setup 6）生成 `out/installer/Native3DGSViewer-Setup-x64.exe`。安装包是当前用户自包含安装，带有运行库、解码辅助程序、shader 和第三方许可文件。打开窗口后选择一个 PLY/SPZ 文件，或将文件拖入视口；也可传入单个文件路径作为启动参数。

- 固定模式：按住左键拖动，模型沿鼠标方向旋转；右键平移，滚轮缩放；“适配”和“重置”恢复模型视角。
- 自由模式：点击视口并移动鼠标转向；W/A/S/D 移动，Q/E 下降/上升，Shift 加速，Esc 退出捕获。
- “翻转 Y”镜像显示的 Y 坐标；再次点击恢复。此操作不修改模型文件，加载新模型时保持当前翻转设置。SDK 另提供 X、Y、Z 三轴独立翻转命令。
- 打开新模型时旧模型保持可见；可取消加载或关闭当前模型。

GUI 的 SDK 调用顺序与宿主生命周期见 [GUI 例程指南](GUI/README.md)。

## 运行示例宿主

先在本地安装 SDK，再编译独立消费示例，最后传入一个标准 3DGS PLY 或 SPZ 文件：

```powershell
cmake --install out/cmake --config Release --prefix out/sdk --component SDK
cmake -S out/sdk/examples/sdk-host -B out/sdk-consumer -G "Visual Studio 18 2026" -A x64 "-DCMAKE_PREFIX_PATH=$((Resolve-Path out/sdk).Path)"
cmake --build out/sdk-consumer --config Release
$scene = 'C:\models\example.ply'  # 改为已有的 PLY 或 SPZ 文件路径。
& .\out\sdk-consumer\Release\sdk-host.exe $scene
```

示例宿主创建 DXGI 合成交换链、加载场景并渲染帧，确认 splat 已进入 GPU 后退出。成功时会输出 `SDK OK frames=... active=... splats=...`。它没有可见视口；桌面应用需要将交换链绑定到自己的界面表面。源码位于 [examples/sdk-host](examples/sdk-host)。

部分测试使用的 `1.ply` 和 SPZ 参考场景**不随仓库分发**。运行完整的已安装 SDK 测试时，请将兼容的 PLY 放在 `..\1.ply`；运行上述示例则可使用自己的文件。参考样本的哈希见[模型读取验证记录](docs/model-io-verification.md)。

## 测试与基准

常规测试分为模型读取、渲染核心、模型与渲染联合、引擎及已安装 SDK 五组：

```powershell
# 不准备外部 ../1.ply 时，运行其余四组。
ctest --test-dir out/cmake -C Release --output-on-failure -E InstalledSDK

# 准备好 ../1.ply 中的标准 3DGS PLY 后，运行全部五组。
ctest --test-dir out/cmake -C Release --output-on-failure
```

渲染和引擎测试需要上述 D3D12 硬件。缺少外部样本时，前四组中的部分真实场景用例可能跳过；父级工作区若存在其他 SPZ 参考文件，模型读取测试也会使用它们。通过 Visual Studio 测试桥运行时执行：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
```

GPU 阶段诊断可使用独立的 Release 基准程序；这些结果不能代替与现有 Viewer 的等画质整机对比：

```powershell
$scene = 'C:\models\example.ply'  # 改为已有的 PLY 或 SPZ 文件路径。
& .\out\Release\Native3DGSViewer.SortBench.exe 8000000 100
& .\out\Release\Native3DGSViewer.SceneBench.exe $scene load 1
& .\out\Release\Native3DGSViewer.SceneBench.exe $scene smoke 1
& .\out\Release\Native3DGSViewer.SceneBench.exe $scene cached 300
& .\out\Release\Native3DGSViewer.SceneBench.exe $scene force 300
```

硬件、样本哈希、测试证据和基准条件见[引擎验证记录](docs/engine-sdk-verification.md)及[渲染核心验证记录](docs/render-core-verification.md)。
本次大场景加载、显存预算和单帧实测见[大场景验证记录](docs/large-scene-verification.md)。

## 在其他工程中使用 SDK

使用匹配的 MSVC 工具链创建 Release x64 CMake 工程，将 `CMAKE_PREFIX_PATH` 指向已安装的 SDK（或解压后的 SDK ZIP）。在工程中配置目标的最小示例如下：

```cmake
find_package(Native3DGS 0.2 CONFIG REQUIRED)
add_executable(my-viewer main.cpp)
target_compile_features(my-viewer PRIVATE cxx_std_20)
target_link_libraries(my-viewer PRIVATE Native3DGS::Engine dxgi d3d12)
native3dgs_deploy_runtime(my-viewer)
```

`native3dgs_deploy_runtime` 会把 `model-io-helper.exe` 和预编译着色器复制到宿主可执行文件旁。自行管理加载和渲染循环的宿主也可使用导出的 `Native3DGS::ModelIo` 与 `Native3DGS::RenderCore`。SDK 包可重定位，但不承诺跨编译器的 C++ STL 二进制 ABI。完整宿主生命周期见 [SDK 集成指南](docs/SDK-guide.md)，公共接口契约见[引擎与 SDK 规格](docs/SPEC-engine-sdk.md)。

生成可分发的 ZIP：

```powershell
cpack --config out/cmake/CPackConfig.cmake -C Release -B out/packages
```

产物为 `out/packages/Native3DGS-SDK-0.2.0-windows-x64-SDK.zip`，包含完整 `docs/`、GUI 宿主说明、公共头、库、着色器、辅助程序及许可声明。[独立 SDK 消费测试](tests/sdk/installed-sdk.ps1)可用场景文件验证解包、重定位、构建和运行。

## 修改源码

| 修改范围 | 从这里开始 | 契约与测试 |
| --- | --- | --- |
| 场景类型与解码 | `include/splat-types/`、`include/model-io/`、`src/model-io/` | [模型读取规格](docs/SPEC-model-io.md)、`tests/model-io/` |
| D3D12 设备、上传、排序与绘制 | `include/render-core/`、`src/render-core/`、`shaders/` | [渲染核心规格](docs/SPEC-render-core.md)、`tests/render-core/` |
| 相机与异步引擎 | `include/native3dgs/`、`src/engine/` | [引擎与 SDK 规格](docs/SPEC-engine-sdk.md)、`tests/engine/` |
| 打包与宿主示例 | `CMakeLists.txt`、`cmake/`、`examples/sdk-host/` | `tests/sdk/`、[SDK 集成指南](docs/SDK-guide.md) |

修改时应保持上述依赖边界。变更公共契约前先更新所属模块规格；修改着色器缓冲区布局时同步修改宿主侧定义。行为变更应添加对应的 GoogleTest 用例，先运行相关 CTest 分组，再运行全套测试。评估性能时应保持分辨率、画质设置、场景、相机路径和 GPU 一致。[AGENTS.md](AGENTS.md)记录仓库约定，[third_party/README.md](third_party/README.md)记录依赖版本与许可。

## 常见问题

- **CMake 提示缺少依赖源码：**运行 `git submodule update --init --recursive` 后重新配置，确认各子模块包含固定版本的源文件。
- **CMake 找不到 DXC：**安装含 DirectX Shader Compiler 的 Windows SDK 10.0.26100.0。当前构建检查 `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe`。
- **设备创建或 GPU 测试失败：**检查显卡与驱动是否支持 Feature Level 12.0、Shader Model 6.0 和 WaveOps；不支持软件适配器。
- **已安装 SDK 测试提示缺少场景：**在 `..\1.ply` 放置标准二进制 3DGS PLY，或按上文使用 CTest 的 `-E InstalledSDK` 选项先测试其余分组。
- **SDK 宿主无法解码或渲染：**在 CMake 目标上调用 `native3dgs_deploy_runtime`，确认可执行文件旁有 `model-io-helper.exe` 和 `shaders/`，并使用匹配的 MSVC 运行库。

## 已知限制与后续计划

- 桌面安装包已在第二台 Windows 11 23H2 / Intel Arc 机器打开两种模型；未完成完整的干净系统依赖矩阵验收，安装包尚未签名。
- 尚未完成固定相机 Spark 图像对比（SSIM）、等画质 PresentMon 测量、长期稳定性及 AMD/Intel 的完整兼容性矩阵。
- 当前基准只测得 RTX 3080 上的 GPU 阶段耗时，不能证明相对于现有 Viewer 达到了规划中的 20% 性能提升。
- 后续格式、多模型渲染、LoD、编辑和动画均需另立契约并补充测试。

[技术开发计划](docs/technical-development-plan.md)和[系统技术设计](docs/system-technical-design.md)列出了后续里程碑及验收条件。[兼容性调试经验](docs/compatibility-lessons.md)记录 Intel Arc、UMA、wave8 和镜像输入的排查结论。

## 参与贡献

提交拉取请求前，请将改动限定在所属模块；公共契约变化需同步更新规格，行为变化需附测试，并列出实际运行的构建与测试命令。渲染改动应提供固定相机图像证据或等画质基准数据。命名、测试和提交约定见 [AGENTS.md](AGENTS.md)。

## 许可证

仓库根目录目前没有声明项目许可证的 `LICENSE` 文件。不能根据第三方依赖的许可证推定本项目源码或 SDK 的再分发权限。第三方组件保留各自许可证，详见 [third_party/README.md](third_party/README.md)；SDK 包中也包含相应许可文件。
