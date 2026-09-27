# Android SDK 接入指南

当前版本 `0.2.0` 技术预览，最低 Android 10/API 29，发布 ABI 为 `arm64-v8a`，GPU 需支持 Vulkan 1.1。
模拟器图像与集成测试已通过；真实 arm64 GPU 的画质、性能、低内存和热稳定性仍待验收。

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
API 版本为 1。`gs_android_open_fd` 接收 PLY/SPZ 文件描述符，函数返回前复制 FD，
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

当前仍需真实 arm64 设备验证 Vulkan 画质、设备丢失、内存压力、30 FPS 与长时间运行。
大于单个 `maxStorageBufferRange` 的场景尚未分段上传，会返回资源失败。当前帧提交
使用单帧 fence 同步，尚未建立多帧流水和 GPU 时间戳统计；这些是性能验收前的门槛。
