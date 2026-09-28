# Android 原生 3DGS 任务清单

依赖按任务 ID 标记；每项只在所列验收证据产生后勾选。未来代码路径以 `ForAndroid/` 为根；共享代码路径由 T01 固定。阶段门槛见[实施计划](plan.md)。

## A0 文档

- [x] **D01 能力图和模块规格。** 依赖：无。验收：七个稳定模块 ID 的职责/接口/依赖一致；总体计划、系统设计和七份规格完整，区分设计与已验证证据。验证：枚举文档、检查所有 Markdown 相对链接、`git diff --check`。涉及：`ForAndroid/README.md`、`docs/`。
- [x] **D02 任务与命令审核。** 依赖：D01。验收：每个实现任务有依赖、验收和验证；阶段停点与模块规格一致；未建立的构建入口标注为预期命令。验证：逐项核对能力图/任务 ID，检查当前仓库未新增运行时代码。涉及：`tasks/plan.md`、`tasks/todo.md`。

## A1 共享核心与可行性

当前 A1 原型证据见[核心验证记录](../docs/verification/core-2026-09-27.md)。下列任务含尚未完成的退出条件，故仍保持未勾选。

- [ ] **T01 固定基线并搭建工程。** 依赖：D02。验收：记录 Windows 全套测试与样本哈希；新 Android Gradle wrapper/NDK CMake 工程可生成空 AAR 和 Debug APK，`arm64-v8a` Release 与 `x86_64` Debug 明确分开。验证：Windows CTest、Gradle assemble 与模拟器安装空 APK。涉及：共享构建清单、`ForAndroid` 构建文件、版本清单。
- [ ] **T02 提取场景与相机数学。** 依赖：T01。验收：两平台编译同一平台无关源码，现有 Windows API 和数值行为不变，Android 公共头无 Windows 类型。验证：Windows 全套回归、NDK 编译及相机/布局夹具。涉及：共享头/实现、Windows 适配、测试。
- [ ] **T03 Vulkan 设备和 Surface 探针。** 依赖：T01、T02。验收：枚举 instance/device/queue/Surface/格式，真实呈现三角形或返回阶段化 UnsupportedDevice；模拟器实际结果记入证据。验证：`adb -s emulator-5560` 安装/启动/旋转/后台测试，真机复测 Vulkan 路径。涉及：`src/render-core/device/`、`GUI/` 探针宿主、测试。
- [ ] **T04 提取 PLY/SPZ 主机解码。** 依赖：T01、T02。验收：同一解析/规范化源码处理 Windows 与 Android fd 流，PLY 分块、SPZ 基础格式和非法属性行为相同。验证：两平台 ModelIo 语料、ASan/UBSan 可用配置及大文件峰值记录。涉及：共享 codec、Android model-io、测试。
- [ ] **T05 稳定 GPU 排序自检。** 依赖：T03。验收：Vulkan 1.1 无固定 subgroup 路径处理 0/1、同键、组边界和随机键值，启动不通过则停止 Ready。验证：真机 GPU readback 与 CPU stable sort 逐项比对、Validation 无错误；模拟器仅在探针支持时运行。涉及：`shaders/`、`src/render-core/passes/`、GPU 测试。

**检查点 A1：** Windows 回归、Android 主机语料、模拟器 fd/界面和真实 Vulkan 能力结果均归档；缺 Vulkan 的模拟器不能冒充 GPU 测试通过。

## A2 正确渲染

- [ ] **T06 文件描述符与隔离 Service。** 依赖：T04。验收：可 seek/non-seek provider 均可取消读取，Service 版本化协议和死亡接收器能返回结构化终态，不在 Binder 传大数组。验证：Android 仪器测试覆盖权限拒绝、Service 杀死、快速替换和临时文件清理。涉及：`src/model-io/`、`src/android-bridge/` Service/Manifest、测试。
- [ ] **T07 共享场景和动态 CPU 准入。** 依赖：T06。验收：共享 FD/布局经宿主二次校验，只读场景完整后发布；按实际内存增量核算，OOM 保留旧场景。验证：损坏偏移、取消、低内存注入、FD/映射计数和大 SPZ 峰值测试。涉及：共享布局、model-io、测试。
- [ ] **T08 Vulkan 分块上传和预算。** 依赖：T03、T07。验收：属性/SH/索引按设备限制分段，copy fence 证明进度，实际分配失败回滚待命资源。验证：上传字节、边界/溢出、强制低预算和设备内存统计测试。涉及：`src/render-core/scene/`、预算契约、测试。
- [ ] **T09 Gaussian 投影、SH 与合成。** 依赖：T05、T08。验收：SH 0-3、透明排序、近面/边缘、Vulkan Y 和色彩空间固定夹具正确；不丢点、不逐帧 CPU 排序。验证：固定相机 GPU 截图、CPU 数学对照与 Windows/旧查看器差异图。涉及：`shaders/`、`src/render-core/passes/`、图像测试。
- [ ] **T10 Surface/设备恢复与统计。** 依赖：T09。验收：resize/旋转/0x0、Surface loss、device loss 有界恢复；fence 后释放；低频 CPU/GPU 统计不阻塞帧。验证：100 次 Surface 重绑、故障注入、Validation 和 GPU 时间戳测试。涉及：`src/render-core/device/`、统计契约、测试。

