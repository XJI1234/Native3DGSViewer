# Android SDK 接入指南

当前版本 `0.2.2` 技术预览，最低 Android 10/API 29，发布 ABI 为 `arm64-v8a`，GPU 需支持 Vulkan 1.1。
Adreno 735/750 已完成 GPU 稳定排序自检与分阶段性能采样；画质 SSIM、低内存、跨驱动和热态性能仍待验收。

## Kotlin 接入

将包内 `aar/native3dgs-sdk-release.aar` 放入应用的 `libs/`。应用模块声明：

```kotlin
dependencies {
    implementation(files("libs/native3dgs-sdk-release.aar"))
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
}
```

AAR 的 Manifest 会合并非导出的 `:native3dgs_decoder` Service，不需要文件广域权限。
用 `ACTION_OPEN_DOCUMENT` 取得 `content://` URI；应用仍须遵守 URI 的授权生命周期。
示例调用序列：

```kotlin
val engine = Native3dgsEngine(applicationContext)
val surfaceGeneration = engine.attach(surfaceView.holder.surface)
engine.resize(surfaceGeneration, surfaceView.width, surfaceView.height)
val requestId = engine.open(selectedUri) // 在协程中调用
engine.state.collect { state -> /* 根据 phase/sceneCount/error 更新界面 */ }
engine.orbit(dxPixels, dyPixels)
engine.freeMode()
engine.fly(right, up, forward, seconds)
engine.detach(surfaceGeneration) // SurfaceHolder.surfaceDestroyed
engine.closeAndWait() // 宿主协程中等待原生关闭调用完成
```

固定模式 `orbit` 接收实际手指像素位移；左拖对应模型向左转。`flipAxes` 使用 X/Y/Z
独立布尔值，不改变拖动方向。`open` 在隔离 Service 完成解码、共享场景交付后返回
原生请求 ID；随后状态从 Loading/Uploading 进入 Ready。打开新模型时旧模型保留到
新场景首帧呈现。`cancel(requestId)` 取消原生阶段；挂起的 `open` 协程取消时会取消
Service 阶段。`closeScene` 清空活动模型。应用应在可见时绑定 Surface，尺寸变化时
调用 `resize`，销毁时 `detach`；新 Surface 使用新的代际编号。
`close()` 可从 UI 线程发起并异步执行原生关闭；需要等待原生线程与 GPU 资源释放时，
在非 UI 协程使用 `closeAndWait()`。驱动长期不响应可能使此等待持续较长时间。

## C/NDK 接入

将整个包解压，设置 `NATIVE3DGS_SDK_ROOT` 后在 NDK CMake 中调用
`find_package(Native3DGSAndroid CONFIG REQUIRED)` 并链接
`Native3DGSAndroid::Engine`。公共 ABI 在 `include/native3dgs/android_engine.h`。
`gs_android_create` 的配置与 `gs_android_get_snapshot` 的输出都使用 `struct_size`；
API 版本为 2。`gs_android_open_fd` 接收 PLY/SPZ 文件描述符，函数返回前复制 FD，
解码在引擎后台线程执行；非 seek provider 需要配置应用私有 `temporary_directory`。
`gs_android_open_shared_fd` 接收隔离解码 Service 输出的版本化共享场景 FD。
调用者仍拥有原始 FD，原生引擎在 `gs_android_destroy` 时结束工作。

`gs_android_attach_surface` 接收 `ANativeWindow*`，内部获取独立引用；
`gs_android_detach_surface` 按代际解绑。公开 C 命令可以从任意调用线程发起，
同一引擎的销毁必须在调用者停止其他命令之后进行。GPU 工作由引擎渲染线程独占。
示例见 `examples/c-consumer`。AAR 已含 `.so`；仅原生消费时同时打包
`lib/arm64-v8a/libgs_android_decoder.so` 和 `libc++_shared.so`。

## 状态与限制

`Ready` 表示首帧成功呈现，`Failed`/`Recovering` 可通过快照的 `error` 区分。
没有 Surface 时可以解码，但渲染与 Ready 等待新的 Surface。模型 URI 与逐点数据
不会写入日志。首期只支持单模型、PLY/SPZ、SH 0–3；不编辑、不导出。

当前仍需真机验证固定相机画质、设备丢失、内存压力、30 FPS 与长时间运行。
SH 数据已按设备 `maxStorageBufferRange` 分段。帧资源按 fence 复用，已完成帧的
GPU 投影、排序和绘制计时可用于诊断；这些计时不等于实际显示帧间隔。
Adreno 735/750 可在能力探测和 GPU 稳定排序自检通过后使用 subgroup 排序，失败时
回退原始稳定排序；其他 GPU 使用原始路径。

## Kotlin 公开接口逐项说明

