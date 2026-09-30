# Spec: sdk-package

## 目标与交付物

把 Android 原生引擎交付给不依赖本仓库源码的应用。首期版本从 `0.1.0` 开始，工件含 Release AAR、`arm64-v8a` 的 native `.so`、原生 C 头与 CMake 消费配置、Kotlin 和 C/NDK 示例、完整 `ForAndroid/docs/`、版本/ABI 清单、固定依赖与许可证。`x86_64` 只供已连接模拟器和 CI 调试构建，不进入正式发布包，除非后续明确扩展正式 ABI 范围。

AAR 包含 Kotlin API、JNI `.so`、非导出的解码 Service Manifest 声明、必要 consumer rules 和许可说明；原生包暴露版本化 C ABI 而非 C++ STL ABI。应用同时引入 AAR 和原生包时必须共用同版本 `.so`，不能加载两个状态不同的引擎实例。模块发布记录构建工具、shader/SPIR-V 哈希和所有第三方源码版本；没有模型样本或用户日志进入 SDK 包。

`0.2.2` 发布包命名为 `Native3DGS-SDK-0.2.2-Android-arm64-v8a.zip`，与查看器的 `Native3DGSViewer-0.2.2-Android-arm64-v8a-preview.apk` 对齐。清单中的产品版本为 `0.2.2`，C ABI `api` 字段对应头文件 `GS_ANDROID_API_VERSION`，当前为 2。APK 的内部 `versionCode` 必须高于已发布的 `0.2.1`；预览 APK 使用开发签名，发布说明须明确标注。

## 构建与兼容

固定初始工具链见[总体计划](technical-development-plan.md)。Gradle wrapper 管理 AGP/Kotlin 构建，CMake + NDK 构建原生库，`glslc` 预编译 shader；Debug 可启用 Vulkan validation、ASan/HWASan 支持时的专项测试，Release 关闭高开销验证。所有包从干净目录生成，不读取开发机的绝对路径；CMake package/Prefab 消费声明须能定位头、库和运行时资源。版本号同时进入 Kotlin、C ABI 和包名，ABI 不兼容变更提升主版本并提供迁移说明。

SDK 文档提供最短接入路径、权限/Manifest 合并、`Surface` 与 fd 所有权、错误码、相机命令、线程约束、后台/销毁、Service 故障和日志导出。第三方应用不需要复制源码或知道内部 Service IPC。用户主动安装示例 APK；发布签名密钥不存仓库。SBOM/许可证清单覆盖 Niantic SPZ、zlib、zstd、测试与 shader/排序来源；外部 Spark 代码只作对照，不未经许可复制。

## 独立消费测试和退出条件

发布前将 AAR 与 NDK 包解压/安装到仓库外临时目录；用仅依赖工件的 Kotlin 示例及 C/NDK 示例构建，验证无模型启动、打开标准 PLY/SPZ、Surface 绑定、取消、场景替换、恢复和有界关停。检查包内完整规格和操作文档、Manifest 服务不导出、仅含承诺 ABI、无开发机路径/测试样本/日志。Debug 模拟器包可做状态和安装检查；正式 GPU 图像、兼容性和性能报告须来自真机。任何消费测试失败不得标记 SDK 可发布。
