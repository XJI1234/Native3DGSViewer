# SDK 审查与接口指南验证

本轮对应 `out/ocr-android-final.txt` 中未处理的 Android 审查项。
已修复共享场景观察器异常转换、Surface 恢复与替换事务、相机回调并发、
空闲渲染线程关闭唤醒、零尺寸 Surface 等待、探针设备丢失清理、
探针超时后避免无界等待、PixelCopy 结果发布/位图回收，以及失败用例的
安全退出。
此前已合入的 Kotlin 解码代际保护、Windows 解码峰值预算及 resize 原子性
保留在当前基线。本次核对 `detach_generation` 使用非递减最大代际；
“detach(1), attach(2), detach(2)” 会清除仍在渲染的代际 1，
因此旧报告将其判为丢失解绑的结论不成立。

## 验证

- Android x86_64 模拟 Adreno 640：原生 model-io、render-core、联合与 engine
  测试共 29/29。新增用例覆盖异常观察器、Surface 替换/恢复、回调重入、
  并发相机命令与 resize 冲突时的单次回调语义。
- Android SDK 仪器测试 8/8，含反复创建/关闭空闲引擎、隔离解码到首帧及
  Surface/PixelCopy 画质检查。arm64 Release AAR 已交叉构建。
- Windows Release CTest 5/5；安装包外的重定位 SDK 宿主构建并运行，
  报告 `SDK OK frames=131 active=1 splats=1179648`。
- Android SDK ZIP 清单的 44 个文件哈希均匹配；包外 C 和 Kotlin
  消费示例独立编译通过。Windows SDK ZIP 同时包含顶层和 `docs/`
  内的接口手册。
- `git diff --check` 通过。OCR 复审输出保存在构建目录，
  不作为版本化交付文件。

接口使用说明见 [Windows SDK 接口手册](SDK-API-reference.md) 和
[Android SDK 接入指南](../ForAndroid/docs/SDK-guide.md)。
Android arm64 真机的画质、性能、设备丢失和长时稳定性仍需验收；
当前模拟器结果不能代替这些检查。
