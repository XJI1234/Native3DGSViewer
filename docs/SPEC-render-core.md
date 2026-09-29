# Spec: render-core（Windows 原生 3DGS 渲染核心）

状态：独立模块与桌面基础集成已实现，模块验证见[验证记录](render-core-verification.md)；Spark 画质与整机性能仍待验收；2026-09-26。当前由 [engine/SDK](SPEC-engine-sdk.md) 计算初始相机并调用本模块，下文早期“桌面层”表述指引擎宿主侧。本文规定首期单模型渲染。

## 实现参数锁定

- GPU 排序选用 MIT 许可的 AMD FidelityFX Parallel Sort，固定 `0c539948c8d196ae338d91efbc8ca495f1ea0d1d`，使用原版八次 4-bit LSD pass 替代原提议四次 8-bit pass；保留稳定 key/value、同键原始索引顺序及纯 GPU 帧图。GPU 对照测试必须包含 800 万键。
- 最低 D3D12 feature level 12.0、Shader Model 6.0、WaveOps；支持 wave lane 最小值 8、最大值 32 的设备，不回退软件适配器。排序在原有 wave16+ 路径、经设备验证的 SM6.6 固定 wave32 路径和 wave8 安全路径间选择。DXC 来自锁定 Windows SDK 10.0.26100.0，构建编译并记录 SHA-256。
- 交换链固定 BGRA8 UNORM、flip sequential、2-3 buffers、stretch、premultiplied alpha。训练 RGB 在 SH 求值后 clamp 到 [0,1]，直接输出到 UNORM，保留当前 Viewer 的显示编码色值域；不启用 sRGB RTV，不声称已经通过 Spark 色彩对照。
- Gaussian quad 使用 max_stddev 截断和半径上限；近面相交的支撑范围保守保留并将投影深度钳到近面，完全位于相机后方或远面之外的支撑范围剔除。质量上界为 max_stddev<=8、blur<=64、radius<=16384，最大视口 16384x16384。
- 所有 fence CPU 等待仅在渲染线程进行，单次上限 5 秒；设备恢复最多两次，surface 重绑等待 10 秒。

## 1. 目标与边界

`render-core` 把不可变 `gs::SplatScene` 显示到 DXGI 交换链。它拥有 D3D12 设备、队列、GPU 场景资源、每帧排序和绘制、GPU 计时及设备故障恢复；不解析文件、不创建 WinUI 控件、不解释鼠标手势。首期 Windows 11 x64、单模型、透视相机、标准 PLY/SPZ 规范化数据与 SH 0-3 阶。正交相机、多视角、跨模型排序、LoD、编辑与动画留在后续阶段。

成功条件：正确显示所有受支持阶数和透明叠加；加载/替换/取消不出现半成品场景；窗口变化与设备移除后有确定结果；同画质性能达到总计划第 5 节的 RTX 3080 门槛。性能门槛须实测，GPU 排序架构本身不保证胜过 Web Viewer。

## 2. 依赖与数据契约

依赖仅为 C++20、Windows SDK 的 D3D12/DXGI/DirectXMath、DXC、`splat-types`。公共头不引用 WinUI、miniply、SPZ 或 Spark 类型。`gs::SceneHandle` 由 `splat-types` 定义为 `std::shared_ptr<const gs::SplatScene>`；输入含 RUB 右手坐标、double `worldOrigin`/中心包围盒与 `maxScale`、float32 局部中心、正尺度、xyzw 单位四元数、训练色值域的 `rgb0`/SH、opacity 与实际 SH 阶数。`rgb0` 允许超出 [0,1]，着色阶段按锁定的颜色转换与 clamp 顺序处理。渲染器不得修改 `SceneHandle` 的 span，也不得假定共享映射在句柄释放后仍有效。

建议的 C++20 边界如下；声明是规格形状，评审时可调整命名，但状态和所有权语义必须保留：

