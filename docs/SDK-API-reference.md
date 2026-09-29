# Windows SDK 接口手册

本手册以 `include/` 下的公开 C++20 头文件为准。命名空间为 `gs::engine`、
`gs::io`、`gs::render` 和 `gs`。SDK 是 MSVC Release `/MD` 静态库接口，
不是跨编译器的 C ABI。一般宿主只需 `Native3DGS::Engine`；直接使用
`ModelIo` 或 `RenderCore` 时由宿主自行管理线程、Surface 和恢复流程。
构建、部署和最小工程见 [SDK-guide.md](SDK-guide.md)。

## 场景与错误类型

`gs::SceneHandle` 是 `std::shared_ptr<const gs::SplatScene>`。`SplatScene` 的
`count`、`shDegree`、`sourceFormat`、`worldOrigin`、`bounds`、`maxScale`
描述场景；`centerLocal`、`scale`、`rotation`、`opacity`、`rgb0`、`shRest`
是只读 float span，均依赖同一对象的 `storage` 保持映射有效。
不得在释放 `SceneHandle` 后保存这些 span。PLY 坐标可在加载请求中选
`Coordinates::Rdf`（默认）或 `Rub`；引擎使用 RUB 世界坐标。

`io::LoadResult = std::variant<SceneHandle, io::LoadError>`；加载错误包含
`code`、`stage`、可选 `byteOffset`、`diagnostic`。`LoadErrorCode` 包括
`NotFound`、`AccessDenied`、`IoFailure`、`UnsupportedFormat`、
`UnsupportedVersion`、`UnsupportedFeature`、`InvalidHeader`、
`TruncatedData`、`InvalidAttribute`、`EmptyScene`、`ResourceLimit`、
`OutOfMemory`、`Cancelled`、`Timeout`、`DecoderFailure`、
`DecoderCrashed`、`ObserverFailure`。
`render::RenderError` 含 `code`、`hresult`、`diagnostic`；错误码包括
`UnsupportedDevice`、`InvalidSurface`、`InvalidCamera`、`InvalidScene`、
`InvalidQualityConfig`、`ResourceLimit`、`OutOfVideoMemory`、
`UploadFailed`、`ShaderFailure`、`DeviceRemoved`、`SurfaceLost`、
`Cancelled`、`InternalFailure`、`GpuTimeout`。展示错误时保留原始码和诊断。

## 引擎接口

包含 `<native3dgs/engine.h>`，链接 `Native3DGS::Engine`。
`EngineConfig{quality, viewport, event_capacity}` 配置画质、初始物理尺寸
和有界事件队列容量。`create_engine` 返回引擎或 `RenderError`；失败时
不会暴露 Surface 队列。`IEngine` 由返回的 `unique_ptr` 独占。

