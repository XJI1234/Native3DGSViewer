# Windows 原生 3DGS 查看器技术开发计划

状态：开发前技术方案；2026-09-25。本文是实施依据，不表示渲染器已经建成或达到性能目标。

## 1. 目标与边界

开发独立的 Windows 11 x64 桌面查看器，以 C++20、Direct3D 12 和 WinUI 3 直接使用原生图形与窗口接口。首期面向本地单模型的高质量查看；长期逐阶段覆盖 Spark 2.0 在 Windows 上适用的渲染能力。现有 [Viewer](../../Viewer/README.md)（Spark 2.1 + Electron）是同机性能基线，[Spark 2.0 功能说明](../../spark-2.0.0/docs/docs/new-features-2.0.md)是长期功能对照，二者的版本和用途不能混同。

首期完成的用户流程：打开或拖入本地标准 3DGS PLY/SPZ，看到加载进度，模型完整显示后可旋转、平移、缩放、自由飞行、适配模型及重置视角；取消加载或打开无效文件时应用保持可用。首期不包括多模型、远程 URL、LoD、编辑、VR、网格混合渲染或模型导出。标准 PLY 指原始 3DGS 属性布局；SuperSplat/gsplat 压缩 PLY 单列为后续格式，不能因扩展名同为 `.ply` 而静默误读。

成功需同时满足功能、图像和性能验收。性能目标是在 RTX 3080 上、相同质量与物理像素条件下，相比当前 Viewer 的中位帧时间降低至少 20%，且 1% low 帧率不低于基线。性能提升是待验证的目标，不是语言或 API 自带的结果。

## 2. 能力图与模块契约

| 模块 ID | 职责 | 对外契约 | 依赖 |
|---|---|---|---|
| `splat-types` | 不可变场景值类型、格式标识和只读句柄 | `SplatScene`、`SourceFormat`、`SceneHandle` | 无 |
| `model-io` | 本地 PLY/SPZ 识别、解码、校验、进度与取消；产出统一数据 | `IModelLoader::load`、`LoadResult`、进度/错误 | `splat-types` |
| `render-core` | D3D12 设备、资源、排序、splat 光栅化、统计 | `upload_scene`、`set_camera`、`resize`、`render_frame`、`get_stats` | `splat-types`；不依赖解码器或 WinUI |
| `desktop-viewer` | WinUI 3 窗口、文件选择与拖放、相机控制、加载状态 | 调用前两模块，拥有视口和输入事件 | `model-io`、`render-core` |

依赖方向为 `splat-types -> {model-io, render-core} -> desktop-viewer`；两个底层模块不相互依赖。能力图先作为规格评审门槛；确认后分别制定三个模块规格，再按本计划分解实现任务。后续新增格式由 `model-io` 提供，新增 LoD/多视角由 `render-core` 提供，桌面层只编排用户操作。

统一 `SplatScene` 至少包含：splat 数量、float32 中心、尺度、单位四元数、[0,1] 不透明度、SH 0-3 阶系数及实际阶数、中心包围盒与最大尺度、源格式、坐标约定。DC/SH 保持 3DGS 训练色值域，颜色空间转换由 renderer 统一处理。CPU 解码结果采用分量数组，GPU 上传前按着色器访问模式打包；边界只批量传输缓冲区，不逐 splat 调用。不存在的高阶 SH 不参与着色，保留输入的实际最高阶；非有限数、非法尺度/旋转、截断文件和超大声明数量必须产生可解释的 `LoadError`。解析器先按文件头/格式签名确认类型，不能只信扩展名。

坐标以右手世界空间、相机朝局部 -Z 为内部约定，投影映射到 D3D 的 [0,1] 深度范围。PLY/SPZ 的源约定和 Spark 对照画面在解码测试中固定；自动适配视角只改变相机，不悄悄旋转或修改模型数据。

## 3. 首期技术架构

### 3.1 技术栈与工程组织