导入 `org.native3dgs.sdk.Native3dgsEngine`、`EngineState`、`EnginePhase` 和
`EngineException`。AAR 自带隔离解码 Service 与原生库。一个引擎实例管理一个
活动模型和一个 Surface；`SurfaceHolder.Callback` 的代际编号不能在实例内复用。
除 `open` 和 `closeAndWait` 是挂起函数外，下列函数均为普通 Kotlin 调用；
失败时抛 `EngineException(code, message)`。`close()` 为异步资源关闭。

| 调用语法 | 含义、返回值和注意事项 |
| --- | --- |
| `Native3dgsEngine(applicationContext)` | 创建引擎并启动状态轮询；初始化失败抛 `EngineException`。持有实例直至 `close`/`closeAndWait`。 |
| `engine.state.collect { state -> ... }` | `StateFlow<EngineState>`；每约 100 ms 更新，主线程可收集。`Ready` 表示首帧已呈现；实际模型仍应以 `sceneCount` 和 `activeRequestId` 判断。 |
| `engine.attach(surface): Long` | 绑定有效 `Surface`，返回新代际；宿主保存该值。内部持有 `ANativeWindow` 引用，不转移调用者 Surface 所有权。 |
| `engine.resize(generation, width, height)` | 设置物理像素视口；尺寸必须为 1..16384 且代际有效。Surface 尺寸变化时调用。 |
| `engine.detach(generation)` | 在 `surfaceDestroyed` 解绑当前代际；旧代际或重复解绑抛错。随后可绑定新 Surface。 |
| `engine.open(uri): Long` | 挂起；用 `ContentResolver` 打开 `content://` 文档，在隔离 Service 解码、交付共享场景后返回原生请求 ID。返回时 GPU 可能仍在上传；观察 `state`。新模型首帧前旧模型继续显示。 |
| `engine.cancel(requestId)` | 取消已返回的原生请求 ID；正在挂起的 `open` 应取消其协程以取消 Service 阶段。过期 ID 会抛错。 |
| `engine.closeScene()` | 清空场景并取消当前隔离解码；引擎可继续打开其他模型。 |
| `engine.orbit(dxPixels, dyPixels)` | 固定模式拖动，参数为实际触控像素位移；左拖使模型向左转。 |
| `engine.pan(dxPixels, dyPixels)` | 固定模式平移焦点，单位为实际像素。 |
| `engine.dolly(steps)` | 固定模式缩放，`steps` 为滚轮/手势步数。 |
| `engine.look(dxPixels, dyPixels)` | 自由模式改变朝向，单位为实际像素。 |
| `engine.fly(right, up, forward, seconds)` | 自由模式局部移动。前三项为方向输入，建议范围 -1..1；`seconds` 为单次时间步长。W/S 对应 `forward` 正/负，A/D 对应 `right` 负/正，Q/E 对应 `up` 负/正。 |
| `engine.fit()` | 将活动模型适配当前视口；须有有效尺寸。 |
| `engine.reset()` | 恢复当前模型初始视角。 |
| `engine.fixedMode()` / `engine.freeMode()` | 切换绕模型固定浏览与自由游览。 |
| `engine.flipAxes(x, y, z)` | 三个绝对布尔状态控制显示翻转；可组合，不修改模型。拖动方向保持一致。 |
| `engine.close()` | 发起异步销毁并使实例停止接收命令；只调用一次即可。不要在此之后复用实例。 |
| `engine.closeAndWait()` | 挂起直到原生渲染线程/GPU 资源释放；从非 UI 协程调用。驱动停滞时可能持续等待。 |

`EngineState` 的 `phase` 为 `Empty`、`Loading`、`Uploading`、`Ready`、
`Recovering`、`Failed`、`Stopping`、`Stopped`；`requestId` 是最新请求，
`activeRequestId` 是已显示模型的请求。`uploadTicket` 标识上传事务，
`surfaceGeneration` 标识当前 Surface，`sceneCount` 为活动点数；
`framesPresented` 是累计呈现帧数，`lastFrameMicros` 是最近一次 CPU
帧耗时（微秒），不是 GPU 计时。`error` 为原生错误或解码错误整数；
应用应保存其原值供诊断。`EngineException.code` 为同步命令/打开失败代码。

```kotlin
val engine = Native3dgsEngine(applicationContext)
val generation = engine.attach(surfaceView.holder.surface)
engine.resize(generation, surfaceView.width, surfaceView.height)
val request = engine.open(uri) // 在协程中；uri 来自 ACTION_OPEN_DOCUMENT
engine.state.collect { state ->
    if (state.phase == EnginePhase.Ready && state.activeRequestId == request) {
        // 当前请求已呈现
    }
}
// SurfaceHolder.surfaceDestroyed: engine.detach(generation)
// 宿主退出时，在后台协程调用 engine.closeAndWait()
```

## 原生 C ABI 逐项说明

包含 `<native3dgs/android_engine.h>`；API 版本为 2，最低 API 29。
所有配置/快照结构先零初始化，再设置 `struct_size = sizeof(struct)`。
`gs_android_result_t` 的 `OK` 为 0；`INVALID_ARGUMENT` 表示参数或代际
不合法，`CLOSED` 表示已停止，`IO_ERROR` 表示 FD 访问失败，
`UNSUPPORTED` 表示平台能力缺失，`OUT_OF_MEMORY` 表示创建资源失败。
`gs_android_engine_t*` 不透明；调用方停止所有并发命令后再销毁。