| 调用语法 | 作用与返回 | 注意事项 |
| --- | --- | --- |
| `auto result = gs::engine::create_engine(config);` | `variant<unique_ptr<IEngine>, RenderError>`；创建 D3D12 引擎并做排序自检。 | `config` 可省略；创建失败检查错误，勿提取引擎。 |
| `engine->open(gs::io::LoadRequest{path, coordinates, limits})` | `variant<RequestId, RenderError>`；开始异步加载。 | 成功仅表示请求已接受；以快照 `Ready` 或 `SceneReady` 事件判断可见。新模型首帧成功前保留旧场景。 |
| `engine->cancel(request_id)` | 请求取消，无返回值。 | 仅当前请求有效；观察快照或事件取得最终状态。 |
| `engine->close()` | 异步清除场景，无返回值。 | 等待 `SceneCleared` 后才可假定场景资源释放；不是引擎析构。 |
| `engine->camera_command(command)` | `optional<RenderError>`；无值表示接受。 | 需要活动场景；命令字段见下节。 |
| `engine->addref_surface_queue(generation)` | 返回持有一个 COM 引用的 `ID3D12CommandQueue*`，失败为 `nullptr`。 | 使用快照中的当前 `surface_generation`；宿主使用后 `Release()`。 |
| `engine->attach_swapchain(generation, swapchain)` | `optional<RenderError>`；绑定 DXGI composition swapchain。 | 引擎自持引用；宿主在 UI 线程将 swapchain 绑定到 `SwapChainPanel`。 |
| `engine->detach_swapchain(generation)` | 请求解绑，无返回值。 | 收到 `SurfaceDetached` 后再释放旧 Surface/设备相关宿主引用。 |
| `engine->resize(generation, gs::render::Viewport{w, h})` | `optional<RenderError>`；更新物理像素尺寸。 | `0 x 0` 暂停呈现；DPI 变化时传物理像素。 |
| `engine->acknowledge_device_release(generation)` | `bool`；确认宿主已释放失效代际的 GPU 引用。 | `DeviceLost` 后先解绑并释放旧 swapchain/queue，再确认；等待 `SurfaceRebindRequired` 绑定新代际。 |
| `engine->snapshot()` | 返回 `Snapshot` 值副本。 | 权威状态；事件队列可能丢事件。可从 UI 线程轮询。 |
| `engine->poll_events()` | 取走当前 `vector<EngineEvent>`。 | 定期调用；事件有界，`snapshot().dropped_events` 非零时重新同步快照。 |
| `engine->request_shutdown()` | 发起停止，不阻塞。 | 停止向引擎发命令。 |
| `engine->wait_until_stopped(timeout)` | `bool`；等待 `Stopped`。 | 在后台线程调用；超时后勿假定资源已释放。析构会等待工作线程。 |

`Snapshot` 的 `phase` 为 `Empty`、`Loading`、`Uploading`、`Ready`、
`Closing`、`Recovering`、`Failed`、`Stopping` 或 `Stopped`；
`current_request` 与 `active_request` 区分待加载和已显示模型。
`active_ticket`、`surface_generation`、`load_progress`、
`upload_done/upload_total`、`active_scene`、`camera`、`flip_axes`、
`stats`、`error` 均是观察值。`active_scene` 含点数、SH 度数、格式、原点、
边界和最大 scale。`EngineEvent::Kind` 为 `SceneReady`、`SceneFailed`、
`SceneCleared`、`DeviceLost`、`SurfaceRebindRequired`、`SurfaceDetached`、
`DeviceRestored`、`Fault`、`Stopped`；事件另带请求 ID、Surface 代际和错误。

### 相机命令

语法：`engine->camera_command(gs::engine::CameraCommand{.action = action,
.x = x, .y = y, .z = z, .seconds = seconds, .fast = fast,
.flip_enabled = enabled})`。省略的字段保持默认值。固定模式使用 `Orbit`，
自由模式使用 `Look` 和 `Fly`。

| `CameraAction` | 参数与行为 |
| --- | --- |
| `Orbit` | `x/y` 为鼠标物理像素位移，绕模型旋转；左拖使模型向左转。 |
| `Pan` | `x/y` 为物理像素位移，平移固定模式焦点。 |
| `Dolly` | `x` 为滚轮步数，调整固定模式距离。 |
| `Look` | `x/y` 为物理像素位移，自由模式转向。 |
| `Fly` | `x/y/z` 分别是右、上、前方向输入，`seconds` 是本次时间步长，`fast` 加速。 |
| `Fit` | 将活动模型重新适配当前视口。 |
| `Reset` | 恢复场景初始视角。 |
| `OrbitMode` / `FlyMode` | 切换固定/自由浏览模式。 |
| `FlipX` / `FlipY` / `FlipZ` | `flip_enabled` 设置对应轴的绝对开关状态，可组合。 |

翻转只影响显示视角，不修改场景数据。启用奇数个翻转轴时，宿主须将最终合成图像
水平镜像；偶数个翻转轴无需镜像。以 `snapshot().flip_axes` 的位 1/2/4
读取 X/Y/Z 当前状态。`gs::engine::CameraController` 是可选的独立数学接口；
应用通过 `IEngine::camera_command` 调用时无需单独实例化它。