```cpp
namespace gs::render {
struct CameraState {
    double3 position_rub;
    quaterniond orientation_xyzw;  // 局部 -Z 为前方，+Y 为上方
    double vertical_fov_radians;
    double near_plane;
    double far_plane;
};
struct Viewport { uint32_t physical_width, physical_height; };
enum class SortMode : uint8_t { Radial, ViewDepth };
struct QualityConfig {
    SortMode sort_mode = SortMode::Radial;
    uint8_t sh_degree_cap = 3;
    float max_stddev = 3.0f;
    float min_alpha = 0.0f;
    float covariance_blur_px2 = 0.0f;
    float max_pixel_radius_px = 1024.0f;
    bool premultiplied_alpha = true;
};
enum class RenderErrorCode : uint8_t {
    UnsupportedDevice, InvalidSurface, InvalidCamera, InvalidScene,
    InvalidQualityConfig,
    ResourceLimit, OutOfVideoMemory, UploadFailed, ShaderFailure,
    DeviceRemoved, SurfaceLost, Cancelled, InternalFailure, GpuTimeout
};
struct RenderError { RenderErrorCode code; HRESULT hresult = S_OK; std::string diagnostic; };
using UploadTicket = uint64_t;
using SurfaceGeneration = uint64_t;
using ViewportRevision = uint64_t;
struct RendererEvent {
    enum class Kind {
        UploadProgress, SceneReady, SceneFailed, DeviceLost,
        SurfaceRebindRequired, SurfaceDetached, DeviceRestored,
        SceneCleared, RenderFault, FatalDeviceError
    } kind;
    UploadTicket ticket = 0;  // 设备级事件使用 0
    SurfaceGeneration surface_generation = 0;
    uint64_t bytes_done = 0, bytes_total = 0;
    std::optional<RenderError> error;
};
struct RenderStats {
    uint64_t presented_frame_id = 0;
    std::optional<double> cpu_frame_ms, gpu_frame_ms;
    std::optional<double> gpu_sort_ms, gpu_draw_ms, present_call_ms;
    uint64_t candidate_splats = 0, drawn_splats = 0;
    uint64_t rejected_projection_splats = 0;
    uint64_t sort_reuse_count = 0, completed_upload_bytes = 0;
    uint64_t sort_pass_count = 0;
    uint64_t wrong_thread_frame_calls = 0;
    uint64_t local_budget_bytes = 0, local_usage_bytes = 0;
    uint64_t nonlocal_budget_bytes = 0, nonlocal_usage_bytes = 0;
    uint32_t device_recovery_count = 0;
};
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual SurfaceGeneration surface_generation() const = 0;
    virtual ID3D12CommandQueue* addref_surface_queue(SurfaceGeneration) = 0;
    virtual std::optional<RenderError> attach_swapchain(
        SurfaceGeneration, IDXGISwapChain3*) = 0;
    virtual void detach_swapchain(SurfaceGeneration) = 0;
    virtual std::variant<UploadTicket, RenderError> upload_scene(
        gs::SceneHandle, CameraState initial_camera) = 0;
    virtual void cancel_upload(UploadTicket) = 0;
    virtual void clear_scene() = 0;
    virtual std::optional<RenderError> set_camera(
        UploadTicket active_scene, CameraState) = 0;
    virtual std::optional<RenderError> resize(
        SurfaceGeneration, ViewportRevision, Viewport) = 0;
    virtual void render_frame() = 0;
    virtual RenderStats get_stats() const = 0;
};
using EventSink = std::function<void(const RendererEvent&)>;
std::variant<std::unique_ptr<IRenderer>, RenderError>
create_renderer(QualityConfig, EventSink);
}
```

