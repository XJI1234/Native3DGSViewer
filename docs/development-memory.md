# 开发记忆：Windows 大场景与 Android SDK（2026-09-27）

本文记录本轮可复用的工程结论；实际通过范围以各平台验证记录为准。

## Windows 资源准入

- `model-io` 辅助进程 Job 内存上限取运行时可用物理内存、可用提交内存二者较小值的 80%；场景输出也分别受两者 80% 约束。保留显式测试上限覆盖，实际 Job 限额命中仍返回 `ResourceLimit`。
- `zhihuizhimen.spz` 在 50% 限额时触发 `Helper memory limit`，改为 80% 后大样本联合测试和完整 CTest 通过。不要根据总 RAM 或输入压缩字节估算可加载性；记录当时可用提交内存与实际峰值。
- 更改共享 PLY/SPZ 解析源码后同时运行 Windows 五组 CTest 与 Android 端侧 model-io 测试。

## Android 场景与生命周期

- 系统文档选择器交付 FD。普通文件用 `pread` 独立游标，避免 `dup` 共享文件位置；SPZ 在解码前分块复制到私有内存，源文件被截短时返回错误。非 seek provider 使用受限、可取消的私有临时文件。
- Service 输出共享场景；宿主按块导入到私有只读快照，再校验布局与属性。取消/替换用请求 ID 过滤，旧结果不能更改活动场景。旧式 ashmem 字符设备在不支持位置读取时使用只读映射回退。
- `Ready` 只能在新场景或恢复后的场景实际 Present 后发布。Surface generation、upload ticket 和 request ID 分别保护表面、上传和解码事务。Kotlin `close()` 异步销毁；`closeAndWait()` 在后台协程等待原生工作线程与 GPU 资源释放。
- GPU 预算必须使用与实际缓冲分配兼容的 host-coherent 内存堆，并计入直方图、扫描层、排序缓冲与固定开销。Vulkan 分配失败仍须回滚并保留旧场景。

## 复测与发行

- Android x86_64 端侧：`cmake --build out/android-x86_64 -j 6`，随后 `ForAndroid/tests/run-device-tests.ps1 -Serial <adb-serial>`；仪器测试：`ForAndroid/gradlew.bat -p ForAndroid :sdk:connectedDebugAndroidTest :sdk:assembleRelease`。
- Windows：`cmake --build out/cmake --config Release --parallel 6`，`ctest --test-dir out/cmake -C Release --output-on-failure`。发行前分别验证解包后的 Windows SDK、Android SDK 与安装包内容。
- Android 当前只有 x86_64 模拟设备证据。arm64 Adreno/Mali 真机、SH 1–3 图像对照、Validation、长时热性能、分段上传和完整故障矩阵未通过；Android 包必须标注技术预览。