| `CameraController` 调用语法 | 含义与注意事项 |
| --- | --- |
| `camera.fit_scene(scene, quality, viewport)` | 按场景边界初始化视角，返回可选 `RenderError`；需有效场景与物理尺寸。 |
| `camera.resize(viewport)` | 更新物理尺寸，返回可选错误；不主动移动相机。 |
| `camera.orbit(dx_px, dy_px)` | 固定模式绕焦点旋转，返回可选错误。 |
| `camera.pan(dx_px, dy_px)` | 固定模式平移，返回可选错误。 |
| `camera.dolly(wheel_steps)` | 固定模式调整距离，返回可选错误。 |
| `camera.look(dx_px, dy_px)` | 自由模式改变朝向，返回可选错误。 |
| `camera.fly(FlyMotion{right, up, forward, fast}, seconds)` | 自由模式移动，返回可选错误；按真实时间步长调用。 |
| `camera.set_mode(gs::engine::ViewMode::Orbit)` | 切换固定模式；自由模式传 `ViewMode::Fly`，返回可选错误。 |
| `camera.set_flip_axes(mask)` | 绝对设置三轴翻转位掩码 1/2/4，无返回值。 |
| `camera.reset()` | 恢复适配场景时的视角，无返回值。 |
| `camera.camera()` / `camera.mode()` / `camera.flip_axes()` / `camera.has_scene()` | 分别读取显示相机、模式、翻转掩码和场景状态。 |

### Surface 和关闭顺序

```cpp
auto created = gs::engine::create_engine();
if (auto *error = std::get_if<gs::render::RenderError>(&created)) {
    // 将 error->code 和 diagnostic 报告给宿主
    return;
}
auto engine = std::move(std::get<std::unique_ptr<gs::engine::IEngine>>(created));
const auto generation = engine->snapshot().surface_generation;
ID3D12CommandQueue *queue = engine->addref_surface_queue(generation);
// 用 queue 创建符合 SDK-guide.md 约定的 IDXGISwapChain3，然后 queue->Release()。
// 将 swapchain 挂到宿主 UI 后调用 attach_swapchain(generation, swapchain)。
// 退出：detach_swapchain -> SurfaceDetached -> request_shutdown -> wait_until_stopped。
```

设备丢失时旧队列与 swapchain 不能跨代际复用。所有引擎命令必须先于引擎对象
析构完成；不要从同一线程同时析构和发命令。

## 独立模型加载接口

包含 `<model-io/model_loader.h>`，链接 `Native3DGS::ModelIo`。

| 调用语法 | 作用与注意事项 |
| --- | --- |
| `auto loader = gs::io::make_model_loader();` | 返回 `unique_ptr<IModelLoader>`；加载器不依赖 D3D12。 |
| `loader->load(request, stop_token, progress_sink)` | 同步解码 PLY/SPZ；返回 `LoadResult`。从工作线程调用，传 `std::stop_token` 取消；`ProgressSink(const LoadProgress&)` 在加载线程运行，不要直接操作 UI。 |

`LoadRequest{path, plyCoordinates, limits}` 的 `path` 是本机文件路径；
`LoadLimits{maxInputBytes,maxSplats,maxSceneBytes}` 只能收紧默认限制。
`LoadProgress` 有阶段、已读字节、可选总字节数。加载器使用独立解码进程，
需要随宿主部署 `model-io-helper.exe`。异常观察器转为 `ObserverFailure`；
取消、损坏文件和资源不足均通过 `LoadError` 表示。

## 独立渲染接口

包含 `<render-core/renderer.h>`，链接 `Native3DGS::RenderCore`。
宿主须提供渲染线程、事件分发、Surface 生命周期和设备恢复。
`QualityConfig` 的 `sort_mode` 为 `Radial` 或 `ViewDepth`；
`sh_degree_cap` 限制最高 SH 度数，`max_stddev` 控制高斯范围，
`min_alpha` 控制透明度门限，`covariance_blur_px2` 为像素协方差模糊，
`max_pixel_radius_px` 限制投影半径，`premultiplied_alpha` 对应合成方式。
`allow_memory_mitigation` 默认为 true；显存预算不足时自动逐级降低 GPU 中的 SH 阶数，最低到 0 阶，仍不足时把点间隔逐级加倍。`sh_degree_cap` 是手动 SH 上限，`point_stride` 是手动抽样间隔（默认 1），`max_point_stride` 是自动抽样上限（默认 16）；两个间隔都只能为 1、2、4、8、16，且 `point_stride <= max_point_stride`。`max_point_stride=1` 只允许自动降 SH；`allow_memory_mitigation=false` 禁止自动缓解，但保留手动设置。调用方应在 `RenderStats::memory_mitigation` 为 true 时向用户提示，并用源/实际阶数、点数和 `active_point_stride` 显示实际质量。`CameraState` 含 RUB 位置、
XY ZW 四元数、垂直 FOV、近远裁剪面；`Viewport` 单位为物理像素。

