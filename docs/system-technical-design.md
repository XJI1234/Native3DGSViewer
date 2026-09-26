# Windows 原生 3DGS 查看器系统技术实现设计

状态：引擎、SDK 与桌面基础集成已实现，外部验收待完成；2026-09-26。本文件整合[总技术计划](technical-development-plan.md)、[model-io](SPEC-model-io.md)、[render-core](SPEC-render-core.md)、[engine/SDK](SPEC-engine-sdk.md)及[desktop-viewer](SPEC-desktop-viewer.md)。依赖方向为 shared types → model-io/render-core → engine → desktop host。模块、SDK 消费和 GUI 本机测试见[引擎验证记录](engine-sdk-verification.md)与[桌面查看器验证记录](desktop-viewer-verification.md)；Spark 画质对照和整机性能基准仍待实施。

## 1. 产品边界和验收总则

首期交付 Windows 11 x64 原生离线单模型查看器：本地标准 Graphdeco 3DGS PLY 与 SPZ v1-v4 基础文件、SH 0-3 阶、鼠标轨道/平移/滚轮、键鼠自由飞行、适配及重置视角、真实进度/取消/错误。标准 PLY 仅限已明确支持的二进制小端布局；压缩 PLY、SPLAT/KSPLAT/SOG、SPZ antialiased 扩展、远程文件、多模型、LoD、编辑、动画、VR 不在首期交付范围。长期能力顺序遵循总计划 F1-F5。

“与 Spark 2.0 功能一致”在长期路线中指 Windows 适用能力逐项经独立验收，不是 JavaScript API 的机械复刻。Spark 2.0 源码/文档提供行为参照；同机现有 Spark 2.1 + Electron Viewer 提供性能基线。根目录与 Rust 工作区的许可标记不一致，未经许可核实不复制 Spark Rust 实现。首期达标同时要求图像、稳定性与性能，不能用降低 SH、splat 数、分辨率、截断半径或关闭排序换速度。

## 2. 能力图与依赖

| 模块 ID | 唯一职责 | 公共提供 | 依赖 |
|---|---|---|---|
| `splat-types` | 不可变规范化场景和公共数学值类型 | `SplatScene`、`SourceFormat`、`SceneHandle`、只读数组 | C++20 标准库 |
| `model-io` | 输入识别、隔离解码、验证和取消 | `IModelLoader::load`、`SceneHandle`、结构化错误/进度 | `splat-types` |
| `render-core` | D3D12 设备、上传、排序、合成、统计/恢复 | `IRenderer`、ticket、事件、相机/质量配置 | `splat-types`、Windows 图形 API |
| `engine` | 异步请求、相机、渲染线程和 surface 生命周期 | `gs::engine::IEngine` | `model-io`、`render-core` |
| `desktop-viewer` | WinUI 窗口、文件入口、输入及可见状态 | 用户操作与 SDK 宿主示例 | `engine` |

依赖方向单向：`splat-types` 先于两个底层模块；桌面层最后集成。`model-io` 不调用 D3D12，`render-core` 不知道 PLY/SPZ 或 WinUI，桌面层不接触逐点渲染数据。模块边界以三份规格的公共契约为准，本文定义跨模块时序和交付机制；有冲突时先修订提供者规格和消费者契约测试，再实施。

```text
本地文件 -> desktop-viewer -> IEngine::open -> model-io helper 进程
                                 | SceneHandle
                                 v
                         engine -> render-core -> D3D12 queues
                            | snapshot/events              |
                            v                              v
                     desktop-viewer <- SwapChainPanel <- composition swapchain
```

辅助解码进程由 `model-io` 创建，使用 Job Object 限制内存与收尾；它是崩溃/资源隔离，不声称是恶意文件安全沙箱。宿主为 WinUI 3 单进程，内含 UI STA 线程、协调器工作线程与渲染线程。无需本地服务或联网。第三方源码及 zlib/ZSTD 的版本、补丁与许可证在 `third_party/manifest` 固定，发布包带许可清单。工程内允许 renderer 的 private 头引用 D3D12；公共边界尽量用值类型与 opaque handle，唯一 Win32 图形交界是 queue/swapchain COM 句柄。

## 3. 进程和线程生命周期

启动顺序：加载配置和日志 -> WinUI 线程创建窗口/空视口 -> 渲染线程枚举适配器/创建 D3D12 设备与 direct/copy queue -> 桌面宿主以 renderer direct queue 创建 DXGI composition swapchain -> UI 线程 `SetSwapChain` -> renderer attach/back buffer 初始化 -> 空场景可呈现 -> 开放“打开文件”。设备初始化失败时保留 WinUI 窗口和错误操作，不因 GPU 不支持直接无提示退出。M0 三角形原型确定 swapchain 格式、alpha、buffer count、DPI 矩阵及 WinUI 绑定/解绑顺序；这些参数以实测记录，不能照抄 WebGL 设置。

