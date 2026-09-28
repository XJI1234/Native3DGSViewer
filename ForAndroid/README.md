# Native3DGS for Android

状态：Android 引擎、SDK 和查看器 App 为技术预览。已实现 PLY/SPZ 隔离解码、Vulkan Gaussian/SH 绘制、稳定 GPU 排序、固定/自由相机、请求事务、C ABI 和 Kotlin AAR；查看器构建与操作见 [GUI 说明](GUI/README.md)。模拟器证据见[核心验证记录](docs/verification/core-2026-09-27.md)及[引擎验证记录](docs/verification/engine-2026-09-27.md)。真实 arm64 GPU 验收仍未完成。

首期目标是 Android 10+、`arm64-v8a`、Vulkan 1.1 的离线单模型查看器和可供其他应用使用的 SDK。Kotlin 原生应用负责文件选择、触控和界面；C++20 引擎负责解码协调、相机和 GPU 渲染。不使用 WebView 作为渲染后端。模型编辑、导出、多模型、LoD 和远程加载不属于首期。

## 阅读顺序

1. [文档索引](docs/README.md)区分已确定的设计、待真机验证的假设与实施证据。
2. [能力图](docs/capability-map.md)规定七个模块的责任和依赖方向。
3. [总体开发计划](docs/technical-development-plan.md)规定阶段、质量与性能门槛。
4. [系统技术设计](docs/system-technical-design.md)规定跨模块时序、线程、资源和故障处理。
5. [实施任务](tasks/plan.md)和[任务清单](tasks/todo.md)给出依赖顺序及检查点。

## 当前工程布局

与仓库的 Windows 工程保持可辨认的模块边界；已实现目录与未来目录同列，后者随对应任务创建。

```text
ForAndroid/
  docs/          总体方案、系统设计、模块规格及验证记录
  tasks/         分阶段实施计划和检查点
  include/       Android 原生 C ABI 与模块公共契约
  src/           model-io、render-core、engine、桥接实现
  shaders/       Vulkan GLSL 源码和构建生成的 SPIR-V 清单
  GUI/           Kotlin Android 查看器与 SurfaceView 宿主
  tests/         主机侧单元测试与 Android 真机集成测试
  bench/         固定相机路径、GPU/内存/温度采样
  packaging/     AAR、NDK 包和可重定位消费验证
  third_party/   Android 特有的固定版本依赖与许可证
```

共享场景值类型与 PLY/SPZ 探测、解码、规范化源码由 Android NDK 与 Windows 工程共同编译。Android 相机数学、请求事务及版本化接口位于 `include/engine/`、`include/native3dgs/`。SDK 接入见[指南](docs/SDK-guide.md)，打包入口为 `packaging/build-sdk.ps1`。`sdk` 模块的 Debug Activity 只用于仪器测试；正式查看器位于 `GUI/`。

## 构建与测试

需要 JDK 21、SDK 35、NDK 27.2.12479018、CMake 3.22.1 和连接的 `x86_64` 模拟器。从仓库根目录运行：

```powershell
& .\ForAndroid\gradlew.bat -p ForAndroid :sdk:assembleDebug :sdk:assembleRelease
cmake -S ForAndroid -B out/android-x86_64 -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_HOME/ndk/27.2.12479018/build/cmake/android.toolchain.cmake" -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Debug
cmake --build out/android-x86_64 -j 6
& .\ForAndroid\tests\run-device-tests.ps1 -Serial '<adb devices 中的序列号>'
& .\ForAndroid\gradlew.bat -p ForAndroid :sdk:connectedDebugAndroidTest
```

`run-device-tests.ps1` 上传固定样本与 SPIR-V，在模拟器执行 model-io、render-core 和联合 GoogleTest，并把 XML 拉到 `out/android-x86_64/test-results/`。模拟器测试不代替 arm64 真机图像、热和性能验收。