`QualityConfig` 在 `create_renderer` 时校验并冻结，运行期只读；上述默认值是原型初值，M0 对齐 Viewer 后锁定基准配置。`sh_degree_cap` 必须为 0-3，`max_stddev`/`max_pixel_radius_px` 必须有限且大于零，`min_alpha` 在 [0,1)，`covariance_blur_px2` 有限且非负；超出经设备资源预算核定的上界返回 `InvalidQualityConfig`。输入错误由 `create_renderer`、`upload_scene`、`attach_swapchain`、`set_camera` 或 `resize` 同步返回，不入队；异步 GPU/交换链错误通过事件返回。事件回调在渲染线程执行，不持有内部锁，异常在边界捕获并记录；UI 必须转发到 dispatcher。

`upload_scene` 的初始相机由桌面层按包围盒计算，随模型一起待命和激活；`upload_scene`、`set_camera`、`resize` 为线程安全命令入口，验证后只入队且不等待 GPU。`render_frame` 只由渲染线程调用。初始 surface generation 为 1，设备重建时递增；`addref_surface_queue(generation)` 仅在参数匹配且设备就绪时返回已增加引用计数的 direct queue，宿主用完释放该引用。`attach_swapchain` 和 `detach_swapchain` 只处理指定 generation；同一 generation 内 `resize` 还按递增 `ViewportRevision` 只应用最新尺寸。`set_camera` 只作用于对应活动场景 ticket；没有活动场景时不调用它。交换链由 renderer 按 COM 引用计数保留；正常关闭时 `SurfaceDetached(generation)` 确认相关 fence 安全后 UI 才解绑并释放自己的交换链引用。设备移除路径不等待旧 generation 的 `SurfaceDetached`。WinUI 的 `ISwapChainPanelNative::SetSwapChain` 在桌面层执行。`get_stats` 返回最近完成的不可变快照，可能落后一至数帧，不触发 GPU 等待；未完成的时间值为空，不填零冒充实测。

## 3. 设备、线程与 GPU 资源

启动时枚举 DXGI 硬件适配器，排除软件适配器；按配置的 LUID 优先，其次选高性能适配器。要求可创建 D3D12 设备、所需 Shader Model、WaveOps 和 SRV/UAV 格式；能力不足返回 `UnsupportedDevice`，不悄悄改成 WARP 或 WebView。固定 adapter LUID、驱动和 feature 查询结果写入诊断；运行时根据能力选择经过测试的排序变体，不能假定 NVIDIA wave32。若最小 wave lane 小于 16，优先验证 SM6.6 固定 wave32 变体；不支持或自检失败时验证 wave8 安全变体。排序 GPU 读回自检在 renderer 初始化时完成，验证固定夹具的键和值及稳定顺序；失败时返回 `ShaderFailure`，不得继续绘制。`RenderStats` 公开已选择的排序变体及自检通过状态，供宿主记录诊断。

一个渲染线程独占 direct queue、命令分配器/列表、描述符堆、交换链 back buffer、PSO 与帧状态；一个 copy queue 负责分块上传，独立 fence 表示完成。首期不启用并发 compute queue，避免排序和混合之间复杂的跨队列同步。每帧资源用 2-3 个 fence 标记的槽循环利用；只有对应 direct fence 完成才重用命令分配器、上传页、排序缓冲或 back buffer。copy queue 写完默认堆后，direct queue 用 `Wait(copyFence, value)` 建立 GPU 依赖，再做资源状态转换。所有 GPU 资源由 RAII 包装，释放延迟到最后使用它的 fence 完成，禁止 UI 线程直接 `Release` 仍在使用的资源。

`SceneHandle` 从 `upload_scene` 入队起一直保留到最后一块 copy fence 完成。首期对当前活动场景继续保留 CPU 句柄，以便设备恢复时重新上传同一快照；取消的待命场景在 copy fence 安全点释放。此选择提高 RAM 常驻量，需计入预算并在 M3 实测。异步上传对 UI 显示阶段/真实已完成 copy fence 的字节进度，不在 UI 线程拷贝整场景。`SceneCleared` 事件的 `ticket` 必须是实际被清除的活动场景 ticket；若清除时无活动场景则为 0，桌面层据此过滤过期完成事件。

