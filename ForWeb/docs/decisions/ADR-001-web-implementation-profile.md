# ADR-001：首个可验证的 WebGPU/WASM SDK 配置

状态：Accepted for local engineering baseline，2026-10-04。用户已授权依据计划实施。本文取代架构草案中尚未实现的默认执行/包结构推测；未来增强另立ADR。

## 决定

内部模块使用src/{splat-types,model-io,render-core,engine,web-adapters}。对外一个ESM SDK及react/vue子路径，不提前发布多个npm包。公共场景数据保持opaque，WASM与WGSL布局属于renderer私有契约。

主线程仅做短GPU提交，解码在独立Worker。当前不提供渲染Worker执行选项、SIMD/pthreads产物、云客户端或Three.js scene桥接。它们不是静默失效的可选参数。EngineOptions暴露实际canvas/assets/limits；capabilities.execution固定main。

排序默认使用八个4bit稳定LSD pass，分层扫描避免单工作组串行前缀瓶颈；8bit虽改善独立排序微基准，整帧复测无收益，因此保留为内部对照，不依赖subgroup/WaveOps。四输入页在投影后参与一次全局排序/绘制，页间透明排序不独立。基线保留完整float32与SH0–3。

错误/取消保留旧场景；加载总deadline覆盖解码、等待非零视口、上传和激活。关闭与恢复通过sceneEpoch防止场景复活；dispose等待上传/恢复清理。resize/pause/resume不能覆盖交易或Faulted阶段。Ready是GPU完成，不是显示器扫描确认。

默认输入256MiB、CPU估算1536MiB、GPU512MiB，WASM最大1GiB。大模型返回明确ResourceLimit，不抽点或降SH。CPU准入是保守估算，并非测量进程RSS；更大模型的分页/直接流式SPZ规范化需要独立实现，不用扩大一个数值冒充无限模型支持。

生产显式部署baseUrl与workerUrl，固定decoder.worker.js/mjs/wasm一起更新。独立安装消费者证实Vite重新打包不能自动携带库内默认Worker地址，因此提供Worker URL覆盖，并加入真正解码的包消费测试。

显示采用训练色值域、sRGB Canvas的UNORM路径、黑色不透明背景、premultiplied alpha。当前不提供透明背景/可调质量参数；架构草案中的扩展属于后续接口。相机fov固定60度。

## 证据与代价

GPU排序、图像、生命周期、模型和安装包测试及原始性能数据在docs/verification/evidence。排序百万键由87.82ms降至2.29ms；PLY字段预编译把预加载规范化由1104ms降至33.6ms，数字只对应各自测量范围。

单卡/单浏览器验证不能代表跨厂商或移动验收。默认预算可拒绝不少用户样本；把拒绝算作安全机制通过，不算作模型成功显示。Spark对照必须匹配坐标/SH/参数并通过前景画质，再报告性能；刷新率限制下不宣称FPS优胜。
