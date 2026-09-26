# WinUI 3 桌面例程

此目录是 `Native3DGS::Engine` SDK 的桌面宿主例程，使用 C++20、WinUI 3 和 Windows App SDK 1.8。`main.cpp`/`App.xaml.cpp` 创建窗口；`MainWindow.xaml`/`.cpp` 管理控件、DXGI 交换链、输入和引擎快照。加载、相机、GPU 上传、取消与设备恢复由 `gs::engine::IEngine` 负责。

## 操作

使用“打开模型”或将一个本地 PLY/SPZ 拖入画面。可将模型路径作为程序的唯一命令行参数。旧模型在新模型准备完成前保持可见；“取消”停止当前加载，“关闭模型”清空视口。

- 固定：按住左键拖动模型，模型沿鼠标方向旋转；右键平移，滚轮缩放。
- 自由：点击视口进入，鼠标转向；W/A/S/D 前后左右，Q/E 下降/上升，Shift 加速；Esc、失焦或切换模式退出。
- 适配：根据场景包围盒重新设置视距。重置：恢复该模型初次打开时的视角。
- 翻转 Y：切换显示时的 Y 坐标镜像，再次点击恢复。模型文件和 `SceneHandle` 不会改变，固定/自由模式和当前视角仍可继续使用。

## 引擎接入

窗口使用 `gs::engine::create_engine()` 创建引擎，`open({path})` 返回请求 ID；取消、关闭、视角和模式分别调用 `cancel(id)`、`close()`、`camera_command()`。定时读取 `snapshot()` 显示进度，并通过 `poll_events()` 处理场景完成、失败、设备恢复和交换链解绑。窗口以 `surface_generation` 取得 `addref_surface_queue()`，创建合成交换链，随后调用 `attach_swapchain()` 和 `resize()`。关闭时请求 `detach_swapchain()`，收到 `SurfaceDetached` 后在 UI 线程解除 `SwapChainPanel` 绑定；最后调用 `request_shutdown()`，后台销毁引擎。

其他程序可复用 `include/native3dgs/engine.h`，替换本目录的窗口和输入层。加载不会阻塞 UI 线程。宿主必须按 generation 处理交换链解绑和设备释放确认，并用 request ID 过滤过期事件。SDK 的完整接口与部署步骤见 [SDK 集成指南](../docs/SDK-guide.md)。

Y 镜像由相机视图与 `SwapChainPanel` 的水平显示镜像共同完成；这样不复制大型 splat 数组，也不改变引擎的场景契约。SDK 的 FlipX、FlipY、FlipZ 可独立组合，奇数轴翻转时宿主需水平镜像合成画面，偶数轴不需要。镜像只影响查看，不导出或写回模型。

## 构建与发布

从仓库根目录执行根 README 中的解决方案构建命令；根构建会在 WinUI XAML 实现生成后重新编译窗口文件。WinUI 项目是自包含的未打包应用；`packaging/build-installer.ps1` 用 Inno Setup 6 生成当前用户安装包，并携带许可、VC 运行库和全部编译后的 shader。`tests/engine/camera_tests.cpp` 覆盖拟合、移动、重置与各轴组合镜像的相机数学；模块测试用 `ctest` 运行。