`QueryVideoMemoryInfo` 的 local/non-local 预算和当前进程用量仅作动态预检，实际创建资源及 residency 仍可能失败。以 `Budget - CurrentUsage` 求当前增量余量；旧场景与已有 back buffer 已在 `CurrentUsage` 中，不能再次从余量扣除。启动上传前受检估算新场景属性/SH 缓冲、索引与 radix 临时缓冲、额外帧资源、可能同时存在的新旧尺寸 back buffer 及上传页的增量峰值；按所属内存段分别比较，默认最多使用可用余量的 80%。预算不足返回 `OutOfVideoMemory` 并保留旧场景，不靠截断点数或 SH 降级。创建失败时清理待命资源、采集新预算并报告增量需求/可用容量；绝不通过无限重试造成卡死。D3D12MA 可在 M0 许可/版本核对后用于堆分配，但不得改变 fence 所有权与预算语义；无此依赖时先用 D3D12 committed resource 实现正确性原型。

集成显卡是否采用统一内存由 `D3D12_FEATURE_ARCHITECTURE1.UMA` 判定；不支持该查询时回退到 `D3D12_FEATURE_ARCHITECTURE.UMA`，不由 `DedicatedVideoMemory` 数值猜测。UMA 设备的上传页已经包含在 local 增量估算中，不再额外要求 non-local 预算或其查询成功；非 UMA 设备仍分别检查 local 场景资源和 non-local 上传页。任何预检拒绝都须返回阶段、UMA 标记、local/non-local 预算与用量、估算需求，供宿主日志区分预算压力与实际 D3D12 分配失败。物理内存总量不替代 DXGI 动态预算，也不保证任意规模的模型都能加载。

### 2026-09-30 显存缓解扩展

`QualityConfig::allow_memory_mitigation` 默认为 true。先按 `min(scene.shDegree, sh_degree_cap)` 保持完整质量预算；若 DXGI 当前增量预算不够，依次尝试更低的 SH 阶数，直到 0 阶。0 阶仍不足时才按源点索引的固定间隔 2、4、8、16 抽样，仅上传所选点；每次加载和恢复得到同样的选择，不逐帧改变点集。CPU 解码结果始终完整，原始文件不改写。仍保留 80% 动态预算规则，间隔 16 仍不足时照常拒绝并保留旧场景。`sh_degree_cap` 是宿主可设置的手动上限；`allow_memory_mitigation=false` 可禁止自动缓解。每次上传在准入和实际分配前重新核对预算；设备恢复沿用该场景已选阶数和抽样间隔。`RenderStats::active_sh_degree`、`active_splats`、`active_point_stride` 与 `memory_mitigation` 暴露实际结果，桌面查看器须提示画质或点数变化。此段替代上文“预算不足时不靠 SH 降级”的首期限制，不允许无提示的静默降级。抽样模式不可用于等画质性能验收。

## 4. 场景上传与原子激活

场景状态为 `Queued -> Budgeted -> Uploading -> GpuReady -> FirstFrameReady -> Active`，终态另有 `Failed/Cancelled`。旧 `Active` 在新场景完成首次排序与一次成功 Present 之前持续保留。上传前再次校验 `count`、每数组长度、SH 阶数及受检内存计算；GPU 布局版本由 renderer 内部定义并由 shader 编译常量验证，不暴露给 `model-io`。

GPU 使用两个只读字节缓冲：基础属性为每点 56 字节，仅实际渲染阶数所需的 SH 系数单独存放。每个缓冲的字节数必须在 32 位 shader 地址范围内；场景总字节数可超过 4 GiB。保留源全精度 float32 作为正确性基线；后续量化属于单独画质评审。按 64 MiB 页拷贝到 upload heap，copy queue 上传到 default heap；进度表示完成 copy fence 的字节，不把 CPU memcpy 当作已上传。每页和总量都用 64 位受检计算。切换时生成新场景自己的索引/排序缓冲，不复用旧场景尚在飞行的缓冲。投影使用二维线程组网格处理超过 65,535 组的场景。

