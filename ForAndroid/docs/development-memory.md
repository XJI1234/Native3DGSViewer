# Android 引擎开发记忆（2026-09-27）

- 普通文件 FD 使用 `pread` 独立游标；SPZ 解码前按块复制到私有内存，输入被截短时返回受控错误。非 seek provider 复制到受限的应用私有临时文件。
- Service 输出共享场景，宿主按块导入私有只读快照并重新校验。Android 10 旧式 ashmem 字符设备不支持位置读取时走只读映射回退。取消和迟到结果按请求 ID 处理，旧场景保留到新场景首帧 Present。
- Surface generation 与上传票据独立验证；恢复后的场景实际 Present 前保持 `Recovering`。关闭时 Kotlin `close()` 异步执行原生销毁，`closeAndWait()` 在后台协程等待完成。
- Vulkan 场景预算按实际可分配的 host-coherent 堆计算，包含直方图和扫描层缓冲；预检不能替代实际分配回滚。
- 本轮 x86_64 模拟设备完成原生 22 项和仪器 7 项测试。arm64 真机、跨 GPU 画质、30 FPS/1% low、热稳定性、分段上传和故障矩阵仍需独立验收。详细命令和证据见[验证记录](verification/engine-2026-09-27.md)。