| 调用语法 | 作用与注意事项 |
| --- | --- |
| `gs::render::create_renderer(quality, event_sink)` | 返回 `variant<unique_ptr<IRenderer>, RenderError>`；`EventSink(const RendererEvent&)` 在渲染线程回调，不要回调时重入渲染器。 |
| `renderer->surface_generation()` | 返回当前 Surface 代际。 |
| `renderer->addref_surface_queue(generation)` | 返回自带 COM 引用的队列，宿主 `Release()`。 |
| `renderer->attach_swapchain(generation, swapchain)` | 返回可选错误；引擎保留自己的 swapchain 引用。 |
| `renderer->detach_swapchain(generation)` | 请求解绑；等待 `SurfaceDetached` 事件再释放宿主资源。 |
| `renderer->upload_scene(scene, camera)` | 返回上传票据或错误；`SceneHandle` 保持场景内存有效。 |
| `renderer->cancel_upload(ticket)` | 取消指定上传票据。 |
| `renderer->clear_scene()` | 清除当前场景；以 `SceneCleared` 事件确认完成。 |
| `renderer->set_camera(ticket, camera)` | 设置对应票据视角，返回可选错误。 |
| `renderer->resize(generation, revision, viewport)` | 调整视口；同代际 `revision` 须递增。 |
| `renderer->render_frame()` | 提交并呈现一帧；只能在单一渲染线程调用。 |
| `renderer->get_stats()` | 返回帧时间、排序、上传、内存预算与恢复计数的快照。 |

`RendererEvent::Kind` 包含 `UploadProgress`、`SceneReady`、`SceneFailed`、
`DeviceLost`、`SurfaceRebindRequired`、`SurfaceDetached`、`DeviceRestored`、
`SceneCleared`、`RenderFault`、`FatalDeviceError`。设备丢失后先释放宿主
持有的旧设备/队列/Surface 引用，再调用下一帧。析构渲染器必须在渲染线程，
且应先停止其他命令调用。`RenderStats` 的 `cpu_frame_ms`、`gpu_frame_ms`、
`gpu_sort_ms`、`gpu_draw_ms`、`present_call_ms` 是可选测量值；
`sort_shader_mode`、`sort_self_test_passed` 与 `wave32_fallback_hr`
可用于诊断设备变体。`presented_frame_id` 是最近呈现帧 ID；
`candidate_splats`、`drawn_splats`、`rejected_projection_splats`
分别为候选、绘制和投影拒绝点数；`sort_reuse_count`、`sort_pass_count`
为排序复用和轮次；`completed_upload_bytes` 为已上传字节；
`wrong_thread_frame_calls` 为跨线程误调用计数；
`local_budget_bytes/local_usage_bytes` 与
`nonlocal_budget_bytes/nonlocal_usage_bytes` 为 GPU 预算与使用量；
`device_recovery_count` 为设备恢复次数。`active_ticket` 为当前 GPU 场景票据，0 表示无活动场景；`active_source_sh_degree`/`active_source_splats` 与 `active_sh_degree`/`active_splats` 分别为源质量与实际 GPU 质量，`active_point_stride` 为源点抽样间隔。`memory_mitigation` 仅表示由预算触发的自动降低，手动上限或抽样可使实际质量下降而不置位。场景替换时可选帧时间和 `presented_frame_id` 清空，直到新活动场景的帧完成；旧场景的在途帧不会覆盖新统计。这些值不改变 `SplatScene` 中的源数据。