运行期 UI 线程只处理视图、输入及短时状态更新；解码 helper 可能一次独占数 GiB 内存，决不与 UI 同线程；渲染线程只提交 GPU 命令，禁止回调 UI 控件。renderer 创建时接收经校验的固定 `QualityConfig` 和事件接收器，异步错误通过事件到协调器；输入错误在命令入口直接返回。跨线程消息包含 `RequestId`、`UploadTicket`、surface generation 和 viewport revision；相机命令携带活动 ticket，resize/交换链操作携带 generation，resize 还携带同一 surface 内的递增 revision。回调和事件按标识过滤，避免迟到事件覆盖新场景或新尺寸。相机命令采用覆盖最新值策略；资源/错误事件必须可靠投递，不得被相机更新挤出队列。

正常关停顺序：窗口进入 Closing 并禁用新请求 -> 停止输入、解除鼠标捕获 -> 协调器请求解码取消/渲染上传取消 -> helper 退出并关闭 Job -> renderer 停止新帧、对已提交 fence 做有界等待并完成对应 generation 的 `detach_swapchain` -> UI 线程解除 `SwapChainPanel` 绑定 -> 释放宿主交换链引用、队列和设备 -> 释放场景映射/日志。若已发生设备移除或 fence 等待超时，转设备故障路径并记录 DRED/诊断，不能继续等待不可能完成的 fence 或调用正常 `ResizeBuffers`；随后进行受控资源释放和进程关停。具体 COM 解绑顺序在 M0 interop 原型中验证；UI 不执行同步等待。

## 4. 打开与替换模型的事务

1. UI 收到文件选择/拖入，分配新的 `RequestId`；协调器只保留一个待启动的新路径，取消旧待命请求。当前活动场景与相机继续显示。
2. `model-io` 用 `CreateFileW` 对本地常规文件完成限制预检，在受限 helper 中解码、标准化并校验；只返回完整只读 `SceneHandle`。错误/取消直接结束本次事务。
3. 桌面相机控制器依据 `SplatScene::bounds`、`maxScale * max_stddev` 的保守支撑扩展量及视口物理纵横比计算初始 fit 视角，连同句柄调用 `render-core::upload_scene`。此时旧视角继续控制旧场景，初始视角只属于待命模型。
4. renderer 按 DXGI `Budget - CurrentUsage` 核算新模型和临时缓冲的增量显存，不重复计入已存在的旧场景；`model-io` 同时核算宿主共享映射及 helper 的 CPU 内存峰值。准入后用 copy queue 分块上传，等待 copy fence，生成待命模型自己的排序结果与首次画面；旧场景在整个阶段可绘制。无预算、shader 或设备错误时回滚待命资源并保留旧场景。
5. 在帧边界把待命 GPU 场景和初始相机绑定为一个版本，完成一次成功 Present 后发 `SceneReady(ticket)`；UI 这时才改显示的模型名、重置视角基线和 `Ready`。如果该 Present 失败，按 surface/device 错误处理，保留可恢复的 CPU 快照，不能宣告成功。旧 GPU 资源在最后使用它的 direct fence 完成后释放。

取消上传只能阻止未提交 GPU 工作；已提交命令标记 abandoned 并在 fence 完成后回收。故障或取消绝不向用户显示半场景。若新旧模型并存超过 CPU 或 DXGI 当前预算，直接拒绝新请求并保留旧模型；用户可主动执行“关闭当前模型”，等待 `SceneCleared` 释放旧模型后重试。首期不自动卸载旧模型以省内存，因为这破坏失败回退语义。无模型的首次打开使用相同路径，只是回滚后显示空视口。窗口不可见或 0x0 时待命场景停在等待首次 Present 的状态，恢复视口后完成事务，期间仍可取消。

## 5. 数据、画质与性能契约

`model-io` 统一 RUB、double 世界原点 + float32 局部坐标、中心包围盒/最大尺度、xyzw 四元数、正尺度、opacity、训练色值域的 `rgb0` 和实际 SH 阶数。renderer 以 CPU double 差值生成相机相对 float 坐标，D3D 投影 `[0,1]` 深度并统一执行已锁定的颜色转换；桌面层不改动模型数据或轴向契约。GUI 的“翻转 Y”是仅供浏览的显示镜像；SDK 提供 X、Y、Z 三轴独立镜像命令。格式源坐标、SH 顺序/符号、颜色空间和截图相机均建立小样本测试。对不能确定坐标约定的 PLY，由用户/配置显式选择，默认 RDF；不猜测。

