# WinUI 3 桌面例程

此目录是 `model-io` 与 `render-core` 的桌面宿主示例，使用 C++20 和 Windows App SDK 1.8。入口 `main.cpp`/`App.xaml.cpp` 创建窗口；`MainWindow.xaml`/`.cpp` 管理 WinUI 控件、DXGI 交换链和输入；`viewer_engine.cpp` 负责后台解码、上传、渲染线程与取消；`camera.cpp` 是不依赖 WinUI 的相机控制器。

## 操作

使用“打开模型”或将一个本地 PLY/SPZ 拖入画面。可将模型路径作为程序的唯一命令行参数。旧模型在新模型准备完成前保持可见；“取消”停止当前加载，“关闭模型”清空视口。

- 固定：左键旋转，右键平移，滚轮缩放。
- 自由：点击视口进入，鼠标转向；W/A/S/D 前后左右，Q/E 下降/上升，Shift 加速；Esc、失焦或切换模式退出。
- 适配：根据场景包围盒重新设置视距。重置：恢复该模型初次打开时的视角。
- 翻转 Z：切换显示时的 Z 坐标镜像，再次点击恢复。模型文件和 `SceneHandle` 不会改变，固定/自由模式和当前视角仍可继续使用。

## 引擎接入

例程保持 `model-io`、`render-core` 的公共接口独立。后台线程通过 `gs::io::make_model_loader()->load()` 取得只读 `SceneHandle`，用 `CameraController::fit()` 计算相机，再调用 `IRenderer::upload_scene(scene, camera)`。GPU 激活场景后保存 upload ticket；相机更新使用 `set_camera(ticket, camera)`。窗口通过当前 surface generation 创建并绑定 `SwapChainPanel`，调整大小时递增 viewport revision。窗口关闭时等待 `SurfaceDetached` 后解绑，渲染器在渲染线程销毁。

要在其他程序复用引擎，可保留 `include/splat-types/scene.h`、`include/model-io/model_loader.h` 和 `include/render-core/renderer.h` 三个契约，替换本目录的窗口与输入层。`render-core` 不读取文件，`model-io` 不创建 D3D12 资源；不要在 UI 线程同步调用 `load()`。本例程对读取请求使用 request ID，对 GPU 场景使用 upload ticket，对交换链使用 surface generation，防止旧事件覆盖新状态。

Z 镜像由相机视图与 `SwapChainPanel` 的水平显示镜像共同完成；这样不复制大型 splat 数组，也不改变引擎的场景契约。镜像只影响查看，不导出或写回模型。

## 构建与发布

从仓库根目录执行根 README 中的命令。WinUI 项目是自包含的未打包应用；`packaging/build-installer.ps1` 用 Inno Setup 6 生成当前用户安装包，并携带许可、VC 运行库和全部编译后的 shader。`tests/desktop-viewer/camera_tests.cpp` 覆盖拟合、移动、重置与 Z 镜像的相机数学；完整模块测试用 `ctest` 运行。
