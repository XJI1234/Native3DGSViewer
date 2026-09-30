# Android 设计文档索引

状态：引擎与 SDK 技术预览；下述技术参数是首期设计基线。真机阶段数据见[Adreno 750 验证记录](verification/device-performance-2026-09-30-adreno750.md)，尚未通过完整性能验收。

| 文档 | 内容 |
| --- | --- |
| [能力图](capability-map.md) | 稳定模块 ID、责任、公共契约位置和依赖顺序 |
| [总体开发计划](technical-development-plan.md) | 产品边界、阶段、工具链、质量/性能与发布门槛 |
| [系统技术设计](system-technical-design.md) | 文件/场景/帧数据流、进程线程、生命周期和错误恢复 |
| [splat-types](SPEC-splat-types.md) | 共享场景、坐标、颜色和相机数学契约 |
| [model-io](SPEC-model-io.md) | 文件描述符、隔离解码、共享内存和资源准入 |
| [render-core](SPEC-render-core.md) | Vulkan 设备、排序、上传、帧图、Surface 和统计 |
| [engine](SPEC-engine.md) | 异步事务、相机、状态快照及设备恢复 |
| [android-bridge](SPEC-android-bridge.md) | 原生 C ABI、JNI/Kotlin、文件和 Surface 所有权 |
| [android-viewer](SPEC-android-viewer.md) | 手机/平板查看器、触控和用户状态 |
| [sdk-package](SPEC-sdk-package.md) | AAR、NDK 包、示例、许可与独立消费测试 |
| [SDK 接入指南](SDK-guide.md) | Kotlin/C 接入、线程、FD/Surface 所有权与状态 |
| [实施计划](../tasks/plan.md) / [任务清单](../tasks/todo.md) | 可独立验证的交付切片 |
| [核心验证记录](verification/core-2026-09-27.md) | 实际构建、模拟器独立/联合/仪器测试与未完成门槛 |
| [引擎验证记录](verification/engine-2026-09-27.md) | Gaussian 图像、请求事务、SDK 包外消费及未完成门槛 |
| [Adreno 750 验证记录](verification/device-performance-2026-09-30-adreno750.md) | 三轮基线与复测、大模型缓冲分段、热状态和剩余门槛 |
| [开发记忆](development-memory.md) | FD 所有权、Surface 恢复、预算及复测经验 |

## 已确定

- Android 10+（API 29）、`arm64-v8a`、Vulkan 1.1；Kotlin 原生宿主与 C++20 引擎，首期只做单模型查看。
- 七模块能力图已经确认。共享逻辑提取到平台无关层；Android 特有实现放在 `ForAndroid/`。
- Kotlin AAR 与原生 C 接口均为 SDK 交付面；解码使用独立 Service 进程。
- 首期支持标准 PLY/SPZ、SH 0-3、手机和平板，目标是在代表性近代旗舰真机上对约 100 万至 300 万点场景持续浏览达到 30 FPS。

## 待验证假设

- Vulkan 1.1 设备上的 subgroup、内存堆、呈现和驱动行为可能不同；排序以无固定 subgroup 宽度路径起步，设备自检和跨厂商真机测试决定是否加入变体。
- 代表性性能设备为至少 8 GB RAM、Adreno 7xx 或同级 Mali/Immortalis 的真机；Adreno 750 上已取得分阶段真机数据，跨厂商与持续热态性能仍待验证。
- Android 厂商的 GPU 预算扩展可能缺失；预检不能代替实际 Vulkan 分配结果。大型 SPZ 完整 cloud 的峰值需要测量。

## 实施证据

已取得模拟器上的隔离解码、GPU 稳定排序自检、Gaussian 首帧像素、请求事务和 Windows 回归证据；包外 C/NDK 与 Kotlin 编译消费通过，见[引擎验证记录](verification/engine-2026-09-27.md)。Adreno 750 的真机帧时间、热状态和大模型加载见[验证记录](verification/device-performance-2026-09-30-adreno750.md)；SH 1–3 固定图像、GPU validation、30 分钟热态及跨厂商测试仍待完成。
