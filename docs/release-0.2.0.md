# Native3DGS 0.2.0

## 更新

- 新增 Android 10+ Vulkan 1.1 原生引擎与 `arm64-v8a` C/Kotlin SDK 技术预览。提供隔离 PLY/SPZ 解码、共享场景校验、无固定 subgroup 的 GPU 排序、Gaussian/SH 渲染、相机、Surface 事务及包外消费示例。实际 Android 查看器 App 尚未实现。
- Windows 大模型解码的辅助进程限额和场景准入改为运行时可用物理内存与提交内存的 80%，解决大 SPZ 在 50% 限额下被拒绝的问题。
- Android 共享导入和 SPZ 读取改为受检分块读取；取消、源文件截短、Surface 恢复、SDK 关闭及内存堆准入进一步收紧。

## 验证

- Windows Release x64 构建和完整 CTest 5/5 通过；第三个大模型的联合测试在 80% 限额下通过。
- Android x86_64、模拟 Adreno 640 设备上原生 22/22、仪器 7/7，通过 Gaussian 首帧、GPU 稳定排序、PLY 经隔离 Service 到 SDK 的联合链路。`arm64-v8a` Release AAR 和原生库完成交叉构建。
- Windows SDK、桌面安装包与 Android SDK 均随此版本打包；包内内容和独立消费证据见各自验证记录。

## 限制

Android SDK 为技术预览：尚无 arm64 真机画质、跨 GPU、30 FPS/1% low、热节流及长时验收；单属性缓冲仍受 `maxStorageBufferRange` 限制，GPU 时间戳与分段上传尚未完成。Windows 安装包未签名，Spark 等画质与更多 GPU 厂商矩阵仍待验收。
