# Changelog

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
