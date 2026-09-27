# Android 原生引擎能力图

模块 ID 在实施期保持稳定。公共契约定义在提供者规格中，消费者不得复制一份略有差异的约定。

| 模块 ID | 唯一责任 | 提供的边界 | 依赖 |
| --- | --- | --- | --- |
| `splat-types` | 跨平台不可变场景、坐标/颜色和相机数学 | 场景布局、只读句柄、受检尺寸计算 | C++20 标准库 |
| `model-io` | PLY/SPZ 识别、隔离解码、校验、进度与取消 | 文件描述符输入、结构化结果、共享场景 | `splat-types` |
| `render-core` | Vulkan 设备、GPU 上传、排序、绘制及统计 | 场景票据、Surface 代际、渲染事件 | `splat-types` |
| `engine` | 请求事务、相机、工作线程及恢复协调 | 非阻塞命令、状态快照、可靠事件 | `splat-types`、`model-io`、`render-core` |
| `android-bridge` | 版本化 C ABI、JNI/Kotlin 桥接和 Android 对象所有权 | 不透明句柄、Kotlin API、Service IPC 适配 | `engine` |
| `android-viewer` | 文件选择、触控、界面与宿主生命周期 | 可安装的原生查看器 | `android-bridge` |
| `sdk-package` | AAR/NDK 包、示例、许可证和消费验证 | 可重定位发布工件 | `android-bridge`、`engine`、`render-core`、`model-io` |

依赖图：`splat-types -> {model-io, render-core} -> engine -> android-bridge -> {android-viewer, sdk-package}`。`model-io` 不引用 Vulkan，`render-core` 不引用文件解码器、JNI 或 Android UI；`android-viewer` 不解析逐点数据。解码 Service 的 IPC 协议归 `model-io`，其 Android 生命周期和 AAR Manifest 集成归 `android-bridge`。

实施先后：共享契约与 Windows 回归基线；Vulkan Surface/排序最小原型和文件描述符解码原型；单模型端到端渲染；事务/恢复与 Kotlin 桥接；手机/平板 UI；SDK 打包及跨设备验收。Vulkan 与解码两个原型在共享契约稳定后可独立推进；端到端集成依赖两者都通过。

首期不增加 `editor`、`exporter`、`multi-scene` 或 `lod` 模块。未来新增能力须更新本图、提供者规格和下游契约测试，不能把未实现能力隐含在首期 API 中。
