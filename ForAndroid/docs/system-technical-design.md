# Android 原生 3DGS 引擎系统技术设计

状态：系统目标设计，部分核心路径已验证；当前实施边界与证据见[核心验证记录](verification/core-2026-09-27.md)。本文把[能力图](capability-map.md)中的七个模块连接成一条可运行路径；各接口由相应提供者规格定义。Android 版沿用 Windows 的右手 RUB 场景、只读场景句柄、请求/上传票据、Surface 代际与事务激活语义，但不复用 Win32 Job、DXGI swapchain 或 D3D12 公共头。

## 1. 进程、线程与依赖

```text
用户文档 URI -> android-viewer -> android-bridge -> engine
                    |                    |          |
             ParcelFileDescriptor   SurfaceView   +-> model-io Service 进程
                    |                    |                  |  共享内存/状态
                    +--------------------+------------------+
                                                 |
                                          render-core Vulkan
                                                 |
                                           ANativeWindow/Surface
```

主应用进程包含 UI 主线程、一个引擎协调线程和一个独占 Vulkan device/queue 的渲染线程；model-io 独立 Service 进程只处理受控文件描述符、解码和只读共享场景。JNI 不在 Binder 线程或渲染线程直接调用 View。`android-bridge` 将 Kotlin 的 `Uri` 和 `Surface` 转为持有明确所有权的文件描述符和 `ANativeWindow`；`engine` 不知道 Activity、ContentResolver 或 Java 对象。渲染器不依赖 `model-io`，只接受 `splat-types` 场景。

## 2. 启动、加载和替换事务

启动：宿主创建界面和空 `SurfaceView`；桥接层创建 engine；渲染线程枚举 Vulkan 1.1 物理设备、队列、Surface 能力和必需格式，运行稳定排序的 GPU 读回自检；成功后才开放模型加载。模拟器仅用于阶段/错误路径测试，不能因存在 Vulkan HAL 文件就跳过 `vkCreateInstance`、设备与 Surface 探针。无合适设备保留可交互的错误界面，不进入黑屏重试。

打开：`ACTION_OPEN_DOCUMENT` 得到 `content://` URI，宿主用 `ContentResolver.openFileDescriptor` 取得只读 FD；桥接层在非阻塞入口立即 `dup`，调用者可关闭原 FD。引擎分配递增 RequestId，把 fd 和格式预检请求发送到 Service。解码器不依据扩展名猜格式；可 seek 的 FD 直接分块读取，不能 seek 或无法得到稳定大小的提供者先复制到有上限、可取消的应用私有临时文件并记录复制进度。禁止在 UI 线程读取完整模型或把 URI 当本地绝对路径。

Service 预检文件头、点数、stride、SH 阶数和共享场景字节，使用可用内存与进程峰值估算决定是否启动；分块 PLY 或受控 SPZ 解码写入共享内存，完成校验后将只读句柄和版本化布局元数据经 Binder 返回。Binder 只传状态和文件描述符，不传逐点大数组。宿主重新校验 header、长度、数组边界、源坐标和版本；只在完整成功后发布不可变 SceneHandle。Service 崩溃、被系统杀死、超时、取消或 Binder 断开均变成稳定错误并清理 FD/共享映射，不使活动模型失效。

渲染线程在旧活动场景仍可绘制时，对新场景核算 CPU/GPU 增量峰值，按页上传、等待相应 timeline/binary fence，完成首次排序与首次成功呈现后在帧边界同时提交新场景和初始相机，发送 `SceneReady(request,ticket,generation)`。失败/取消/过期请求释放待命资源并保持旧场景、相机和可见模型名。关闭模型需明确等待 `SceneCleared`，不能把取消新请求等同于关闭当前模型。

## 3. 帧图、颜色和预算

相机 double 世界位置与模型 double 原点先相减，再送 float32 相机相对数据到 Vulkan。世界坐标约定右手、+Y 向上、相机局部 -Z 向前；投影映射 Vulkan 的 `[0,1]` 深度并按实际 Surface 方向处理 Y 映射。PLY/SPZ 的 DC/SH、opacity 和四元数标准化与 Windows 共享；默认保留同一排序键与远到近稳定顺序。帧图为候选/投影 -> 稳定 GPU radix 排序 -> 读取排序索引绘制 Gaussian quad -> SH/alpha 混合 -> Present。普通帧不读回排序数组，诊断读回和截图只在受控测试开启。

