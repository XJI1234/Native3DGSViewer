# Spec: render-core

## 目标与边界

把只读 `splat-types` 场景绘制到 Android `ANativeWindow` 对应的 Vulkan Surface。拥有 Vulkan 1.1 instance/device/queue、swapchain、上传、投影、GPU 稳定排序、Gaussian 合成、GPU 统计和设备恢复；不解析文件、不引用 JNI/Kotlin、也不决定 UI 文案。最小能力以实际 `vkEnumeratePhysicalDevices`、queue family、格式、storage buffer/atomic、Surface 呈现和 shader 功能探测为准，不能由 Android 版本或厂商名称推断。

## 接口、Surface 和资源

内部接口提供 `create_renderer`、`attach_surface(generation, ANativeWindow*)`、`detach_surface`、`resize(generation, revision, physical_size)`、`upload_scene(scene,camera)`、`cancel_upload(ticket)`、`clear_scene`、`set_camera(ticket,camera)`、`render_frame`、`get_stats` 和结构化事件。所有异步结果包含 ticket/generation；旧请求与旧 Surface 不得激活。桥接层将 `ANativeWindow_fromSurface` 得到的引用转交，renderer 在安全卸载后释放。0x0、不可见或后台状态暂停 acquire/present，不丢 CPU 场景。

渲染线程独占 VkDevice/queue/command pool；每帧槽直到对应 fence 完成才复用。上传页与 scene storage 保留至最后一次 GPU 读取完成。首期以预编译 SPIR-V 创建 pipeline；pipeline cache 可按 device/driver/版本持久化，验证 cache 头和失败回退，不能把一个 GPU 的缓存用于另一个驱动。对 `VK_ERROR_OUT_OF_DATE_KHR`/`SUBOPTIMAL`、Surface loss 和 device loss 分别处理；设备丢失不等待失效 fence，最多有限重建次数，重建后重传仍保留的活动 CPU 场景。

静态单模型按变化重绘：首帧、场景替换、相机修订、画质档或渲染比例变更和 Surface 重建均触发呈现；相机静止时保留 Surface 的最后画面并暂停重复投影、排序与绘制。渲染中的新相机命令必须在下一轮被检测，不丢失唤醒；交互帧性能采样须持续移动相机，不以空闲时不绘制的统计冒充帧率提升。全质量档使用全部输入点及物理像素；移动档的 LoD 和渲染比例独立可控，候选合并必须通过屏幕空间误差及图像验收，未就绪时使用原始点。

Android 宿主可用 `SurfaceHolder.setFixedSize` 将 Vulkan Surface 缓冲设为视图物理尺寸的 0.75-1.0 倍；SurfaceFlinger 将该缓冲放大到视图尺寸，叠加的 Android UI 不降分辨率。`setSizeFromLayout` 恢复全质量物理尺寸。render-core 仍按 `ANativeWindow` 实际缓冲尺寸投影与绘制，不自行声称 Surface 缓冲等于最终显示尺寸。切换尺寸须按现有 Surface generation/resize 语义安全重建；若宿主未提供可变尺寸 Surface，则移动档保持 1.0。

## 排序和画质

GPU 排序从不依赖固定 subgroup 宽度的稳定 radix 路径开始；启动时对 0/1、同键、组边界和随机键值做 GPU 读回自检，与 CPU 稳定排序逐项对照。自检失败不进入 Ready。厂商特化仅在同样自检与跨驱动测试通过后增加，不把 Windows HLSL wave32 假设直接移植。生产帧排序和绘制不逐帧读回 CPU。投影/协方差、SH 0-3、深度键、远到近透明合成、颜色 clamp 和 Gaussian 截断与 Windows 等画质夹具对照；Vulkan Y 方向和目标色彩空间以固定色条及相机截图验证，不能凭默认坐标猜测。

正式构建在已验证的 Adreno 735 上可选择 subgroup ballot 稳定 scatter：先探测 compute 阶段的 basic/ballot 能力及 subgroup 宽度，再对候选 shader 做 GPU/CPU 逐项自检；任一条件不满足或管线创建失败即使用原始稳定 scatter，并记录最终路径。其他 GPU 保持原路径，待各自驱动上的正确性和整帧收益验证后再扩展。移动档降质与排序路径独立；排序路径不得改变全质量档输出。

每个缓冲按 `maxStorageBufferRange` 和 descriptor 能力分段；SH 系数按完整点的系数步长切段，投影 shader 由点索引选择段并读取完整 SH 0–3，未使用的描述符绑定有效哑缓冲。分段数量超过设备每阶段 storage descriptor 限额时明确拒绝，不能截断 SH。64 位受检布局含基础属性、SH、索引、键、radix scratch、上传页、帧目标和重建峰值。动态 GPU 预算优先取 `VK_EXT_memory_budget`；缺失时使用 heap 容量、已有分配跟踪和保守余量，任何准入均非分配成功保证。预算不足或实际分配失败时回收待命资源并保留旧场景；不能静默截断点数、降 SH 或分辨率。

## 统计、测试与退出条件

基准采样通过版本化、限容量的内存队列导出每帧 CPU 调用时长、`vkQueuePresentKHR` 返回单调时刻及上一已完成帧的 GPU 阶段耗时；GPU 时间不可用时保留无效标记。`vkQueuePresentKHR` 返回不是实际屏幕呈现时间，整机帧时间须与 Perfetto/SurfaceFlinger 记录核对。队列满时累计丢样数，不在渲染线程逐帧写文件。

`RenderStats` 含设备/驱动/功能、选择的排序路径及自检结果、帧号、CPU 提交时间、可用时的 GPU 投影/排序/绘制时间、实际 Present 间隔、原始/活动点数、渲染比例及 budget/usage；未测值保持空。Vulkan 时间戳在对应 frame fence 完成后读取，普通帧不得调用 queue/device idle 等待呈现完成。低频采样不在 UI 线程等 GPU，也不写逐帧日志。先完成 Surface 三角形、排序夹具，再接场景上传/合成和恢复。主机测试覆盖数学/预算溢出；真机 GPU 测试覆盖 SH、透明、近面、排序稳定性、resize/旋转、OOM、Service/Surface 独立故障及设备重建。固定截图与 CPU 参考一致、验证层无错误，持续运行不累积 GPU 资源，才算正确性验收；30 FPS 是后续整机门槛，不由单一 GPU pass 时间替代。

在 LoD 层级通过 SSIM 和局部错误验收前，活动点数等于原始点数。渲染比例的请求值、由实际缓冲尺寸计算的值和原始/活动点数分别记录，避免把请求值当作已经生效的画质变化。