| C 调用语法 | 含义、返回值和注意事项 |
| --- | --- |
| `uint32_t gs_android_api_version(void)` | 返回编译库的 API 版本；与头文件 `GS_ANDROID_API_VERSION` 核对。 |
| `gs_android_create(&config, &engine)` | 创建实例；`config.api_version` 必须匹配。`max_input_bytes/max_scene_bytes` 设 0 表示库默认；非 seek FD 需要应用私有的 `temporary_directory`。成功后调用方拥有 `engine`。 |
| `gs_android_destroy(engine)` | 等待工作线程结束并释放资源；可传空指针。调用前停止其他线程发命令，切勿在 UI 线程等待可能停滞的驱动。 |
| `gs_android_open_fd(engine, fd, &request_id)` | 接收 PLY/SPZ 可读 FD，函数返回前复制 FD；调用方仍拥有原 FD。异步解码在引擎进程内进行，返回请求 ID 后轮询快照。 |
| `gs_android_open_shared_fd(engine, fd, &request_id)` | 接收隔离解码 Service 生成的版本化共享场景 FD；仍复制 FD。不要传未经协议校验的任意字节流。 |
| `gs_android_cancel(engine, request_id)` | 取消当前加载/上传请求；旧 ID 返回 `INVALID_ARGUMENT`，旧场景仍可保留。 |
| `gs_android_close_scene(engine)` | 清除当前及待加载场景，不销毁引擎。 |
| `gs_android_attach_surface(engine, window, generation)` | `window` 是 `ANativeWindow*`；成功后引擎自持引用。`generation` 必须非零且严格递增。 |
| `gs_android_detach_surface(engine, generation)` | 解绑当前代际；宿主可释放自己的 window 引用。旧/重复代际返回 `INVALID_ARGUMENT`。 |
| `gs_android_resize(engine, generation, revision, width, height)` | 当前代际的 `revision` 须递增，物理尺寸须在 1..16384；新 Surface 从新的 revision 开始。 |
| `gs_android_camera(engine, action, x, y, z, seconds)` | 相机命令；只有活动场景时有效。动作参数见下表，失败返回 `INVALID_ARGUMENT`。 |
| `gs_android_get_snapshot(engine, &snapshot)` | 将当前状态复制到调用方结构，按 `struct_size` 截断以兼容扩展；调用前设置 `struct_size`。 |

`gs_android_camera_action_t` 的 `ORBIT`、`PAN`、`LOOK` 使用 `x/y`
物理像素；`DOLLY` 用 `x` 步数；`FLY` 用 `x/y/z` 分别表示右/上/前，
`seconds` 是本次时间步长；`FIT`、`RESET`、`ORBIT_MODE`、`FLY_MODE`
不使用额外参数；`FLIP_AXES` 用 `x` 传整数掩码 `1=X, 2=Y, 4=Z`
（可相加，0 清除），其他参数传 0。固定模式先切 `ORBIT_MODE` 再拖动；
自由模式切 `FLY_MODE` 后使用 `LOOK`/`FLY`。

`gs_android_snapshot_t.phase` 对应 `GS_ANDROID_EMPTY`、`LOADING`、
`UPLOADING`、`READY`、`RECOVERING`、`FAILED`、`STOPPING`、`STOPPED`；
`request_id`、`active_request_id`、`upload_ticket`、
`surface_generation`、`scene_count` 与 Kotlin 同义。
`frames_presented` 为累计帧数，`last_frame_us` 为最近 CPU 帧耗时，
`error` 是原生 `Error` 数值：0 无错误，1 取消，2 解码，3 上传，
4 Surface，5 设备，6 已关闭。`reserved` 暂不使用，不要据此判断状态。

```c
#include <native3dgs/android_engine.h>

gs_android_config_t config = {0};
config.struct_size = sizeof(config);
config.api_version = GS_ANDROID_API_VERSION;
gs_android_engine_t *engine = NULL;
if (gs_android_create(&config, &engine) == GS_ANDROID_OK) {
    uint64_t request_id = 0;
    gs_android_result_t opened = gs_android_open_fd(engine, model_fd, &request_id);
    gs_android_snapshot_t snapshot = {0};
    snapshot.struct_size = sizeof(snapshot);
    if (opened == GS_ANDROID_OK)
        gs_android_get_snapshot(engine, &snapshot);
    gs_android_destroy(engine);
}
```

原生层不持有传入 FD 的所有权；所有 `open*` 成功返回仅表示请求进入队列。
没有 Surface 时可以解码，但不会达到已呈现的 `READY`。
Android SDK ZIP 内的 `examples/c-consumer` 与 `examples/kotlin-consumer`
分别验证 CMake 与 AAR 包外消费。