取消 ticket 只撤销未提交工作；已提交的 GPU 命令无法强制取消，标为 abandoned 并在 fence 后回收。过期 ticket 的完成事件不得激活模型。激活在渲染线程帧边界一次性替换 `SceneGpuHandle` 和初始相机快照，首次 Present 成功后才发出 `SceneReady`；若 Present 失败则恢复旧活动快照或转入设备恢复。旧 GPU 场景延迟到最后引用它的 direct fence 完成再释放。失败、取消、OOM 时保持旧场景和相机。`clear_scene()` 先取消待命 ticket，在帧边界卸载活动 GPU/CPU 场景，相关 fence 安全后发 `SceneCleared`；这是显存紧张时明确的用户操作，不与失败回退混用。设备移除是例外：旧 GPU 资源也失效，但保留 CPU 快照、显示恢复状态并尝试重建。

窗口不可见或视口为 0x0 时不执行 Present；已上传的待命场景停在 `FirstFrameReady`，事件说明等待有效 surface。恢复后才提交首次画面并发 `SceneReady`，取消和关闭命令仍可处理，不能因等待可见性而阻塞工作线程。

## 5. 每帧图与数学约定

帧图固定为：接收最新相机/尺寸 -> 生成可见候选和排序键 -> GPU 稳定 radix 排序 -> 读取排序索引实例化 quad -> 椭圆投影与 SH 颜色 -> Gaussian 像素权重/预乘 alpha 混合 -> Present。所有帧内 pass 使用显式 resource state 与 UAV barrier；CPU 不读取每帧深度或排序结果。场景与配置无变化、相机未变时可以复用上一排序，但复用条件和次数写入统计；持续移动的基准必须触发排序。

相机使用 double 世界位置和模型 double 原点先在 CPU 求差，再以 float 相机相对坐标送 GPU，避免巨大世界坐标直接量化。右手、+Y 上、视线局部 -Z；投影深度映射 D3D `[0,1]`。尺寸全部用物理像素，宽/高为零时暂停提交并保留场景。近远面、FOV、四元数、尺寸和浮点有限性在命令边界验证；非法状态保留上一有效相机并发出 `InvalidCamera`，不得向 shader 传播 NaN。

GPU 先用视锥与经 Gaussian 最大截断半径扩大的屏幕范围裁剪，近面交叉时采用保守处理；不能只检查中心而丢失可见椭圆。生成 32 位键和原始索引，剔除点以 UINT_MAX 标记；稳定排序后有效候选位于数组前部，GPU 计数直接驱动 ExecuteIndirect，不读回可见集。默认按相机相对径向距离平方从远到近，诊断选项为视深度。径向模式将最大坐标绝对值限制到 1e19，超出范围或非有限投影逐点拒绝并计数；有限非负 float32 位模式反序编码。视深度为 max(-view.z, 0)，采用相同单调编码；零距离使用 UINT_MAX-1，避免与剔除标记冲突。对相同键以原始 splat 索引确定顺序，同一适配器/驱动/配置下结果须跨帧确定；跨适配器的浮点键近似差异用图像容差验收。锁定 FidelityFX 的八次 4-bit LSD radix pass，包含直方图、前缀和与稳定 scatter；不能用无序原子追加破坏同键稳定性。所有中间缓冲从有界场景数量预分配，key/value 按完整 512 键块补齐；0/1、同键、线程组边界、百万及 800 万键已与 CPU stable_sort 对照。若所选实现达不到整帧预算，记录证据并评审另一条 GPU 排序路线，禁止逐帧读回 CPU 排序。

