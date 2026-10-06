# Changelog

## 0.2.2-preview.3 — 2026-10-06

- preview.3 默认启用 adaptive 排序和 auto/4 WASM；strict/single 可显式 opt-out，默认排序行为与 preview.2 不同。
- 增加可选 `decoder: { mode, threads }`；默认 auto，保留无需跨源隔离的单线程基础版，增强资源初始化失败或 CPU 预算不足时自动回退。
- 分块 PLY / SPZ v1–3 的规范化、验证、bounds 归并和 tile 打包使用 pthreads；减少 SPZ 范围复制、直接打包输出，并有界预取下一批。gzip 连续解压、SPZ v4 和小型内存路径保持单线程。
- 新增可选只读 `Snapshot.decoder` 活动场景诊断，不改变现有方法或必需字段；React/Vue 继承配置。
- SDK 增加 `assets/threaded/decoder.mjs` 和 `.wasm`，部署须递归复制资产。增强版需 HTTPS、COOP/COEP 和 SharedArrayBuffer；普通站点继续基础版。
- 迁移说明见 `docs/SDK-migration-preview3.md`，逐阶段性能、38 模型两路径等价、回退/取消和审查见 `docs/verification/pthreads-2026-10-06.md`。完整点数、SH 与 float32 精度保持；默认排序行为变化见迁移指南。

- 自适应策略每帧更新投影/SH/裁剪，在有界小位移期间复用完整排列；截图、Y 翻转、视口变化、突变与停止交互后的到期状态刷新排序。
- React/Vue 模板同步 preview.3、隔离响应头与新诊断；Release 仅保留新版 Web ZIP，不附独立哈希文件，包内完整性清单保留。

## 0.2.2-preview.2

- Display-only Y reflection with persistent canonical input; orbit/fly adapter navigation, pointer lock, WASD/QE, focus cleanup and keyboard controls. — 2026-10-05（连续交互调优）

- 自动绘制默认允许两个在途提交，新增固定初始化选项`maxFramesInFlight: 1 | 2 | 3`；队列满时消费最新相机，设备恢复隔离旧完成回调。
- 删除不可见投影记录的冗余GPU清零；保留完整点、SH3、float32及当前排序与透明度语义。
- 增加WASM/OPFS精确长度检查、连续交互/延迟/浏览器Present和阶段性能证据；私密浏览上下文的偶发缓存截短仍需定位。
- radix pass合并、工作组原子汇总、透明度边界收缩与6-bit radix经实验未保留。详细结果见`docs/verification/parallel-optimization-2026-10-05.md`。

## 0.2.2-preview.1 — 2026-10-04（Web SDK 预览）

- 完整大型模型采用有界 WASM/OPFS、逐页上传及投影，保留全部点、float32 和 SH；本机 38 个模型完整验收。
- 64 点属性列布局与稳定 radix bitmap rank 降低 GPU 投影/排序开销。
- 补齐挂起 GPU/I/O 取消、恢复 deadline、离屏首帧验证、临时纹理预算与迟到实例隔离。
- React 支持响应式容器元素；Vue 支持条件容器、重新绑定和保持 WebEngine 类型的浅只读返回值。
- 发布资产附实际 Web commit、依赖、SHA-256 和 Vue/React 接入文档；附于原生 v0.2.2 release，不冒充该 tag 的源码产物。未发布 npm。


## 0.1.0 — 2026-10-04（本地工程版本）

- WebGPU单模型3DGS SDK、完整SH0–3、稳定全局排序、PLY/SPZ1–4 WASM解码。
- 事务加载/取消、预算、相机、订阅、恢复、读回和幂等释放。
- React19/Vue3.5适配、独立tgz消费及部署/生命周期指南。
- 分层排序、PLY字段预编译优化；整帧复测后保留4-bit默认，8-bit内部对照。
- 100次生命周期+30分钟长稳、真实模型/图像、SparkJS2.3.1有节流/无节流及排序滞后证据。
- 无节流循环的20%中位间隔目标未达成；物理呈现/等动态质量及跨平台门槛仍待验收。

完整大型模型后续优化：

- 本机38/38模型完整加载，最大22,480,361点，保留全部SH3/float32。
- OPFS backing、有界PLY/SPZ v1–3 WASM、CRC/精确长度验证、分页投影与上传背压。
- 稳定bitmap rank、2D dispatch与分段prefix；64点属性分块和专门化shader经消融保留。
- 近零批次bounds误拒绝回归、存储/加载收尾竞态与GPU timestamp关闭超时修复。
- Spark Rust/WASM源码研读、完成帧对照及Vue/React资源与存储接入文档；最终证据独立归档。

未发布npm，现有原生/Android/Server接口未变。


2026-10-06 preview.3: optional `sorting.mode: 'adaptive'` retains full-point
ordering, projects current camera/SH, and rejects currently culled instances on GPU.
Default is adaptive from preview.3; explicit strict preserves the previous sorting behavior. No extra per-point GPU buffer; fresh capture, Y flip,
camera-jump and recovery semantics are specified in
[adaptive sorting contract](docs/SPEC-adaptive-sorting.md). See
[migration](docs/SDK-migration-preview3.md) for additive diagnostics and tradeoffs.