- 渲染与数据接口：C++20、Direct3D 12、DXGI、DirectXMath、HLSL Shader Model 6.x、Windows SDK 的 DXC。使用 VS 2026 x64 工具链；提交锁定版本的依赖清单与着色器编译配置。
- 桌面外壳：WinUI 3 / C++/WinRT，`SwapChainPanel` 承载 D3D12 交换链；WinUI UI 线程只处理交互和状态，文件解码与渲染提交不阻塞它。首期以 Windows App SDK 自包含的未打包应用和 ZIP 交付，M4 在干净的 Windows 11 机器上验证运行；签名 MSIX 另列后续发布任务。
- 解码：采用 [miniply](https://github.com/vilya/miniply) 解析标准二进制 PLY，采用 [Niantic SPZ](https://github.com/nianticlabs/spz) 的原生 C++ 库解析 SPZ；两者锁定具体提交并做输入限制及许可复核。原始 3DGS 的 log-scale、logit-opacity 与 SH DC 转换在 `model-io` 中统一，不在着色器中对不同格式重复分支。
- 工程：Visual Studio 2026 解决方案管理 WinUI 应用、静态库与 GoogleTest 项目；MSBuild `/restore` 构建，vcpkg manifest 管理测试依赖，miniply/SPZ 以锁定提交的源码依赖集成。性能用 PIX、GPU 时间戳及 PresentMon 定位瓶颈。

项目建议结构：`src/model-io/`、`src/render-core/`、`src/desktop-viewer/`、`include/splat-types/`、`shaders/`、`tests/`、`bench/`、`docs/`。GPU 特性通过运行时检测，无法创建所需 D3D12 设备时显示明确错误，不回落到 WebView 渲染。

### 3.2 帧与加载的数据流

```text
本地文件 -> 后台读取/解码/验证 -> SplatScene + 包围盒
         -> 分块上传缓冲 + GPU 资源 -> 可显示状态
相机 -> 计算着色器：可见性/投影范围/排序键
     -> GPU 径向/视深度排序 -> 有序索引 + 间接绘制参数
     -> 实例化四边形：3D 协方差投影为屏幕椭圆
     -> 像素着色：SH 颜色、Gaussian 权重、alpha 混合 -> Present
```

初始渲染路线是 GPU 计算生成排序键并完成 32 位排序、从远到近绘制实例化 quad；不进行每帧 GPU->CPU 读回。默认采用与当前 Spark 一致的径向距离排序，视深度排序保留为诊断选项；基准必须在相同模式下比较。顶点阶段计算 `R diag(scale^2) R^T` 经投影 Jacobian 得到 2D 协方差，像素阶段按椭圆 Gaussian 衰减和源 SH 阶数着色，再以与基线一致的预乘 alpha、splat 截断范围和颜色空间混合。多模型的全局排序留待下一阶段，首期单模型排序不能依赖 UI 线程。

内存路径使用后台分块读取、上传堆与 fence 管理资源寿命，预估模型常驻显存和临时排序缓冲；在上传前按 DXGI 当前预算/用量核对新增资源，另核对宿主与 helper 的 RAM 峰值。内存不足时拒绝待命模型、保留旧场景并报告增量需求/余量；用户可主动关闭旧模型再重试。窗口缩放只重建尺寸相关目标资源；设备丢失时停止渲染，用保留的 CPU 场景快照有界重建并重新绑定交换链，失败时显示持续错误及重试入口，不留下空白无反馈的视口。

### 3.3 交互与状态

查看模式：左键轨道旋转、右键平移、滚轮缩放；自由飞行模式：鼠标观察方向、WASD 水平移动、Q/E 升降、Shift 加速；两种模式可切换，速度按模型包围盒尺度初始化。工具栏提供打开、关闭当前模型、适配模型、重置视角及模式切换；键鼠焦点离开视口时停止连续移动。拖放和文件选择走同一加载入口；新模型加载成功后再替换当前场景，失败或取消保留已显示模型。窗口尺寸和 DPI 变化不改变相机朝向或产生拉伸。

## 4. 分阶段实施与检查点

工时为一名熟悉 C++/D3D12 的工程师的人周估计，不是承诺的日历工期；首次跨 WinUI/交换链、GPU 排序和真实数据集的风险另留约 20% 缓冲。

| 阶段 | 人周 | 可运行成果与验收门槛 |
|---|---:|---|
| M0 规格与基线 | 2-3 | 三模块规格、许可结论、按时间驱动的基准脚本/相机路径、Viewer 基线数据；确认 WinUI 3 + D3D12 交换链能持续呈现，并验证 1920x1080 基准是否受呈现节流 |
| M1 正确显示 | 5-7 | 标准 PLY -> GPU -> 单模型画面；GPU 排序的正确性、协方差、颜色、透明混合与固定视角截图通过；1.18M splat PLY 可连续浏览 |
| M2 完整首期交互 | 4-5 | SPZ、拖放/文件对话框、两种浏览模式、关闭当前模型、进度/取消/错误恢复；小中大模型的端到端操作通过 |
| M3 性能优化 | 4-6 | 优化 M1 的 GPU 排序、裁剪、上传和混合，各阶段可测；RTX 3080 同画质对照达到帧时间/1% low 门槛；性能报告含未达标场景 |
| M4 稳定性与交付 | 2-3 | 长时间浏览、窗口/DPI、损坏文件、显存不足及其他 D3D12 显卡测试；发布候选和复现说明 |

M1/M2 估算已包含 [model-io 规格](SPEC-model-io.md)规定的辅助解码进程、资源限制和取消机制，较初版各增加约 1 人周。建议按以下可验证任务落实，任务完成后更新模块规格与测试证据：

1. 固定参考版本、输入样本、许可与性能采集脚本；验证同一路径能在 Viewer 中重放。
2. 建立 VS 解决方案、WinUI 3 `SwapChainPanel` 和 D3D12 三角形/交换链；验证 resize、DPI 与设备创建错误。
3. 定义 `SplatScene` 和 PLY 解码；用属性重排、缺失 SH、损坏头及约 118 万 splat 样本验证。
4. 完成 0 阶颜色、协方差椭圆和 alpha 混合；以固定相机截图对照 Spark。
5. 增加 GPU 排序、可见性计算和帧阶段计时；验证相机穿越与快速旋转时无明显排序条带。
6. 接入 SPZ 解码、异步上传、进度与取消；验证重复打开和取消时资源没有累积。
7. 接入 WinUI 交互、轨道/飞行模式、适配与重置；完成端到端人工操作清单。
8. 做等画质基准、热点优化和回归；未达门槛时按 GPU 时间戳定位排序、投影或混合瓶颈，再进入发布候选。

每 2-3 项任务设一次构建、单测、截图及人工浏览检查点。M1 的图像正确性与 M3 的等画质性能是继续推进的硬门槛，不能以减少 splat 数、降低分辨率、缩小 Gaussian 范围或关闭 SH 伪装提速。

## 5. 验证协议

### 5.1 样本与正确性

| 档位 | 工作区样本 | 用途 |
|---|---|---|
| 小 | [jidaoshan.spz](../../Viewer_android/public/scene/jidaoshan.spz)（约 10 MB） | 快速加载、交互与错误恢复 |
| 中 | [1.ply](../../1.ply)（66 MB，头部声明 1,179,648 splat） | 标准 PLY、排序和基础性能 |
| 大 | [zhihuizhimen.spz](../../Viewer_android/public/scene/zhihuizhimen.spz)（约 101 MB） | 显存、加载峰值、持续浏览 |

样本在基准开始前记录 SHA-256、实际 splat 数、SH 阶数和源许可证；大小不等于 splat 数。补充合成数据验证边界：空/截断文件、异常浮点数、极端尺度、重叠透明 splat、远离原点坐标、不同视线方向。固定相机截图与 Spark 基线对齐颜色空间和分辨率后计算 SSIM，目标不低于 0.95；同时用小型合成夹具逐像素检查 SH、透明合成及裁剪边缘，并检查真实样本的误差热图与局部差异。人工确认无缺块、黑边、错误排序及视角翻转。若画质未对齐，该场景性能数据只作诊断，不作胜出结论。

### 5.2 性能

基线用当前 Viewer 的 Spark 2.1 版本，原生版和基线使用同一 RTX 3080、驱动、电源模式、模型文件、1920x1080 **物理像素**视口、按时间驱动的相机路径、颜色空间、SH 阶数、排序方式、splat 截断范围及关闭 LoD 的单模型设置。Viewer 当前代码默认限制 DPR 到 2、关闭 MSAA；测试需固定实际视口像素，不能只对齐 CSS 尺寸。两者尽可能关闭垂直同步与人为帧率上限，并用 PresentMon 核实实际呈现节奏；WinUI composition、DWM 或 Electron 仍可能限帧。静止视角和持续移动/转向分别测量，移动路径以相同时间戳驱动，记录相机矩阵以防帧率差异改变采样轨迹。若任一端受刷新率上限裁剪，该场景的用户可见帧时间结果标为“不可判定”，不得宣称达到 20% 目标；M0 必须建立双方可比且未受限的测量条件，GPU timer/WebGL timer query 仅作瓶颈诊断。

每场景预热 30 秒、采样 60 秒、重复至少 3 次，记录每次中位帧时间和 1% low FPS，并报告三次的中位值。这里的 1% low 定义为最慢 1% 帧的平均帧时间取倒数（秒单位）；无帧、失焦和模型加载期不计入采样。RTX 3080 主验收集合中的每个可比场景均须满足 `native median frame time <= 0.80 * Viewer median frame time`，且 `native 1% low FPS >= Viewer 1% low FPS`。同时记录首帧可见时间、完整加载时间、CPU 占用、峰值 RAM/VRAM 和 GPU 分阶段耗时，作为诊断指标，不用其替代主门槛。使用固定相机轨迹文件与自动采样脚本，保存原始 CSV、截图、驱动版本和测量命令。

兼容性矩阵至少覆盖另一档 NVIDIA、AMD 独显和 Intel 核显的 D3D12 设备；没有相应设备时标注“未测试”，不得推定兼容。兼容性门槛是能够加载小样本、操作视角、正常关闭和明确报告资源不足，不要求这些 GPU 达成 RTX 3080 的 20% 性能目标。

### 5.3 拟定构建与测试命令

以下命令现可构建并测试 model-io 工程；尚无桌面应用可运行：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
```

M0 需核实 WinUI 3/C++ 模板、Windows App SDK 版本、MSBuild 产物路径与所需 D3D12 feature level，并把测试 DLL 路径固定到构建配置。着色器由项目构建调用 DXC 编译；测试至少覆盖解码、统一数据转换、相机数学、GPU 截图及端到端打开/取消。性能脚本应单独运行，避免把易波动的 FPS 断言放进普通单元测试。

## 6. Spark 能力对照与后续顺序

| Spark 2.0 能力 | 本项目阶段 | 说明 |
|---|---|---|
| 标准 3DGS PLY、SPZ 显示，SH、透明排序 | 首期 M1-M3 | 单模型，图像与性能均验收 |
| 鼠标浏览、自由移动、视角控制 | 首期 M2 | 映射为 Windows 键鼠交互 |
| 压缩 PLY、SPLAT、KSPLAT、SOG | 后续 F1 | 扩展 `model-io`，按格式建立兼容样本 |
| 多模型、实例变换、跨模型正确排序、mesh 融合 | 后续 F2 | 先统一场景图与跨对象索引，再验证混合深度 |
| LoD 生成、RAD、渐进加载、显存分页 | 后续 F3 | 依赖 F1/F2 的数据与资源契约；本地大场景先行，HTTP Range 再扩展 |
| 多视角/离屏渲染 | 后续 F4 | 每视角独立可见集与排序结果 |
| 颜色/形变编辑、动画、可编程 splat 管线 | 后续 F5 | 先定义 GPU 修改接口，再设计图节点或等价系统 |
| WebXR、手机触控与浏览器 API | Windows 不适用 | 不纳入等价验收；未来如需 VR 另立 OpenXR 规格 |

F1-F5 各自需在启动前形成独立模块规格、验收样本与性能预算；“与 Spark 一致”指已列出的 Windows 适用能力经逐项验证，不代表复刻 Three.js/JavaScript API 或未经验证地承诺实验功能。

## 7. 风险与处置

| 风险 | 早期证据与处置 |
|---|---|
| 原生版排序/混合仍受 GPU 带宽与 overdraw 限制 | M0-M1 分离测排序、投影、像素混合；M3 若未达 20%，先用 PIX 定位并优化主瓶颈，记录未达场景，不降低画质门槛 |
| WinUI 3 交换链与 DPI/线程集成复杂 | M0 先做持续呈现与窗口缩放原型；失败则保留 D3D12 核心，单独重评桌面宿主 |
| PLY 变体和 SPZ 版本/SH 排列导致颜色或坐标错误 | 锁定输入子集、解析单测及 Spark 对照截图；未知变体明确拒绝 |
| 大模型造成 RAM/VRAM 峰值过高 | 加载前估算、分块上传、预算检查；M4 做重复打开及设备丢失测试 |
| 呈现节流掩盖性能差异 | M0 用 PresentMon 判断 WinUI/Electron 是否触及刷新率上限；受限场景标不可判定，先建立未受限的等画质验收方法 |
| Spark 源码许可标记不一致 | 根目录 `LICENSE` 与 `package.json` 标 MIT，但 `rust/Cargo.toml` 标 `Proprietary`，现有 Viewer README 亦如此；在逐项厘清授权前只作行为参照，不复制 Rust 模块代码 |
| 估算与设备覆盖不足 | 工时按单人估算并留缓冲；缺席的 AMD/Intel 设备标为待实测，不写成已支持 |

## 8. 规格与资料来源

能力图审阅后，按 `model-io`、`render-core`、`desktop-viewer` 顺序建立规格；[model-io 技术规格](SPEC-model-io.md)、[render-core 技术规格](SPEC-render-core.md)、[desktop-viewer 技术规格](SPEC-desktop-viewer.md)与[系统技术实现设计](system-technical-design.md)已起草，均待评审。每份规格写目标、接口、命令、工程结构、代码约定、测试和边界，并把本文件阶段任务拆成单次可验证的小任务。规格变更先更新文档再更改实现。本文件是总技术路线，后续任务清单应放入项目 `tasks/plan.md` 与 `tasks/todo.md`，避免将未批准的实施细节当作既成事实。

主要依据：[Spark 2.0 新能力](../../spark-2.0.0/docs/docs/new-features-2.0.md)、[格式加载](../../spark-2.0.0/docs/docs/loading-splats.md)、[控制](../../spark-2.0.0/docs/docs/controls.md)、[性能说明](../../spark-2.0.0/docs/docs/performance.md)、[SparkRenderer 源码](../../spark-2.0.0/src/SparkRenderer.ts)、[现有 Viewer 视口](../../Viewer/src/components/SparkViewport.vue)、[D3D12 文档](https://learn.microsoft.com/windows/win32/direct3d12/directx-12-programming-guide)、[Windows App SDK 文档](https://learn.microsoft.com/windows/apps/windows-app-sdk/)。外部资料和第三方库在建仓时锁定版本并再次核对许可证。