**检查点 A2：** 真机稳定排序自检、固定截图和 Vulkan 验证层通过；首次 Present 前不报告 SceneReady。

当前技术预览已在模拟器实现 Gaussian/SH 着色器、生产帧 GPU radix 排序和首帧
Present 事务，但 T08 的分段上传、T09 的 SH 1–3 固定截图和 T10 的设备故障注入
尚未完成，故 A2 各项仍保持未勾选。证据见[引擎验证记录](../docs/verification/engine-2026-09-27.md)。

## A3 引擎、SDK 接口与应用

- [ ] **T11 引擎请求事务。** 依赖：T07、T10。验收：RequestId/ticket/generation 过滤迟到结果，替换失败保持旧模型，相机与场景同帧激活。验证：快速打开/取消/关闭和竞态确定性测试。涉及：`src/engine/`、公共内部契约、测试。
- [ ] **T12 相机及触控数学。** 依赖：T02、T11。验收：固定左拖方向、平移/缩放、自由移动/转向/升降、fit/reset 和镜像组合行为确定。验证：固定输入回放、方向/边界及非有限输入测试。涉及：共享相机实现、`src/engine/`、测试。
- [ ] **T13 版本化 C ABI。** 依赖：T11、T12。验收：不透明句柄、结构大小、错误码、fd/Surface 所有权和关闭语义固定；错误输入不崩溃。验证：旧头/新库消费、ABI 和 CheckJNI 前置测试。涉及：`include/`、`src/android-bridge/`、消费测试。
- [ ] **T14 Kotlin/JNI AAR。** 依赖：T06、T13。验收：Kotlin 异步 API/状态流、非导出 Service Manifest、无 Activity 强引用和跨线程 `JNIEnv*`。验证：单元/仪器测试覆盖双重关闭、Service 死亡与后台恢复。涉及：`sdk/`、`src/android-bridge/`、测试。
- [ ] **T15 手机/平板查看状态。** 依赖：T14。验收：空、加载/取消、Ready、失败、恢复均有可用界面；SAF 文件选择和无权限结果正确；手机/平板布局无遮挡。验证：模拟器截图/无障碍及状态仪器测试。涉及：`GUI/`、UI 测试。
- [ ] **T16 手势、自由模式与生命周期。** 依赖：T10、T12、T15。验收：固定/自由触控、适配/重置、旋转、后台/前台、Surface 销毁重建及移动输入清零正确。验证：100 次打开/旋转/关闭回放与真机交互复核。涉及：`GUI/`、bridge 生命周期、测试。

**检查点 A3：** 模拟器可验证 UI、SAF、Service 与错误状态；只有通过 Vulkan 探针的设备才计入真实图像验收。

T11–T14 已有请求状态机、相机、C ABI、JNI/Kotlin AAR 与模拟器联合测试；
旧头/新库兼容、事件队列与完整故障矩阵仍待验收。T15–T16 的查看器 App
已落地在 `GUI/`，模拟器交互、长时回放和 arm64 真机验收未全部完成，故暂不勾选。

## A4 性能、兼容与稳定性

- [ ] **T17 性能监控与基准。** 依赖：T16。验收：帧、排序、上传、内存和热状态低频采样，含设备/驱动/画质/样本哈希；日志不存完整 URI。验证：固定相机路径三轮原始数据与采样开销测试。涉及：`bench/`、统计/日志、验证记录。
- [ ] **T18 真实设备和等画质对照。** 依赖：T17。验收：Adreno 7xx 或同级 Mali/Immortalis、至少 8 GB RAM 真机上 100 万至 300 万点持续浏览满足中位 <=33.3 ms 且 1% low 不低于旧 `Viewer_android`；画质/像素相同。验证：30 秒预热、60 秒采样、三轮截图/CSV/温度报告；未达标不得勾选。涉及：基准脚本、`docs/verification/`。
- [ ] **T19 长时与故障矩阵。** 依赖：T18。验收：Service 崩溃、provider 撤权、低存储/低内存、Surface/device loss 和 30 分钟浏览均有可解释结果，无累计 FD/GPU 内存泄漏。验证：故障注入、资源曲线与真机复现记录。涉及：故障测试、`docs/verification/`。

**检查点 A4：** 整机 30 FPS 和跨 GPU 兼容性只凭真机证据判断；模拟器 9 GB 内存不替代真机预算和热测试。

## A5 发布

- [ ] **T20 AAR 与原生包。** 依赖：T14、T19。验收：正式包仅 `arm64-v8a`，包含同版 AAR/`.so`/C 头/CMake 消费信息、SPIR-V、Manifest、示例、完整文档和许可证。验证：解包清单、哈希、无绝对路径/样本/日志、Manifest 不导出检查。涉及：`packaging/`、`sdk/`、发布文档。
- [ ] **T21 独立消费与发布验收。** 依赖：T20。验收：仓库外 Kotlin 和 C/NDK 示例能安装、打开 PLY/SPZ、渲染、取消、恢复并关停；真机干净安装无额外源码依赖。验证：独立构建/设备运行日志、SDK 文档操作复演和版本检查。涉及：`examples/`、`tests/sdk/`、验证记录。

`packaging/build-sdk.ps1` 可生成含文档、许可及 SHA-256 清单的 arm64 技术预览包；
包外 C/NDK 链接及 Kotlin 编译消费通过。T20–T21 的正式门槛仍需 T18–T19
真机与独立应用运行证据，故不勾选。
