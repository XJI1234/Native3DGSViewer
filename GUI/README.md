# WinUI 3 桌面例程

此目录是 `Native3DGS::Engine` SDK 的桌面宿主例程，使用 C++20、WinUI 3 和 Windows App SDK 1.8。`main.cpp`/`App.xaml.cpp` 创建窗口；`MainWindow.xaml`/`.cpp` 管理控件、DXGI 交换链、输入和引擎快照。加载、相机、GPU 上传、取消与设备恢复由 `gs::engine::IEngine` 负责。

## 操作

使用“打开模型”或将一个本地 PLY/SPZ 拖入画面。可将模型路径作为程序的唯一命令行参数。旧模型在新模型准备完成前保持可见；“取消”停止当前加载，“关闭模型”清空视口。

- 固定：按住左键拖动模型，模型沿鼠标方向旋转，翻转 Y 后方向保持一致；右键平移，滚轮缩放。
- 自由：点击视口进入，鼠标转向；W/A/S/D 前后左右，Q/E 下降/上升，Shift 加速；Esc、失焦或切换模式退出。
- 适配：根据场景包围盒重新设置视距。重置：恢复该模型初次打开时的视角。
- 翻转 Y：切换显示时的 Y 坐标镜像，再次点击恢复。模型文件和 `SceneHandle` 不会改变，固定/自由模式和当前视角仍可继续使用。

## 引擎接入

窗口使用 `gs::engine::create_engine()` 创建引擎，`open({path})` 返回请求 ID；取消、关闭、视角和模式分别调用 `cancel(id)`、`close()`、`camera_command()`。定时读取 `snapshot()` 显示进度，并通过 `poll_events()` 处理场景完成、失败、设备恢复和交换链解绑。窗口以 `surface_generation` 取得 `addref_surface_queue()`，创建合成交换链，随后调用 `attach_swapchain()` 和 `resize()`。关闭时请求 `detach_swapchain()`，收到 `SurfaceDetached` 后在 UI 线程解除 `SwapChainPanel` 绑定；最后调用 `request_shutdown()`，后台销毁引擎。

其他程序可复用 `include/native3dgs/engine.h`，替换本目录的窗口和输入层。加载不会阻塞 UI 线程。宿主必须按 generation 处理交换链解绑和设备释放确认，并用 request ID 过滤过期事件。SDK 的完整接口与部署步骤见 [SDK 集成指南](../docs/SDK-guide.md)。

Y 镜像由相机视图与 `SwapChainPanel` 的水平显示镜像共同完成；这样不复制大型 splat 数组，也不改变引擎的场景契约。鼠标拖动增量从未镜像的根布局坐标取得，避免视口 `RenderTransform` 反转输入。SDK 的 FlipX、FlipY、FlipZ 可独立组合，奇数轴翻转时宿主需水平镜像合成画面，偶数轴不需要。镜像只影响查看，不导出或写回模型。

## 构建与发布

从仓库根目录执行根 README 中的解决方案构建命令；根构建会在 WinUI XAML 实现生成后重新编译窗口文件。WinUI 项目是自包含的未打包应用；`packaging/build-installer.ps1` 用 Inno Setup 6 生成当前用户安装包，并携带许可、VC 运行库和全部编译后的 shader。`tests/engine/camera_tests.cpp` 覆盖拟合、移动、重置与各轴组合镜像的相机数学；模块测试用 `ctest` 运行。

## 日志

每次启动优先在程序所在目录的 `logs` 文件夹生成一个以 UTC 启动时间和进程号命名的 `.log` 文件；目录不可写时回退到 `%LOCALAPPDATA%\Native3DGSViewer\logs`，两个位置都不可写才提示。启动时清理这两个位置中当前使用目录内超过 30 天、名称符合程序格式的旧日志。每行是一条 JSON 记录，包含操作系统版本、CPU/内存、DXGI 显卡及 D3D12 能力探测、引擎和视口启动结果、输入字节数、加载耗时、成功加载的点数与 SH 阶数、失败诊断及字节偏移、设备恢复事件。活跃场景每 5 秒至多记录一次 `render_sample`，含 CPU/GPU 帧时间、GPU 排序时间、绘制点数和 DXGI 预算/用量。日志不写入模型的完整路径，也不逐帧写盘。

正式版启动时，引擎在开放视口前自动执行 65,537 个键和值的 GPU 稳定排序读回测试。`engine_created` 中的 `sort_self_test=passed` 表示通过，`sort_mode` 表示使用 `standard`、`fixed_wave32` 或 `wave_agnostic`；`wave32_fallback_hr` 非零表示固定 wave32 管线创建或校验失败并已回退。自检完全失败时，`engine_create_failed` 记录失败的排序变体，不会继续渲染。故障设备测试时请发送本次启动对应的完整日志。

集成显卡通过 D3D12 的 UMA 能力判断共享内存，不要求独立的 non-local 上传预算。模型仍须符合 DXGI 当前 local 预算的 80% 增量余量限制。若加载失败，`scene_failed` 的 `diagnostic` 会记录 `upload_admission` 或 `upload_begin`、`UMA`、local/non-local 预算与当前用量、查询结果及估算需求；请连同该次完整日志提供，以确认是预检拒绝还是实际资源分配失败。物理内存容量本身不保证任意模型都能加载。

从仓库根目录运行 `./packaging/build-installer.ps1` 生成 `out/installer/Native3DGSViewer-Setup-x64.exe`。安装包不携带构建机上的日志。

大场景按实际 CPU 可用内存、提交余量及 DXGI 当前显存预算准入。SH 3 阶属性拆分为基础缓冲和 SH 缓冲，单缓冲地址仍受 32 位字节偏移约束；达到该技术边界会返回不支持的场景错误。22,480,361 点 SH3 的 PLY 已在 RTX 3080 上完成加载和 GPU 单帧烟雾测试；首次排序开销较高，具体交互性能以 `render_sample` 和同机基准为准。
