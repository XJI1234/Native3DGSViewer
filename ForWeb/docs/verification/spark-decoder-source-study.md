# Spark decoder source study

Reviewed 2026-10-04 against upstream commit `f5368253780bed4d863c96b1fdea9dd3d614d131` (MIT). Installed benchmark package remains `@sparkjsdev/spark@2.3.1`; upstream main is an architectural reference, not proof of the exact published binary's performance.

## Organization and useful mechanisms

| Source | Observed mechanism | Application here |
|---|---|---|
| [spark-rs/decoder.rs](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/rust/spark-rs/src/decoder.rs) | WASM boundary `ChunkDecoder.push`; reusable thread-local input Vec, maximum 1MiB JS copy per iteration | Keep JS/WASM boundary small, use bounded input batches; avoid retaining whole input and normalized scene together |
| [spark-lib/decoder.rs](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/rust/spark-lib/src/decoder.rs) | Pure Rust `ChunkReceiver`, `SplatReceiver`, batch and per-attribute setters, format dispatcher | Preserve provider boundaries: file decode owns normalization; renderer owns its private packing and GPU data |
| [spark-lib/ply.rs](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/rust/spark-lib/src/ply.rs) | Header parsed once; property access prepared ahead of point loop; reusable output arrays; maximum 65,536 splats per batch | Existing compiled property offsets remain; streaming batches add a byte bound as well as a point bound |
| [spark-lib/spz.rs](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/rust/spark-lib/src/spz.rs) | Stateful incremental decompressor and sections (centers, alpha, RGB, scale, quaternion, SH); 128KiB decompression output buffer with a 32KiB sliding history window | Incremental validated gzip and bounded attribute batches; inflated sections stored on disk instead of allocating a full cloud in WASM |
| [spark-lib/csplat.rs](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/rust/spark-lib/src/csplat.rs) | Compact representation uses f16 opacity and u8 RGB/scale/rotation | Our padding removal keeps float32; quantized memory/throughput gains require a separately declared quality profile and cannot be counted as an equal-precision improvement |
| [rust workspace](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/rust/Cargo.toml) | Core Rust library separated from WASM bindings and offline build-lod tooling | Preserve the current native C++ provider parity and pinned Emscripten build; adopting Rust itself is not necessary to adopt bounded buffers and streaming interfaces |

## Decisions and comparison constraints

The immediately useful design is streaming and memory reuse, rather than the implementation language alone. This engine already compiles validated native C++ into WASM. A rewrite would require revalidating coordinate conversion, sigmoid, quaternion normalization, SH conventions and corrupt-input handling. No upstream implementation was copied into the engine in this change.

SPZ attributes are columnar. Keeping a full vendor GaussianCloud for a 22m-point input is not a bounded solution. The new path inflates to OPFS, reads corresponding attribute slices, invokes the same vendor unpack mathematics on small batches, packs world-space floats, then rebases once using global bounds. The disk passes trade I/O for predictable memory and recovery. Their load-time cost must be reported separately from arithmetic decode cost; streaming is primarily a capacity/reliability improvement.

Spark has additional packed formats and LoD/tree construction paths. Those can improve storage and rendering performance, but this gate requires every point and all SH coefficients. Comparisons must distinguish complete decoding from quantized packing, tree construction, GPU upload, first ordering and first completed frame. A Rust/WASM label by itself does not establish which stage is faster.

Future justified experiments include reusable WASM input/output allocations, decoding directly into compact batches without an intermediate SoA copy, and format-native compressed GPU storage under an explicit quality contract. Each requires isolated before/after timings and existing math/input-contract tests.

## 架构阅读结论与本仓库对应关系

阅读入口为上表固定提交；分析源码不把 upstream main 与已安装的 2.3.1 WASM 二进制视为同一版本。

```text
TypeScript SplatLoader / SplatMesh
  -> SplatWorker：RPC、任务排队、可转移缓冲、WASM 初始化
  -> spark-rs：wasm-bindgen 边界、ChunkDecoder、packed/ext/LoD 对象
  -> spark-lib：独立格式解析、SplatReceiver、编码、空间排序、LoD 算法
  -> build-lod：离线转换工具，可用 wgpu 处理部分计算
```

[SplatWorker.ts](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/src/SplatWorker.ts) 把解码与渲染宿主分离，复用编译的 WASM 模块，按消息 ID 关联结果、错误和进度。其队列/worker pool 避免每个批次创建新线程。ForWeb 同样在 Worker 中执行 WASM，并以 operation identity 隔离旧任务；由于终止 Worker 是取消与总 deadline 的保障，复用 Worker 需要先规定状态复位、过期消息、存储锁释放和故障隔离，不能仅为减少启动成本删掉取消机制。

### 解码与建树是不同阶段

[worker.ts](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/src/worker.ts) 在完整解码后按选项调用 `tiny_lod` 或 `bhatt_lod`，单独记录 LoD 构建时长；已有树的格式可直接进入树数据路径。

[tiny_lod.rs](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/rust/spark-lib/src/tiny_lod.rs) 先按 feature size 排序，再按层级网格/Morton 空间次序聚合，保留子节点关系。[chunk_tree.rs](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/rust/spark-lib/src/chunk_tree.rs) 把树组织成约 65,536 个节点的批次，结合 AABB、轴向分割和空间排序。[lod_tree.rs](https://github.com/sparkjsdev/spark/blob/f5368253780bed4d863c96b1fdea9dd3d614d131/rust/spark-rs/src/lod_tree.rs) 提供页面/树生命周期与按视点遍历。建树收益来自可见层级选择、局部性和分页，并非格式解码速度本身。

可借鉴的后续设计是独立预处理器、稳定页面标识、每页包围盒、可取消的有界调度和保留完整叶子的树。LoD 选择会改变当帧点集合，必须由调用者显式选择质量策略，并另做误差与完整叶子验收。本轮 38 模型门槛保持全部点和 SH；没有以 LoD 结果替代完整加载，也没有实现或声称完成 LoD 建树。

### 已采用和暂缓的机制

| 机制 | 本轮处理 | 验证方式 |
| --- | --- | --- |
| 头部解析后准备字段访问 | 保留现有 compiled PLY layout | 重排属性与非法属性 WASM 契约 |
| 65,536 点批量并复用小缓冲的原则 | 同时以 4MiB 源字节限制批次；SPZ 并发读取六个列段 | 最大 SPZ 阶段计时、宽 PLY 与 64MiB CPU 预算 |
| gzip 流与格式阶段分离 | 增量校验后落 OPFS；逐批规范化并一次全局 rebase | CRC/截断/尾随成员与全缓冲数学等价 |
| 编码/传输/消费接口分层 | decoder 不依赖 WebGPU；renderer 管理私有 float32 布局 | provider 边界检查和真实 SDK 消费 |
| 紧凑表示和访问局部性 | 无损去 padding、64 点属性分块 | 65/127/129 点全属性数学及 GPU/图像测试、布局消融 |
| 量化、LoD、跨任务 Worker 复用 | 暂缓，分别需要质量/资源/隔离契约 | 不计入本轮性能收益 |

C++/WASM 路径继续复用已验证的 native normalization 与 SPZ unpack 数学，避免引入两套格式语义。后续若比较 Rust 与 C++，需让两端执行相同解码、规范化、编码和内存策略，单独计入 JS/WASM 拷贝、存储 I/O、建树、GPU 上传与首帧，才能解释差异。