质量配置须记录排序模式、SH 上限、Gaussian 截断范围、最小 alpha、协方差 blur、透明混合、色彩空间、实际物理像素与 GPU shader hash。首期默认 GPU 径向排序，与 Spark 2.0 默认行为对齐；保留视深度诊断选项。等画质前须实测现有 Viewer 的实际参数和版本，不能仅看 Spark 2.0 默认值。GPU 每帧可见集、radix sort、椭圆投影、SH 着色及 alpha 混合不得读回 CPU 排序数据。着色器优化先过固定视角截图，再进入性能验收。

性能测试在 RTX 3080、同一驱动/电源设置/文件哈希、1920x1080 物理像素、按时间驱动的同一相机轨迹、同一排序模式/SH/截断/颜色空间条件下尽可能关闭 VSync 和帧率上限。M0 用 PresentMon 验证两端实际呈现节奏；若被 DWM、WinUI composition 或 Electron 限帧，标记该场景“呈现受限、不可判定 20% 目标”，不得把 16.7 ms 等刷新周期当作渲染耗时。可用 GPU timestamp/WebGL timer query 与 CPU 提交时间诊断瓶颈，但不得用它们替代用户可见帧时间验收，须先建立双方可比且未受限的测量条件。小/中/大样本分别用工作区 SPZ/PLY 与大 SPZ；30 秒预热，60 秒采样，至少三次。每个可比场景要求原生中位帧时间 <= Viewer 的 0.80 倍，且 1% low FPS 不低于 Viewer。图像 SSIM >= 0.95 之外，还要对合成透明/SH/边缘夹具逐像素比对并检查误差热图与局部差异；若局部错误明显，即使 SSIM 达标也不计入性能胜出。指标原始 CSV、截图、GPU timestamp、PresentMon/PIX、内存与驱动信息一并归档。设备兼容性扩展到其他 NVIDIA/AMD/Intel 后实测，未取得设备时标“未测试”。

## 6. 故障处理矩阵

| 故障 | 检出层 | 清理与可见结果 | 恢复条件 |
|---|---|---|---|
| 不支持/损坏/截断输入 | `model-io` | helper 与共享映射释放；旧场景保持，UI 显示稳定错误码 | 立即可打开其他文件 |
| 解码超时、取消、崩溃或资源限额 | `model-io` | 终止对应 Job；不发布场景；UI 有取消中/失败终态 | helper 确认退出后处理最新请求 |
| CPU 内存/共享映射不足 | `model-io` | 受检预算拒绝，不缩减 SH/点数 | 释放资源后重新尝试 |
| DXGI 预算变化、GPU 分配/上传失败 | `render-core` | 待命资源 fence 后清理；旧画面仍可用 | 释放外部 GPU 占用或选择小文件 |
| 新旧模型并存超出 RAM/显存预算 | `model-io` 或 `render-core` | 拒绝待命模型，旧画面保持；显示增量需求和余量 | 用户主动关闭旧模型，等待 `SceneCleared` 后重试 |
| 窗口 0x0、DPI/resize、surface 丢失 | desktop + renderer | 暂停 Present 或重建尺寸相关资源；UI 不冻结 | 有效 surface/尺寸恢复 |
| D3D12 device removed | `render-core` + desktop | 停帧、DRED/原因日志；不等待旧 fence。DeviceLost 后宿主暂停 render_frame，UI 解绑并释放旧 swapchain/queue/device；继续渲染调用重建同 LUID 设备并发 `SurfaceRebindRequired`，宿主重绑，renderer 重传活动场景 | 首次 Present 成功后 Ready，超时/失败进入持续错误态 |
| 迟到回调、UI dispatcher 关闭 | desktop | 按代际丢弃、释放资源；不访问销毁的控件 | 无需重启 |

错误码是程序判断依据，诊断字符串只用于限长本地日志；默认不存模型数据或完整路径。进度只报告可证实的阶段/字节，不捏造百分比。系统崩溃转储与日志收集须经用户主动导出，发布版默认本地存储并轮转。对意外 HRESULT、异常与 shader 编译失败提供带阶段的 `InternalFailure`，不能静默吞掉或无限 retry。

## 7. 工程组织与构建交付