Vulkan 缓冲布局按设备 `maxStorageBufferRange`、地址能力、描述符限制和实测内存预算分段，所有大小与偏移使用受检 64 位算术；不得假设 Windows 的 4 GiB shader 地址边界可直接照搬。优先查询 `VK_EXT_memory_budget`，缺失时记录设备 heap 大小与扩展不可用，并用保守余量预检；分配失败始终按真实 VkResult 处理。旧活动模型、待命模型、共享映射、上传页、排序 scratch、离屏目标和重建期 swapchain 都计入峰值。资源释放延迟到最后使用它的 fence 完成；设备丢失路径不等待永不可能完成的旧 fence。

GPU 时间戳使用 Vulkan timestamp 支持与有效位能力，在已完成帧的 fence 后异步读取；不可用时字段保持空值。统计按固定低频采样记录 CPU 提交、GPU 投影/排序/绘制、呈现间隔、候选/绘制点数、内存预算/用量、驱动、shader 哈希和热状态，不在每帧同步写磁盘。默认不存完整 URI、模型内容或逐点数据。

## 4. Surface 和设备生命周期

`SurfaceHolder` 创建/改变时桥接层取得新的 `ANativeWindow` 引用并提交当前 SurfaceGeneration/ViewportRevision；渲染线程独占 `VkSurfaceKHR`、swapchain、命令缓冲和帧资源。尺寸为零、Activity 不可见或 Surface 已销毁时停止 acquire/present，保留 CPU 场景；宽高、旋转或颜色格式变化只重建尺寸相关资源。旧 generation 的回调和命令不得重新激活旧 Surface。`ANativeWindow_release` 必须在相关渲染引用与 fence 安全后执行，不能由 UI 线程抢先释放。

`VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` 触发受控 swapchain 重建；`VK_ERROR_SURFACE_LOST_KHR` 等待宿主提供新 Surface；`VK_ERROR_DEVICE_LOST` 停止提交、记录阶段与驱动信息、废弃旧 GPU generation，并在有界次数内重建设备/资源和重传仍有效的 CPU 场景。恢复失败进入稳定故障状态，允许关闭/重新初始化，不无限循环。Activity 销毁时先停止输入和新请求，取消解码、等待/放弃有界 GPU 工作、解绑 Surface、关闭 Service 连接，最后释放映射及引擎；UI 主线程不得同步等长时间 GPU fence。

## 5. 失败矩阵与可观测性

| 故障 | 处理结果 | 验证 |
| --- | --- | --- |
| URI/FD 失效、格式不支持、输入损坏 | 结构化加载失败，旧模型保持 | 断开的 provider、截断和非法属性语料 |
| Service 超时/崩溃/系统回收 | 当前请求失败并回收共享内存；可重新打开 | 故障注入与重复打开 |
| CPU/GPU 预算变化或分配失败 | 报需求/余量/真实分配阶段，不静默降质 | 内存压力及注入失败 |
| Surface 消失、旋转、后台/前台 | 停止呈现并按 generation 重绑 | 快速旋转及后台 100 次 |
| Vulkan 设备丢失、GPU 自检失败 | 有界恢复或持久错误，绝不使用未经验证的排序结果 | 注入与不支持设备 |
| 迟到 Binder/回调、关闭中事件 | 按 RequestId/ticket/generation 丢弃 | 替换与关闭竞态测试 |

运行日志保存在应用私有目录，按启动时间分文件并轮转；公开导出由用户操作触发。错误码用于程序控制，诊断字符串只供限长日志。Android 模拟器 API 35、`x86_64`、约 9 GB RAM 已连接；其 PackageManager 未宣告 Vulkan feature，虽有厂商 Vulkan HAL 文件，实际设备/Surface 能力仍待运行探针。它可用于 UI、Service、文件和生命周期测试；性能与热节流验收必须使用目标 `arm64-v8a` 真机。
