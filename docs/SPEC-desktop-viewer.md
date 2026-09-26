# Spec: desktop-viewer（Windows 桌面查看与交互）

状态：GUI 基础实现已落地，完整验收仍在进行；2026-09-26。现行宿主接口为 [engine/SDK](SPEC-engine-sdk.md) 的 `gs::engine::IEngine`；已验证内容见[桌面查看器验证记录](desktop-viewer-verification.md)。下文早期 `ViewerCoordinator` 设计仅作为需求推导记录，实际可复用接口和操作以 [GUI 指南](../GUI/README.md)为准。

## 1. 目标与范围

`desktop-viewer` 是 WinUI 3/C++/WinRT 宿主。用户可离线打开或拖入一个本地标准 PLY/SPZ，查看阶段进度、取消加载，使用轨道或自由飞行浏览，适配模型与重置视角。应用在无模型、正在加载、错误、显存不足和设备恢复时都有可理解的界面状态。桌面层负责相机意图与生命周期编排，不进行格式解析、逐点数据转换、D3D12 排序或着色。

首期仅 Windows 11 x64、键鼠、单窗口单视口和单模型。不引入 MVVM 框架依赖；WinUI 窗口代码处理交互和快照呈现，可测试的纯 C++ 相机与流程控制器由 SDK 提供。未来多视口、编辑和动画通过 SDK 命令扩展。

## 2. 模块边界与接口

桌面层依赖 `gs::engine::IEngine`，由 SDK 持有加载和渲染线程。UI 所有权在主 STA 线程，定时读取 `snapshot()` 与 `poll_events()`；后台线程不直接调用 XAML 控件。窗口只管理交换链、文件选择、输入和可见状态。以下早期拟议接口已由 SDK 接口取代。

建议的内部纯 C++ 接口：

```cpp
namespace gs::desktop {
using RequestId = uint64_t;
enum class ViewMode : uint8_t { Orbit, Fly };
enum class SessionPhase : uint8_t {
    Empty, Loading, Uploading, PreparingFrame, Ready,
    Cancelling, RecoveringDevice, Error
};
enum class DesktopErrorCode : uint8_t {
    MultipleFiles, RemotePath, DispatcherStopped, InternalFailure
};
struct UserError {
    std::variant<gs::io::LoadErrorCode, gs::render::RenderErrorCode,
                 DesktopErrorCode> code;
    std::string diagnostic_id;
};
struct InputCommand;
struct ViewSnapshot {
    SessionPhase phase;
    RequestId request_id;
    std::optional<gs::io::LoadProgress> load_progress;
    uint64_t upload_done, upload_total;
    std::optional<UserError> error;
    bool has_visible_scene;
    ViewMode mode;
};
class ViewerCoordinator {
public:
    void open_local_file(std::filesystem::path path);
    void cancel_current_request();
    void close_current_model();
    void set_viewport(gs::render::Viewport physical_size);
    void apply_input(const InputCommand& command);
    ViewSnapshot snapshot() const;
};
}
```

`RequestId` 每次打开递增；`UploadTicket` 只在同一请求成功解码后关联，`SceneReady` 后成为活动场景 ticket。ViewModel 订阅不可变快照，不直接持有 `SceneHandle`。协调器保存当前活动模型元数据、活动 CPU `SceneHandle` 的弱视图、活动 ticket 和最新相机；强引用由 renderer 按其规格持有。`UserError` 只携带稳定错误码和诊断编号，UI 用固定中文文案表映射；日志记录限长诊断，不能直接显示第三方库异常字符串。

## 3. 线程和状态机

UI 线程只创建/管理 WinUI 控件、文件对话框、鼠标键盘事件、`SwapChainPanel` 绑定和快照呈现。一个协调器工作线程串行处理文件请求：新请求先对旧解码调用 `request_stop()`，对旧 GPU 上传调用 `cancel_upload(ticket)`，等待辅助解码进程确认退出后才调用新的 `load()`；这个等待绝不在 UI 线程。渲染线程由 `render-core` 独占，上传与绘制跨线程以消息/事件通信。协调器给 `set_camera` 附活动场景 ticket，给交换链操作附 surface generation，给 `resize` 再附同一 surface 内递增的 viewport revision；过期命令无权修改新场景。UI 不拥有会在析构时等待解码的 `std::jthread`；协调器拥有它，并在后台执行有界关闭流程，防止窗口关闭卡住。

