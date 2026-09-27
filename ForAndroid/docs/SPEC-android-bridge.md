# Spec: android-bridge

## 目标与边界

把 `engine` 接入 Android 应用和第三方宿主。对外提供版本化原生 C ABI 与 Kotlin AAR API；桥接层负责 JNI 引用、文件描述符、`Surface`/`ANativeWindow`、Service 绑定和线程转发，不实现 PLY 解析、GPU 排序或 UI 控件。外部不能直接拿 `std::shared_ptr`、异常或平台内部 C++ 类跨 ABI 边界。

## 原生 C ABI

公共头定义 API 版本、带 `struct_size` 的配置/快照/错误结构、不透明 `gs_engine_t*`、稳定的整数错误码和显式销毁函数。创建/销毁、`open_fd`、取消/关闭、相机命令、attach/detach/resize、snapshot/poll_events、shutdown 是首期入口；事件缓存由调用者提供容量并返回实际数量，避免跨 DLL/NDK allocator 释放。新结构字段只尾部追加，旧大小按版本拒绝或按明确默认值处理；未知枚举返回参数错误。数值均注明字节、物理像素、秒和坐标单位。

`open_fd` 在入口复制 fd 所有权，调用者可立即关闭原 fd；无可读/无效 fd 同步返回错误。Surface attach 接收 Android 原生窗口指针并增加自己的引用，detach 完成事件后才释放；外部不可在引擎仍持有时销毁对象。所有回调/轮询结果为值类型，字符串由调用者缓冲区或限长复制 API 返回；JNI 异常不穿透 C++ 渲染线程。ABI 兼容性测试用旧版本头编译消费程序，并在新库上运行。

## Kotlin/Android 契约

AAR 暴露 `Native3dgsEngine` 等 Kotlin 封装，提供 suspend/异步 `open(Uri)`、`cancel`、`closeScene`、`attach(Surface)`、`detach`、`resize`、相机命令、不可变状态流和显式 `close()`。Kotlin 使用 `ContentResolver` 打开 `ParcelFileDescriptor`；跨 JNI 前后按上面的 fd 复制契约关闭本地引用。Binder Service 由 AAR Manifest 声明为 `exported=false`，进程名为宿主包私有 `:native3dgs_decoder`；宿主合并 Manifest 后无需存储广域权限，模型 URI 权限来自系统选择器。Service 断开只结束关联请求并释放资源，不自行结束 Activity。

主线程只接收限速的状态流/事件，JNI 轮询与 Service Binder 操作放在受控后台调度器；不能保存 Activity 强引用或把 `JNIEnv*` 跨线程。Activity/Fragment 重建时新 SurfaceGeneration 绑定当前 engine；宿主显式销毁 engine 后所有方法返回 Closed，而非使用悬空原生指针。Android UI 测试和第三方宿主分别验证生命周期。

## 实现与验收

先固定 C ABI 头和错误/所有权契约，再实现 JNI 边界及 Kotlin 状态映射；随后接入 Service Manifest 和 Surface/FD 生命周期，最后用独立示例验证 AAR。JNI 层开启 CheckJNI 测试无本地/全局引用泄漏；错误注入覆盖无权限 URI、关闭中的回调、双重 close、错误版本结构、Service 被杀和 Surface 快速替换。旧头/新库的 ABI 测试、Kotlin 单元测试与 Android 仪器测试均通过后才发布此模块。
