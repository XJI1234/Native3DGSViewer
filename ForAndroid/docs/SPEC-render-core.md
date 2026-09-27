# Spec: render-core

## 目标与边界

把只读 `splat-types` 场景绘制到 Android `ANativeWindow` 对应的 Vulkan Surface。拥有 Vulkan 1.1 instance/device/queue、swapchain、上传、投影、GPU 稳定排序、Gaussian 合成、GPU 统计和设备恢复；不解析文件、不引用 JNI/Kotlin、也不决定 UI 文案。最小能力以实际 `vkEnumeratePhysicalDevices`、queue family、格式、storage buffer/atomic、Surface 呈现和 shader 功能探测为准，不能由 Android 版本或厂商名称推断。

## 接口、Surface 和资源

内部接口提供 `create_renderer`、`attach_surface(generation, ANativeWindow*)`、`detach_surface`、`resize(generation, revision, physical_size)`、`upload_scene(scene,camera)`、`cancel_upload(ticket)`、`clear_scene`、`set_camera(ticket,camera)`、`render_frame`、`get_stats` 和结构化事件。所有异步结果包含 ticket/generation；旧请求与旧 Surface 不得激活。桥接层将 `ANativeWindow_fromSurface` 得到的引用转交，renderer 在安全卸载后释放。0x0、不可见或后台状态暂停 acquire/present，不丢 CPU 场景。

渲染线程独占 VkDevice/queue/command pool；每帧槽直到对应 fence 完成才复用。上传页与 scene storage 保留至最后一次 GPU 读取完成。首期以预编译 SPIR-V 创建 pipeline；pipeline cache 可按 device/driver/版本持久化，验证 cache 头和失败回退，不能把一个 GPU 的缓存用于另一个驱动。对 `VK_ERROR_OUT_OF_DATE_KHR`/`SUBOPTIMAL`、Surface loss 和 device loss 分别处理；设备丢失不等待失效 fence，最多有限重建次数，重建后重传仍保留的活动 CPU 场景。

## 排序和画质

GPU 排序从不依赖固定 subgroup 宽度的稳定 radix 路径开始；启动时对 0/1、同键、组边界和随机键值做 GPU 读回自检，与 CPU 稳定排序逐项对照。自检失败不进入 Ready。厂商特化仅在同样自检与跨驱动测试通过后增加，不把 Windows HLSL wave32 假设直接移植。生产帧排序和绘制不逐帧读回 CPU。投影/协方差、SH 0-3、深度键、远到近透明合成、颜色 clamp 和 Gaussian 截断与 Windows 等画质夹具对照；Vulkan Y 方向和目标色彩空间以固定色条及相机截图验证，不能凭默认坐标猜测。

每个缓冲按 `maxStorageBufferRange` 和 descriptor 能力分段；64 位受检布局含基础属性、SH、索引、键、radix scratch、上传页、帧目标和重建峰值。动态 GPU 预算优先取 `VK_EXT_memory_budget`；缺失时使用 heap 容量、已有分配跟踪和保守余量，任何准入均非分配成功保证。预算不足或实际分配失败时回收待命资源并保留旧场景；不能静默截断点数、降 SH 或分辨率。

## 统计、测试与退出条件

`RenderStats` 含设备/驱动/功能、选择的排序路径及自检结果、帧号、CPU 提交时间、可用时的 GPU 投影/排序/绘制时间、实际 Present 间隔、点数及 budget/usage；未测值保持空。低频采样不在 UI 线程等 GPU，也不写逐帧日志。先完成 Surface 三角形、排序夹具，再接场景上传/合成和恢复。主机测试覆盖数学/预算溢出；真机 GPU 测试覆盖 SH、透明、近面、排序稳定性、resize/旋转、OOM、Service/Surface 独立故障及设备重建。固定截图与 CPU 参考一致、验证层无错误，持续运行不累积 GPU 资源，才算正确性验收；30 FPS 是后续整机门槛，不由单一 GPU pass 时间替代。
