# Android 原生 3DGS 实施计划

状态：A1/A2 核心原型实施中；已验证切片及未完成门槛见[核心验证记录](../docs/verification/core-2026-09-27.md)。总目标与验收见[总体开发计划](../docs/technical-development-plan.md)，七模块依赖见[能力图](../docs/capability-map.md)，跨模块时序见[系统技术设计](../docs/system-technical-design.md)。本文件规定实现顺序和停点；逐项验收与命令见[任务清单](todo.md)。

## 架构决定

- 正式目标 API 29+、`arm64-v8a`、Vulkan 1.1；`x86_64` 仅用于当前 API 35 模拟器的 Debug 探针、UI 和故障路径。模拟器列出了 Vulkan HAL 文件却没有 PackageManager Vulkan feature，不能预设可绘制；A1 必须实际枚举 instance/device/Surface。
- Windows 与 Android 共用场景契约、相机数学和 PLY/SPZ 规范化源码。Windows 独有的 helper/Job/D3D12 接口留在旧工程；Android 特有的 Service/FD/Vulkan/JNI 留在 `ForAndroid/`。任何提取先固定现有 Windows 回归，再改源码，持续运行两端契约测试。
- 文件输入来自 `ACTION_OPEN_DOCUMENT` 的 fd；隔离 Service 进程解码，以受检共享内存交付不可变场景。Kotlin/UI 只持有 URI 和摘要；Binder 不搬运大模型数组。
- Vulkan 使用构建期 SPIR-V、无固定 subgroup 宽度的稳定 GPU sort 和启动自检。预算来自实时能力与分配结果；不以机身 RAM 或点数写死准入，不在生产帧读回排序数据。
- 对外 Kotlin AAR 和版本化原生 C ABI；C++ 类仅内部使用。初始包版本 `0.1.0`，正式包仅 `arm64-v8a`，文档/许可证/示例与二进制同版。

## 任务依赖与可运行切片

| 切片 | 任务 | 可运行成果与检查点 |
| --- | --- | --- |
| A0 规格 | D01-D02 | 文档索引、七规格、任务/命令/链接审核 |
| A1 共享与可行性 | T01-T05 | Windows 回归不变；Android 主机解析测试；模拟器 Vulkan/Surface 能力真实探针 |
| A2 正确渲染 | T06-T10 | 隔离 PLY/SPZ 场景经 Vulkan 稳定排序与 SH 绘制，固定画面通过 |
| A3 事务与应用 | T11-T16 | 取消、替换、恢复、C/JNI/Kotlin、手机和平板浏览端到端可用 |
| A4 性能兼容 | T17-T19 | 真机排序/帧/内存/热证据、等画质旧查看器对照、故障长测 |
| A5 发布 | T20-T21 | 独立消费 AAR/NDK 包、完整许可文档和安装验证 |

`T03` 的 Vulkan 探针与 `T04` 的 PLY/SPZ 主机解码只依赖 `T01-T02`，可分别推进；`T06` 的隔离 Service 依赖解码通过，端到端渲染还须等 `T05` 自检和 `T08` 上传通过。模型 IO、Vulkan 与 SDK 三处公共边界的改动，先改提供者规格和契约测试，再实现。每个任务按单次会话、通常不超过五个源文件设计；大任务拆成清单中的子任务，不把失败测试删掉以推进阶段。

## 阶段停点

**A0：** 文档覆盖目标、命令、目录、风格、测试和边界；七份规格对应能力图且交叉链接可读。文档阶段不产出运行时代码。

**A1：** 主机侧 PLY/SPZ 语料和 Windows 全套测试通过；Android Debug `x86_64` 在连接的模拟器报告真实 Vulkan 能力并能展示三角形或明确 UnsupportedDevice。若模拟器不支持 Vulkan，GPU 里程碑转到 `arm64-v8a` 真机执行，不改正式设备基线。

**A2：** GPU 排序夹具与 CPU 参考一致，SH 0-3、透明、近面、Y 方向和颜色夹具通过；GPU validation 无错误。活动场景仅首次成功 Present 后发布。

**A3：** 用户可在手机和平板打开/取消/关闭 PLY/SPZ，固定与自由模式手势正确；旋转、后台、Surface 销毁、Service 死亡和设备丢失后有确定状态；重复 100 次不累积资源。

**A4：** 真机按固定模型哈希、相机路径、画质和物理像素比较旧 `Viewer_android`，30 秒预热、60 秒采样、三轮；约 100 万至 300 万点场景中位帧时间 <=33.3 ms 且 1% low FPS 不差于旧版。热节流和不具可比性的结果单独标识。门槛未达不能写“性能验收通过”；先定位瓶颈再修订路线。

**A5：** 仓库外独立 Kotlin 与 C/NDK 消费工程构建并运行正式工件；包内有完整开发文档、示例、依赖/许可、shader 哈希和 ABI 版本，无开发机绝对路径或样本模型。真机安装和生命周期验证完成后才能发布正式包。

## 工具、命令与记录

脚手架任务锁定 JDK 21、Gradle 8.11.1、AGP 8.7.2、Kotlin 2.0.21、NDK 27.2.12479018、CMake 3.22.1、compile/target SDK 35、min SDK 29。预期入口从 `ForAndroid/` 运行：`& .\gradlew.bat :sdk:assembleRelease :GUI:assembleDebug`、`& .\gradlew.bat :sdk:testDebugUnitTest`、`& .\gradlew.bat :GUI:connectedDebugAndroidTest`；脚手架创建后必须验证实际任务名并锁定 wrapper。NDK 主机/设备测试的完整 CMake 命令由 T01/T02 写入文档，不用未验证的命令标记通过。

每个阶段保存构建版本、设备/驱动、样本 SHA-256、shader 哈希、测试摘要及原始 benchmark 数据到 `docs/verification/`。当前模拟器实际创建 Vulkan 1.1 device、运行 GPU 排序及 Surface 三角形；不据此推断完整 Gaussian 图像或真机性能。真机未连接，A2 图像、A4 和 A5 的真机门槛保持待验收。