状态转换：

```text
Empty/Ready/Error --Open(id)--> Loading(id)
Loading(id) --解码成功--> Uploading(id) --GPU ready--> PreparingFrame(id)
PreparingFrame(id) --首次成功 Present/SceneReady--> Ready(id)
Loading/Uploading/PreparingFrame --Cancel--> Cancelling --> 原有 Ready 或 Empty
任何待命阶段 --失败--> 原有 Ready 或 Error（无旧场景）
任意可绘制状态 --设备移除--> RecoveringDevice --> Ready 或 Error
Ready/Loading/Uploading/PreparingFrame --Close--> Cancelling --> Empty
```

`Ready` 表示新模型完整可呈现，不能把解码完成当作成功打开。处于待命状态时视口继续显示旧模型；覆盖状态栏显示“正在打开新模型”。取消或失败恢复旧相机、模型名、模式和可见场景；首次打开失败显示空视口及错误操作。新请求期间旧请求迟到的进度/结果按 `RequestId` 和 `UploadTicket` 丢弃，并释放其未使用句柄。取消期间再次打开只保留最新一个待启动路径，避免无限排队。`close_current_model()` 取消待命请求，调用 renderer 的 `clear_scene()`；协调器记录当时的活动 ticket，仅在 `SceneCleared.ticket` 匹配且没有更新的 `SceneReady` 时清除模型名和视角基线，过期事件只作资源完成记录。用户可用关闭操作释放旧模型占用后重试大模型。关闭窗口优先撤销新加载和 GPU 上传，停止输入，再等待辅助进程/渲染线程有界退出；超时写诊断并明确提示，不能让 UI 无期限悬停。

解码阶段显示 `Opening/Inspecting/Decoding/Validating` 的实际阶段；只有 `bytesRead/totalBytes` 有意义时显示百分比，否则用非定量进度。GPU 阶段按完成 copy fence 的字节显示进度；`PreparingFrame` 显示“正在生成画面”，若窗口不可见或 0x0 则显示“等待视口恢复”，不误报加载完成或无限转圈。用户取消后 UI 在 200 ms 内转“正在取消”，辅助进程目标 3 秒内回收，最终取消事件到达后才解锁新请求。相机持续移动事件按最新值合并，不能淹没渲染消息队列。

## 4. 窗口、交换链与 DPI

WinUI 3 窗口含一个 `SwapChainPanel` 视口、紧凑命令栏、状态/进度区及非阻断错误通知。命令栏提供打开、关闭当前模型、适配模型、重置视角、Y 坐标镜像和轨道/飞行模式切换；取消按钮只在有待命请求时可用。打开按钮在无模型和加载失败后仍可用。图形区域不嵌浏览器。可访问名称、键盘焦点和高对比度状态由 WinUI 控件提供。镜像仅改变查看结果，不修改 `SceneHandle` 或文件数据。

M0 先实现 D3D12 三角形原型：renderer 创建设备/direct queue，桌面宿主按当前 generation 取得队列并创建 `CreateSwapChainForComposition` 交换链；UI 获取 `ISwapChainPanelNative` 并在 UI 线程调用 `SetSwapChain`，然后将交换链连同 generation 交给 renderer 保留。`SwapChainPanel` 的逻辑尺寸乘 `XamlRoot.RasterizationScale` 得目标物理像素，四舍五入并限制非零；使用 composition 缩放矩阵处理高 DPI 时，必须实测像素边界与触点映射一致。`SizeChanged`、`XamlRoot.Changed`、显示器迁移和最小化均经 UI 合并成带 generation 与递增 viewport revision 的 resize 命令，renderer 只应用该 generation 最新 revision。正常 resize 等待受影响 back-buffer fence 后释放旧 back-buffer 引用，再调用 `ResizeBuffers`；0x0 时暂停绘制。正常卸载/关窗先请求 renderer detach 并等待 `SurfaceDetached`，再在 UI 线程撤销绑定和释放宿主交换链引用；设备移除时旧 fence 可能不完成，走故障解绑路径而不等待 `SurfaceDetached`。设备重建时响应 `SurfaceRebindRequired(generation)`，使用新 direct queue 重建并重新绑定交换链；迟到的旧 generation 事件不执行。具体 WinUI interop 顺序以 M0 原型实测固定。

