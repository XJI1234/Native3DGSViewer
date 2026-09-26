# Spec: 原生 3DGS 引擎与 SDK

2026-09-26。范围为首期所有非 UI 组件：共享场景、隔离 model-io、D3D12 render-core、纯 C++ 相机控制、异步请求协调与渲染线程、宿主 surface 生命周期、基准工具和可安装 SDK。不创建 WinUI 窗口、文件选择器、输入事件适配器或桌面应用；后续多模型、LoD、编辑和动画沿用原计划另立规格。

## 交付形式

Windows 11 x64、C++20、VS 2026/MSVC 19.50，使用 /MD；首期提供原生静态库和可重定位的 CMake Config 包。目标为 Native3DGS::Engine、Native3DGS::RenderCore、Native3DGS::ModelIo 和必要的私有压缩库。稳定的是本版本源码契约，不承诺跨编译器 STL 二进制 ABI。

SDK 包含公共头、Release 库、预编译 shader、隔离解码 helper、依赖版本与许可证、无 UI 宿主示例、版本文件和部署函数。消费工程只需 find_package(Native3DGS CONFIG REQUIRED)、链接 Engine、调用 native3dgs_deploy_runtime(target)，无需访问本仓库或编译第三方源码。部署函数将 helper 与 shaders 放到宿主可执行文件旁。SDK 发布前必须在独立目录以安装包构建并实际运行消费示例。

## 相机契约

gs::engine::CameraController 使用 double 世界位置、焦点和四元数。输入参数均为物理像素/秒或明确的滚轮步数；不依赖窗口、键盘状态或 WinUI。

- fit_scene 根据中心包围盒、maxScale*max_stddev 和物理 aspect 构造保守包围球，按较小的水平/垂直半 FOV 适配，保留 10% 边距。无法构造 render-core 可接受的有限相机时返回结构化错误，不改变旧状态。
- orbit/平移/指数 dolly；pitch 限制到 ±(π/2-0.001)，缩放有限上下界。Fly 模式支持 look 和本地 right/up/forward 运动，Shift 为 4 倍速度，单次 dt 最大 0.1 秒。
- 模式切换保持当前姿态；reset 恢复首次 fit 保存的相机；resize 不改变姿态，显式 fit 才重算距离。一个 CameraController 对应一个模型，新模型使用新 controller；后续 fit 保留首次 fit 的 reset 基线和当前模式。
- 非有限输入、零/超限 viewport、不可支持场景均被拒绝；固定输入回放的数值可重复。
- FlipX、FlipY、FlipZ 命令分别以 `CameraCommand::flip_enabled` 设置三个世界坐标轴的显示镜像，可同时启用多个轴。镜像以模型包围盒中心为固定轴心，轨道平移或自由飞行后仍不移动该轴心。引擎保持原始场景只读，通过镜像视点和相机朝向生成渲染视图。开启奇数个轴时宿主对合成画面做水平镜像以补偿反射；偶数个轴时无需屏幕镜像。翻转位掩码跨模型加载保留，reset/fit 和模式切换不改变它。

## 引擎契约

IEngine 以线程安全、非阻塞命令向桌面宿主提供 open/cancel/close、surface queue/attach/detach/resize、相机命令和快照/事件轮询。create_engine 完成初始设备创建后返回结构化成功/失败。引擎拥有一个串行加载线程和一个渲染线程；阻塞 load、GPU 等待及 renderer 销毁均在后台执行。

- open 每次返回递增 RequestId；仅保留最新一个未启动请求。新请求停止旧解码并撤销旧上传；旧活动场景仍显示。过期解码结果/进度和 renderer ticket 无权改变当前请求。
- 成功解码后在后台 fit 初始相机并上传；只有 SceneReady 才提交活动模型、相机及 reset 基线。取消/失败恢复旧模型及旧相机。close 取消待命工作，SceneCleared 的 ticket 匹配才清除活动快照；恢复过程中 renderer 尚无活动 scene 而报告 ticket 0 时，正在 Closing 的引擎也接受清场完成。取消和关闭清空请求专属进度。
- 快照包含 phase、当前/活动 RequestId、活动 ticket、加载和上传进度、活动相机、三轴翻转位掩码、统计、surface generation 与结构化错误。事件轮询供宿主处理 surface 生命周期；进度通过快照提供。队列有界，丢弃计数公开，不让未轮询宿主造成无限内存增长。
- DeviceLost 后渲染线程暂停。宿主在 UI 线程解绑并释放旧 swapchain/queue/device，再调用 acknowledge_device_release(generation)；bool 返回值表示当前等待的 generation 是否接受了确认，过期/重复确认返回 false。只有确认后才重建同 LUID 设备。确认等待最长 10 秒，超时为持续错误。新 generation 的 SurfaceRebindRequired 由宿主绑定新 surface；renderer 首次 Present 后才恢复 Ready。
- request_shutdown 非阻塞，停止加载/输入/新命令；渲染线程完成 fence 安全解绑及 renderer 销毁后进入 Stopped。wait_until_stopped 有调用者指定超时；UI 应轮询或在后台等待。析构负责最终 join，应在已 Stopped 后销毁，或交给后台线程。
- 所有错误通过值返回或快照提供；不从后台线程调用宿主/UI 回调。

## 核心改进约束

投影细轴使用稳定 Gram 行列式/主特征值公式，避免 middle-disc 的消减误差；默认质量保持不变。shader 对非有限属性、非正尺度和非法 opacity 做逐点拒绝计数。相机不变时继续复用排序；最新相同相机/尺寸不应触发无效重排。上传页和 copy fence 封装在 scene 内部组件，renderer 负责事务激活和 surface 生命周期；不得依赖解码器。

## 验证

先用回归测试复现极端各向异性、surface attach/resize 顺序及错误恢复问题；运行现有全部模块测试。新增相机数学/回放、请求替换/取消/关闭、旧模型保留、零 viewport、设备释放确认、shutdown、事件上限和安装包消费测试。测试必须验证状态及输出，不能只验证方法能调用。

性能使用独立 Release 基准，记录质量、shader 哈希、设备/驱动、数据/相机、GPU 阶段时间；优化前后须在相同配置比较。Spark SSIM、WinUI 可见合成、整机 PresentMon 与跨厂商硬件门槛仍按总技术计划，缺少外部数据时明确未验收。
