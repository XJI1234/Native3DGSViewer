# Spec: model-io

## 目标与依赖

接受 Android 文档提供者的只读文件描述符，将标准 3DGS PLY 或 SPZ 变成完整、只读的 `splat-types` 场景。模块不创建 Vulkan 资源、不解释触控或 URI 界面文本；其隔离解码 Service 属于此模块的运行时，Manifest 注册和 Kotlin 生命周期接线由 `android-bridge` 完成。

## 输入、接口与协议

内部请求至少携带 RequestId、由接收端独立 `dup` 的 fd、可信的可选长度提示、显式 PLY 坐标约定、调用者可收紧的限制与取消通道。输入真实类型由签名/头部识别；不接受仅凭 `.ply`/`.spz` 后缀。PLY 为二进制小端原始 Graphdeco 属性，支持未知额外标量但拒绝未知 list/不完整 SH；SPZ 首期为 Niantic v1-v4 基础文件、SH 0-3。压缩 PLY、ASCII/大端 PLY、SPZ 未支持扩展返回明确错误。文件描述符是否可 seek/大小是否稳定由实际操作验证；不可 seek 的 provider 流按受检大小复制到应用私有临时文件，复制可取消并在所有终态删除。

Service IPC 有明确协议版本、请求 ID、阶段、完成字节、总字节可用性、场景布局版本、共享 FD 和结构化错误；Binder parcel 不传逐点数组。原生引擎按块位置读取共享 FD 到私有映射，经全部数据校验、只读保护和版本/偏移重检后发布 SceneHandle；导入支持请求取消。旧或取消请求的回复直接关闭 FD 并丢弃，不改变活动场景。Service 死亡接收器把当前请求转成 `DecoderCrashed` 一类稳定错误；下一请求重新绑定，避免无限自动重试。

宿主完整校验统一在原生引擎的 `import_shared_fd` 边界执行。Kotlin IPC 客户端只校验消息版本、请求 ID 和终态，交付的 FD 始终视为不可信；不得在交付前再复制并扫描整份场景。原生导入仍复制为宿主私有只读快照，并完成全部布局、内容与预算校验后才进入 GPU 上传，因此 Service 后续修改或损坏数据不能激活场景。

## 解码与资源机制

复用平台无关的 PLY 头预检、按块解码、log-scale、logit-opacity（正负无限映射 1/0，NaN 拒绝）、DC/SH 顺序与坐标规范化；Niantic SPZ 的完整 cloud 解码保留隔离边界。PLY 每批目标约 4 MiB，不映射或复制整个大输入。字段和 `count * stride`、输出 SoA、大文件偏移均用受检 64 位计算；出错包含阶段、点索引/属性/字节偏移（若可得），不把原始模型内容写进诊断。

预算来自场景输出、SPZ 解压/临时 cloud、共享映射、provider 缓存、当前活动场景及系统可用内存的增量峰值；`ActivityManager.MemoryInfo` 与系统低内存信号是动态预检，实际 `mmap`/分配失败仍必须正确回滚。可选 `android:largeHeap` 不作为必备条件或容量保证。Service 进程的内存限制和宿主 GPU 预算独立判断；不按输入字节数或总 RAM 写死“手机一定可以打开”的门槛。取消、进程终止和超时需要有界收尾，宿主必须能继续打开其他模型。

## 实现任务和验收

先提取并验证主机可运行的 PLY/SPZ 解析/规范化；再做 `ParcelFileDescriptor` 与 seek/non-seek 语料；随后实现 Service IPC、共享映射和死亡/取消；最后接入动态预算与大场景计时。GoogleTest 覆盖完整、损坏、截断、无限 opacity、跨块、中文文件名、长度变动、恶意偏移；Android 仪器测试覆盖 SAF provider、Service 被杀、重复替换和取消。至少 100 次开关/替换无 FD 或映射持续增长，失败后旧场景仍可用，且 100 万至 300 万点样本的 CPU 峰值、加载时长有记录，才算首期通过。