未打包应用的 `FileOpenPicker` 必须按 Windows App SDK/C++/WinRT 的 HWND 初始化要求接入；只展示 `.ply`/`.spz`，但扩展名不是格式可信依据。拖放仅接受一个本地文件路径，多个项目或 URL 给出明确提示；路径用 `std::filesystem::path`/UTF-16 原样交给 `model-io`。每个入口都走同一 `open_local_file()`；实际本地常规文件、大小和权限由 `model-io` 最终验证，不在 UI 维护第二套格式解析器。对 Windows 长路径和中文路径做端到端测试。

## 5. 相机与输入语义

相机控制器完全在纯 C++ 层实现、以 double 存储位置/焦点与四元数，每帧只提交一个已验证 `CameraState`。坐标为 RUB 右手、+Y 上、局部 -Z 前方。默认透视 FOV、近远面由包围盒和视距求得并限制到合理范围；固定视角测试把数值写在相机轨迹文件中。解码成功后以中心包围盒、`maxScale * QualityConfig.max_stddev` 的保守高斯支撑扩展量和纵横比计算 fit 视角，避免仅把中心放进画面却裁掉边缘椭圆；若无法构造有限且符合近远面限制的相机，返回结构化场景/相机错误并保留旧模型，不提交无效 `upload_scene`。有效初始相机随 `upload_scene(scene, initial_camera)` 一起提交，在新场景首次激活时生效。边距可配置但在基准中固定。`Fit` 重算当前模型可见范围，`Reset` 回到该模型首次激活时保存的默认视角。两者仅改相机，不转动模型。没有活动场景时禁用 Fit/Reset，保留空场景默认相机。

轨道模式：左键拖动绕焦点 yaw/pitch，pitch 防翻转；右键拖动按视口像素与深度对应世界单位平移相机和焦点；滚轮以指数比例调节距离并钳位最小/最大；鼠标位置映射考虑 DPI。输入增量须取未受视口显示镜像影响的屏幕坐标，使翻转任何模型轴后左键拖动仍与鼠标方向一致。自由飞行模式：点击视口后捕获指针并隐藏/锁定光标，鼠标相对移动改变朝向；WASD 前后/左右，Q/E 升降，Shift 加速，速度以模型包围盒尺度初始化并按实际帧间隔积分，限制单次大 dt 防止切后台后跳跃。Esc 或模式切换释放捕获。可选择“轨道/飞行”的分段控件或等价明确状态，不让两个模式同时响应同一输入。

`PointerCanceled`、失焦、最小化、打开系统对话框、窗口停用、设备恢复与关闭都清空按键集合并释放指针捕获；防止粘键与后台漂移。鼠标滚轮仅在视口区域生效，控件焦点下不抢文本输入。输入事件可记录成确定性的 `InputCommand` 序列，用于相机回放和基准；录制时间戳与物理视口尺寸，回放在同一固定步长下应得到相同矩阵（规定容差）。

## 6. 错误呈现与可恢复性

| 来源 | UI 行为 | 后续操作 |
|---|---|---|
| 文件不存在、无权限、文件格式/版本不支持 | 用稳定错误码给出文件名及可理解原因；不显示库内部诊断 | 重新选择文件，旧模型继续可用 |
| 属性损坏、截断、超上限、解码进程失败/超时 | 显示“文件无效/资源超限/读取失败”等区分提示，提供诊断编号 | 可重试或换文件，不需重启 |
| RAM/显存预算不足或资源创建失败 | 显示估算增量需求和当前可用预算（GiB），保持旧模型 | 关闭当前模型/其他占用资源的应用，再重试或选小模型 |
| 设备移除/交换链失效 | 状态区显示恢复中，禁止重复打开洪泛；有界重建失败后持续错误界面 | 重试初始化或关闭，不出现静默黑屏 |
| 用户取消 | 显示取消中，终态后恢复旧场景/空状态 | 可以立即再打开 |