```text
Native3DGSViewer/
  include/splat-types/             跨模块只读数据契约
  include/model-io/                加载公共接口
  include/render-core/             渲染公共接口
  src/model-io/client,helper,codecs,normalize/
  src/render-core/device,scene,passes/
  GUI/                             WinUI 3 宿主、协调器与相机
  packaging/                       Inno Setup 安装包脚本
  shaders/                         HLSL 与编译清单
  tests/model-io,render-core,desktop-viewer/
  bench/                            固定相机路径与采样脚本
  third_party/                      版本、补丁和许可清单
  docs/                             技术计划、设计和证据
```

VS 2026 C++20 x64 解决方案含 `splat-types`、`model-io` 客户端/辅助进程、`render-core`、WinUI 3 应用、GoogleTest 测试目标。Windows App SDK、Windows SDK/DXC、miniply、Niantic SPZ v3.0.0、zlib/ZSTD、GoogleTest 及可选 D3D12MA 均锁定可复现版本与许可。依赖升级需重跑损坏输入语料、设备恢复、图像和性能回归。发布候选使用 Release 和锁定 shader 编译产物。MSBuild 生成未打包、自包含的 x64 应用与 helper；Inno Setup 6 将运行时、VC CRT、shader 和许可打入安装包。干净 Windows 11 x64 机器的安装验证仍待完成，签名发布另立发布规格。

以下命令可构建、测试并打包当前应用，程序位于 `out/Release`：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
ctest --test-dir out/cmake -C Release --output-on-failure
& .\packaging\build-installer.ps1 -SkipBuild
```

构建、测试、安装与操作命令见根目录 [README](../README.md) 和 [GUI 例程说明](../GUI/README.md)。跨 DPI、设备 feature floor 的更多硬件验证见桌面查看器验证记录。

## 8. 分阶段可运行检查点

| 阶段 | 可运行成果 | 退出条件 |
|---|---|---|
| M0 规格/基线 | WinUI + D3D12 三角形、可 resize/跨 DPI 的空视口；Viewer 基准脚本 | 交换链参数、工具链/许可、画质参数和相机轨迹记录完成 |
| M1 正确显示 | `model-io` PLY -> 单场景 GPU 上传 -> 排序/SH/alpha 显示 | 固定截图与 CPU/GPU 数学测试通过，约 118 万点连续浏览 |
| M2 首期体验 | SPZ、取消/进度/拖放/Picker、轨道/飞行、Fit/Reset、主动关闭模型 | 每个首期操作可复现，失败/取消保持旧场景，关闭后可从空场景重试大模型 |
| M3 性能 | GPU 分段计时、热点优化、等画质基准报告 | 每个可比场景达到中位帧时间和 1% low 双门槛 |
| M4 交付 | 长时与故障注入、干净机器 ZIP、许可/诊断包 | 资源无持续累积，设备丢失/显存不足有明确结果 |

实施顺序按模块契约和最短可运行切片执行：先设备+交换链三角形，再小 PLY/0 阶颜色，然后完整 SH/透明/GPU 排序、SPZ 与异步事务，最后交互、性能和包装。每阶段保存输入哈希、构建命令、截图及统计证据。GPU radix、WinUI/DXGI interop 与第三方解码内存峰值是最早应验证的三个风险；任何核心路线变更先更新模块规格及其验收条件。总计划的人周估计和 20% 缓冲仍有效，不把本文件当作工期承诺。

## 9. 规格维护与后续接口

`model-io` 的内部 codec 注册表支持 F1 新格式，但公开 DLL 插件 ABI 尚未承诺。F2 多模型要求跨模型同一排序域、场景图与实例变换，不能把首期“一个 SceneHandle 等于一个可见模型”的假设直接复制。F3 RAD/LoD 需页级加载、residency 与预算协议，可能新增流式接口；F4 多视角需要每视角独立可见集/排序；F5 编辑和动画需要 GPU 修改与撤销/持久化规格。每阶段先独立立规格，再审查对三份首期接口的向后兼容性。Windows 不适用的 WebXR/移动触控不作为等价验收项。

资料基线：[Spark 2.0 新能力](../../spark-2.0.0/docs/docs/new-features-2.0.md)、[SparkRenderer 源码](../../spark-2.0.0/src/SparkRenderer.ts)、[现有 Viewer](../../Viewer/README.md)、[D3D12 指南](https://learn.microsoft.com/windows/win32/direct3d12/directx-12-programming-guide)、[SwapChainPanel native 绑定](https://learn.microsoft.com/windows/win32/api/windows.ui.xaml.media.dxinterop/nf-windows-ui-xaml-media-dxinterop-iswapchainpanelnative-setswapchain)。外部版本和行为将在 M0 重新核对并记录。