每点 `Sigma3 = R diag(scale^2) R^T`，由透视投影 Jacobian 得 `Sigma2 = J Sigma3 J^T`；对角可加可配置像素方差用于与 Spark 的预模糊/抗锯齿参数对齐。验证正定性，求特征轴后按固定 `max_stddev` 与像素半径上限生成 quad；异常的投影结果只丢弃该帧该点并计数，正常输入不得造成整帧崩溃。SH 用相机相对方向、实际 `shDegree` 计算颜色，0 阶即 `rgb0`；方向基、系数符号和 clamp 顺序以合成图及 Spark 对照固定，不能假设 GL 与 HLSL 约定自动一致。像素 alpha 用 Gaussian 衰减与同值 `min_alpha` 截断；默认预乘 alpha，固定从远到近、`ONE / INV_SRC_ALPHA` 等价的 RGB/alpha 合成配置。sRGB/线性转换、RTV 格式和 UI 合成空间须在 M0 固定并用色条截图验证，避免靠错误 gamma 获得较快帧时间。

首期仅 splat 画面，不和 mesh 深度缓冲混合；默认关闭深度写入。Spark 2.0 提供更多 `falloff`、blur、depth/扩展参数，首期只为等画质样本冻结所需配置；不能声称全部实验参数已等价。质量参数连同 shader 哈希写入截图/性能记录。

## 6. Surface、故障与诊断

宿主的 DXGI factory 用 renderer 的 direct queue 创建 `CreateSwapChainForComposition` flip-model 交换链，WinUI 的 `SwapChainPanel` 通过 native interop 绑定。具体 BGRA 格式、alpha 模式、buffer count 与 scaling 由 M0 三角形原型及系统文档锁定。resize 消息带 generation，在渲染线程等待相关 back-buffer fence 后调用 `ResizeBuffers` 并重建 RTV；旧 generation 的 resize 被丢弃。DPI 变化产生新的物理尺寸，独立更新相机 aspect，不改变相机姿态。最小化/不可见或零尺寸时停止 Present，恢复后重新确认 surface 与 back buffer。

`Present`、资源创建和 fence 检查若报告设备移除，记录 `GetDeviceRemovedReason` 与可用 DRED breadcrumbs/page-fault 信息，停止提交并发布 `DeviceLost`。设备移除后旧 fence 可能永远不完成；此路径停止等待旧设备 fence，废弃旧命令/资源并在受控销毁中释放其引用，不进入正常的 `SurfaceDetached` 等待。然后在有界次数内重新枚举同一适配器并重建设备、队列和 PSO，发布 `SurfaceRebindRequired(generation)`。桌面宿主取得新队列、创建新的 composition swapchain，在 UI 线程重新 `SetSwapChain` 后调用 `attach_swapchain`。renderer 此时用保留的活动 `SceneHandle` 重新上传并在首次成功 Present 后发 `DeviceRestored`。重绑请求有超时且仅接受当前 generation；若适配器、surface 重绑或重传再次失败，进入 `FatalDeviceError`，UI 显示错误和重新初始化/退出入口，不呈现无反馈黑屏。surface 失效与 device loss 分开分类；不会因为一次普通 resize 重启解码。

`RenderStats` 的完整结构在实现时扩展为排序/绘制 GPU 时间、CPU 帧提交与 `Present` 调用耗时、候选/绘制 splat 数、排序复用次数、上传字节、local/non-local budget/usage 和设备重建次数；字段带帧号及可用性标记。`Present` 不是 GPU pass，不把 CPU `Present` 调用时间伪装为 GPU 时间；显示节奏由 PresentMon 单独采集。GPU timestamp 在完成对应 fence 后异步读取；常规日志限速并不写模型内容或完整文件路径。Debug Layer、GPU validation、DRED 用于调试构建或受控诊断，性能基准使用 Release 且关闭 GPU validation。PIX 与 PresentMon 保存原始采样，统计定义沿用总计划。