弹出提示不应遮住整个可见模型或每帧重复显示；错误详情可展开复制错误码、HRESULT、阶段和诊断编号，但默认不含完整本地路径、文件字节。报告写本地轮转日志，关闭应用也能读取最近故障。UI 层捕获所有异步任务异常并转结构化 `InternalFailure`，不得让 C++/WinRT fire-and-forget 异常静默终止进程。

## 7. 工程、命令与测试

当前例程位于 `GUI/`：XAML 窗口和事件适配在 `MainWindow.*`，后台协调器在 `viewer_engine.*`，纯 C++ 相机在 `camera.*`；相机测试在 `tests/desktop-viewer/`。后续可拆分 ViewModel/状态机并补 UI 自动化。WinUI XAML 控件 ID 与可访问名称应保持稳定；C++20 命名遵循 `model-io` 规格；没有第三方 UI 框架依赖。公开模块接口变更先更新本规格和消费者测试。

当前构建、测试和打包命令：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
ctest --test-dir out/cmake -C Release --output-on-failure
& .\packaging\build-installer.ps1 -SkipBuild
```

| 首期功能 | 验收操作 |
|---|---|
| 文件选择/拖入 | PLY 与 SPZ、中文/长路径、重复打开均进入相同加载流程；多文件、URL、有误扩展名得到正确拒绝或内容识别 |
| 进度/取消 | 真实阶段单调、取消 200 ms 内反馈、完成后旧模型/相机保持；快速连续打开只激活最后一个请求 |
| 关闭当前模型 | 加载中先取消待命请求，`SceneCleared` 后视口转空、RAM/显存回到稳定水平；大模型可从空场景重新打开 |
| 轨道/平移/滚轮 | 固定输入轨迹后的相机矩阵在容差内一致；触边、极近/远距不翻转或产生 NaN |
| 自由飞行 | WASD/QE/Shift 与鼠标方向正确；失焦、Esc、模式切换释放捕获，返回时无突然跳跃 |
| Fit/Reset | 大/小/偏心/远原点模型均完整进入视野；Reset 精确回首次保存的视角 |
| 窗口与恢复 | 100%-200% DPI、跨屏、拖拽 resize、最小化、设备移除及旧 generation 迟到事件；无图像拉伸、错误触点、无反馈黑屏或旧 fence 死等 |
| 无障碍与关停 | 键盘可到达主要命令，错误可读；加载/恢复中关闭不会悬挂或遗留 helper 进程 |

始终保证 UI 响应和请求代际隔离；修改快捷键/手势需更新帮助与回放用例；不从 XAML 直接调用阻塞 `load()`、`WaitForSingleObject` 或 GPU fence 等待。后续 F2-F5 的多模型、编辑、动画应扩展协调器状态和命令，而不能绕过 renderer 的场景生命周期。

## 参考与待决检查点

- [Spark 控制文档](../../spark-2.0.0/docs/docs/controls.md)和[现有 Viewer 视口](../../Viewer/src/components/SparkViewport.vue)仅作用户能力参照；Windows 键鼠映射按本文验收。
- [WinUI 3 桌面应用](https://learn.microsoft.com/windows/apps/winui/winui3/)、[SwapChainPanel native 绑定](https://learn.microsoft.com/windows/win32/api/windows.ui.xaml.media.dxinterop/nf-windows-ui-xaml-media-dxinterop-iswapchainpanelnative-setswapchain)、[Windows App SDK 文件选择器](https://learn.microsoft.com/windows/apps/develop/files/).
- 本机已完成 WinUI 窗口启动与正常关闭、安装包安装/卸载冒烟检查；跨 DPI、跨设备与画质/性能门槛仍按[验证记录](desktop-viewer-verification.md)继续验收。
