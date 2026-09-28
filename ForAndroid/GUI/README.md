# Android 查看器

`GUI` 是 SDK 的原生 Android 宿主模板。它只依赖 `:sdk` 的公开
`Native3dgsEngine` API，不调用 JNI 内部对象，也不接触 Vulkan 和逐点数据。
正式发布时可将 `implementation(project(":sdk"))` 换为同版本 AAR 依赖；
参见 [SDK 接入指南](../docs/SDK-guide.md)。AAR 的隔离解码 Service 通过 Manifest 自动合并。

## 构建与运行

从仓库根目录执行：

```powershell
& .\ForAndroid\gradlew.bat -p ForAndroid :GUI:assembleDebug :GUI:assembleRelease
& .\ForAndroid\gradlew.bat -p ForAndroid :GUI:assembleDebugAndroidTest
```

Debug APK 在 `ForAndroid/GUI/build/outputs/apk/debug/`，支持调试用
`x86_64` 和目标 `arm64-v8a`；Release 仅含 `arm64-v8a`，默认输出未签名 APK，
须由发布方使用自己的正式证书签名后才能安装。设备需 Android 10+
和符合引擎要求的 Vulkan 1.1 GPU。安装后通过系统文档选择器打开 PLY/SPZ，
无需广域文件权限。要运行设备测试，使用 `:GUI:connectedDebugAndroidTest`。

## 宿主调用与界面

- Activity 创建单个 `Native3dgsEngine(applicationContext)`，收集 `state`，
  只在当前 Surface 有效时 `attach`/`resize`，暂停或销毁时 `detach`。
- `open(uri)` 在协程中执行；取消挂起的 Service 解码用协程取消，已返回
  请求 ID 的原生上传用 `cancel(requestId)`。只有 `Ready` 且
  `activeRequestId` 匹配时才更新可见模型名。应用关闭时在后台等待
  `closeAndWait()`。
- 固定模式：单指拖动旋转、双指平移和捏合缩放。自由模式：左摇杆移动、
  右侧拖动转向、右下按住升降。失焦、暂停、模式切换立即清零移动。
- 手机横屏为两行工具栏，平板宽屏为单行；两种布局均提供打开、关闭、
  适配、重置、Y 翻转、固定/自由与全质量/移动画质切换、加载取消和状态/文件名。

当前 SDK 为技术预览。模拟器可验证文件选择、状态和界面；图像质量、
设备丢失及持续性能仍需按 [查看器验证记录](../docs/verification/viewer-2026-09-27.md)
和 Android 技术计划在 arm64 真机验收。
## 性能构建

从 `ForAndroid` 运行 `.\gradlew.bat :GUI:assembleBenchmark`，得到原生代码使用
`RelWithDebInfo` 的测试 APK，包含模拟器用 `x86_64`；正式 Release 仍只包含
`arm64-v8a`。比较时固定模型、物理视口和相机运动。模拟器数据与限制见
[性能验证记录](../docs/verification/performance-2026-09-28.md)。
基准入口接受 `quality=full|mobile` 和可选 `scale_permille=750..1000`；
不指定比例时移动档按连续帧时间自动调整。CSV 记录实际缓冲尺寸、比例和
点数。设备测试脚本为 `bench/run-device-benchmark.ps1`，仅在明确开始真机
测试后运行；它会向设备传入样本并启动基准 Activity。
