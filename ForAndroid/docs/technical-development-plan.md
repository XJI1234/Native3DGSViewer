# Android 原生 3DGS 引擎总体开发计划

状态：目标规格，核心原型实施中。首期由 C++20/Vulkan 1.1 引擎、Kotlin 原生查看器、Kotlin AAR 和原生 C SDK 组成；当前实现证据见[核心验证记录](verification/core-2026-09-27.md)，不改动本文退出门槛。模块边界见[能力图](capability-map.md)，跨模块细节见[系统技术设计](system-technical-design.md)。

## 1. 产品边界与验收

用户在 Android 手机或平板上通过系统文档选择器打开一个本地标准 Graphdeco 二进制小端 PLY 或 Niantic SPZ v1-v4，查看真实进度，可取消、关闭、重新打开；浏览提供固定轨道模式和自由模式、适配与重置视角。SDK 用户可把同一引擎嵌入自己的 Android 应用。首期保留 SH 0-3、原始点数和稳定透明排序，不以静默降质或抽点冒充性能提升。压缩 PLY、远程 URL、多模型、编辑、导出、LoD、AR/VR 留待单独规格。

正式支持基线为 API 29+、`arm64-v8a`、支持所需格式/队列/Surface 的 Vulkan 1.1 硬件。缺少能力时返回明确的设备错误；不调用 WebView、软件 Vulkan 或未经验证的 OpenGL 后备渲染器。支持手机竖屏与平板横屏；旋转屏幕、后台/前台和 Surface 销毁重建均须保持确定状态。

图像正确性先于性能：相同场景、相机、视口、SH/排序/颜色参数与 Windows 实现及 `../Viewer_android` 建立截图夹具，检查透明叠加、近面、边缘、远原点和 SH 0-3。性能在至少 8 GB RAM、Adreno 7xx 或同级 Mali/Immortalis 真机上，以 100 万至 300 万点场景、相同物理像素和等画质参数连续浏览：30 秒预热、60 秒采样、至少三轮，中位呈现帧时间不高于 33.3 ms，1% low FPS 不低于同机旧查看器基线。采样记录 GPU/CPU 阶段时间、分辨率、驱动、内存、温度与热节流状态；受刷新率、温度或旧应用画质差异干扰时标为不可比较，不宣称达标。

## 2. 技术栈与工程约定

以现有工作站已安装版本固定初始方案：JDK 21、Gradle 8.11.1、Android Gradle Plugin 8.7.2、NDK 27.2.12479018、SDK 35、CMake 3.22.1、Kotlin 2.0.21、C++20；`minSdk=29`、`targetSdk=35`、仅 `arm64-v8a`。Kotlin 使用 Android Views/Material 组件和 `SurfaceView`，避免渲染 Surface 的生命周期隐于 WebView。NDK `glslc` 在构建期把 GLSL 编译为目标 Vulkan 1.1 的 SPIR-V，记录源码及二进制哈希；运行期仅创建/缓存 Vulkan pipeline，不从任意模型输入编译 shader。依赖版本、补丁和许可进入 `third_party` 清单。

当前从仓库根目录运行 `& .\ForAndroid\gradlew.bat -p ForAndroid :sdk:assembleDebug :sdk:assembleRelease` 和 `& .\ForAndroid\gradlew.bat -p ForAndroid :sdk:connectedDebugAndroidTest`；NDK CMake、模拟器 GoogleTest 命令见[验证记录](verification/core-2026-09-27.md)。`GUI`、公开 SDK 和独立消费任务尚未创建，相关构建命令在对应任务完成后补充。

代码风格：C++ 四空格、类型 PascalCase、函数/变量 `snake_case`，接口层错误使用稳定枚举与限长诊断；Kotlin 遵循官方命名和空安全约定。公共契约先修订提供者规格，再修改实现和消费者测试。Android 平台代码放在 `ForAndroid/`；共享契约、相机数学和格式规范化代码提取为仓库级平台无关层，Windows 现有构建持续通过。

## 3. 实施阶段

| 阶段 | 可运行/可检查成果 | 退出门槛 |
| --- | --- | --- |
| A0 规格和基线 | 七份模块规格、任务清单、样本哈希及旧 Android 查看器画质/性能采样方案 | 文档链接、契约、依赖图和可执行任务审核完成 |
| A1 移植基础 | 共享场景/相机/解析单元测试；Vulkan 三角形 Surface 原型；文件描述符解码原型 | Windows 回归通过，真机能显示/resize；主机解码语料通过且 fd 接入路径明确 |
| A2 正确显示 | 单模型 SH 0-3、上传、稳定排序、Gaussian 合成与固定截图 | GPU 自检、CPU 参考排序和图像夹具通过，无逐帧 CPU 排序读回 |
| A3 可用查看器 | 请求替换/取消、Service 失败、触控固定/自由模式、手机/平板布局 | 旧场景回滚、后台恢复、关闭/重开和低内存状态均可复现 |
| A4 性能与兼容 | 分阶段计时、预算诊断、同机旧查看器比较、跨 Adreno/Mali 真机 | 达成上述 30 FPS 门槛或记录未达标瓶颈并修订后续技术路线 |
| A5 SDK 交付 | AAR、原生包、示例、完整文档和许可证 | 解包后独立工程可构建、加载场景、渲染和安全关停 |

每两到三项任务设置可运行检查点，单任务原则上不改超过五个文件。排序效率、跨进程内存峰值、Android Surface/进程生命周期和热节流是最早验证的风险。若 A4 未达标，先保存完整证据；任何 LoD、动态分辨率或质量变更先修订规格与对照方法，不暗改验收条件。

## 4. 资源、诊断与发布门槛

CPU 准入使用场景布局、可用内存与进程实际分配；GPU 准入优先使用设备可报告的动态预算，扩展缺失时采用堆信息加保守余量，最终仍以 Vulkan 创建和分配结果为准。旧模型与待命模型可能共存，增量估算必须包括共享场景、上传页、排序缓冲、渲染目标和 Surface 重建峰值。拒绝时记录需求与可用量，不依赖“手机内存至少 8 GB”这一标签直接放行。

运行诊断记录设备/API/驱动能力、shader 哈希、启动自检、模型阶段、结构化错误、内存预算、低频帧统计、Surface 与设备恢复；默认不记录模型内容或完整 URI，日志限速并在应用私有目录轮转。发布前需要干净安装、权限/无权限、低存储、旋转/后台、取消/替换、Service 崩溃、设备丢失与 30 分钟持续浏览测试。无连接真机时这些门槛保持“待验收”，不能从 Windows 结果推断通过。