## 7. 工程、命令与验证

建议 `include/render-core/` 放公共契约，`src/render-core/device/` 放设备/交换链，`scene/` 放上传与预算，`passes/` 放帧图，`shaders/` 放 HLSL，`tests/render-core/` 放 CPU 数学、GPU readback 与截图测试，`bench/` 放可重放相机路径。源文件 C++20，类/枚举 PascalCase，函数/变量 snake_case，DXC 编译产物和 root signature 由构建生成；固定编译目标、警告级别及 hash。仅在测试中允许 GPU readback，生产帧图不读回排序数组。

解决方案已支持下列构建与全量测试命令；独立/联合测试及结果见验证记录：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
```

| 层级 | 验收 |
|---|---|
| CPU 契约/数学 | 相机、原点重定位、协方差、SH、质量配置边界、增量预算与受检乘法；合成样本逐值比对 |
| GPU 正确性 | 可见集、stable radix 的 0/1/同键/距离键溢出边界/线程组边界/百万/800 万点测试与 CPU 参考排序一致；远近顺序与同键原始索引顺序确定，调试层无报错 |
| 图像 | 固定视角覆盖 SH 0-3、重叠透明、近面/画面边缘、远原点、小/大椭圆；与 Spark 同画质截图 SSIM >= 0.95 并人工检查 |
| 生命周期 | 重复加载/取消/替换/关闭 100 次、resize/DPI/最小化、过期 ticket/generation、设备移除与永不完成的 fence 注入、预算变化与资源创建失败；无半成品、悬挂引用、死等或累积泄漏 |
| 性能 | 总计划小中大矩阵、相机路径、1920x1080 物理像素，逐场景达到中位帧时间 <= 0.80 倍基线且 1% low FPS 不下降 |

始终执行：输入与设备能力边界验证、fence 后释放、质量参数可记录、失败保留旧场景。更改 GPU 布局、坐标/颜色约定或默认质量参数先修订规格与基准。禁止：在 UI 线程等待 GPU、静默降画质或点数、将设备移除视作普通 Present 失败而无限重试。

## 参考与待决检查点

- [Spark 渲染入口](../../spark-2.0.0/src/SparkRenderer.ts)、[顶点投影](../../spark-2.0.0/src/shaders/splatVertex.glsl)、[像素混合](../../spark-2.0.0/src/shaders/splatFragment.glsl)仅用于行为对照；其旧版系统设计说明不作为当前排序实现依据。
- [D3D12 编程指南](https://learn.microsoft.com/windows/win32/direct3d12/directx-12-programming-guide)、[DXGI composition swapchain](https://learn.microsoft.com/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)、[DXGI 显存预算](https://learn.microsoft.com/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo)、[DRED](https://learn.microsoft.com/windows/win32/direct3d12/use-dred)。
- [AMD FidelityFX Parallel Sort](https://gpuopen.com/fidelityfx-parallel-sort/) 已固定集成，许可/版本见 third_party/README.md；800 万键稳定性与独立排序耗时见验证记录。
- 待验收：WinUI 3 实际 SwapChainPanel/DPI、完整渲染显存峰值、跨厂商硬件、实际 Viewer 色彩/混合对照与等画质性能。当前 composition swapchain 使用未绑定控件的模块测试 surface。

## 宿主恢复约束

同一适配器上的 D3D12 device 是单例。DeviceLost 回调交付后，宿主须暂停后续 render_frame，完成 UI 线程解绑并释放旧 swapchain、queue 和 device 的引用，再继续渲染调用；renderer 在下一次调用中重建同 LUID 设备并发布 SurfaceRebindRequired。设备移除路径不等待 SurfaceDetached。新 surface 重绑后保留场景重传，首次 Present 后发布 DeviceRestored。销毁 renderer 也在渲染线程执行，先停止所有命令入口调用。
