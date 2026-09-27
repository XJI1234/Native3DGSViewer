# Spec: engine

## 目标与依赖

在 `model-io` 与 `render-core` 之上提供线程安全的单模型异步事务、相机和 Surface 生命周期；不依赖 Activity、Kotlin、JNI 或具体文件 URI。公共命令快速返回，阻塞解码、Binder 等待和 GPU fence 仅在后台线程执行。依赖方向见[能力图](capability-map.md)。

## 状态和接口

核心命令为 `open(fd, source_options)`、`cancel(request_id)`、`close_scene()`、`camera_command`、`attach_surface(generation,window)`、`detach_surface`、`resize`、`request_shutdown`。`open` 返回递增 RequestId 或同步参数错误；渲染器上传返回 UploadTicket。只读快照包含 Empty/Loading/Uploading/Ready/Recovering/Failed/Stopping/Stopped 等阶段、当前/活动请求、活动 ticket、SurfaceGeneration、进度、相机、场景摘要、渲染统计和最近结构化错误；事件队列只传必须可靠消费的场景/Surface/设备状态，队列有界并公开丢弃计数。事件和快照不持有 JNI `jobject`。

新打开请求取消旧待命解码或上传，旧活动模型继续可见；仅在新场景首次成功呈现后同时提交场景、相机与 reset 基线。旧请求的 Binder 回复、解码进度、upload fence 或 Surface 回调按 ID/代际丢弃。失败、取消或 CPU/GPU 预算不足不卸载旧活动模型；用户显式关闭时等待 SceneCleared，释放后可以重试大模型。活动 CPU 场景保留到设备恢复重传完成或用户关闭，必须计入内存预算。

相机控制器复用平台无关的 double 数学。固定模式围绕模型中心 orbit，单指向左拖动屏幕上的模型就向左旋转；自由模式按相机 local right/up/forward 移动，移动速度由 `dt` 限幅。fit/reset、屏幕方向变化及 X/Y/Z 显示镜像的输入语义由相机契约测试锁定。模式切换不改变当前姿态，旋转屏幕不自动重置视角。

## 生命周期、故障与实现顺序

Surface generation 随新 `ANativeWindow` 增加，同 generation 中 resize revision 递增；窗口不可见或 0x0 不 Present，但命令、取消和关闭仍可推进。设备丢失先停止提交，通知宿主解绑旧 Surface，再有限重建设备和上传活动场景；旧 generation 的 attach/detach 不得影响新窗口。Activity 退出调用非阻塞 `request_shutdown`，后台有界等待终态；析构不得在 UI 主线程隐式等待无期限 Service 或 GPU。

先从 Windows engine 测试提取可复用的请求/相机状态机夹具，再对接 Android Service 与 Vulkan 空 Surface，随后实现上传激活、替换、恢复和关停。错误入口用稳定错误码与阶段；异常、Binder 断开和 Vulkan VkResult 统一转为结构化 EngineError，但保留原始平台码供日志。没有活动场景时依然能启动、显示空 Surface 并关闭。

## 测试与退出条件

确定性测试覆盖连续快速打开、取消、关闭、迟到回调、旧模型保留、0x0、旋转、后台恢复、设备丢失与永不完成的模拟 fence；验证每个请求恰好一个终态，事件丢弃不改变权威快照。重复 100 次打开/关闭/Surface 重绑无线程、FD、场景或 GPU 资源持续累积；UI 命令的同步耗时保持短且没有文件解码/长 GPU 等待，即完成首期引擎验收。
