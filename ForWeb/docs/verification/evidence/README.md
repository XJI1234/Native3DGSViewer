# 证据索引与使用范围

本目录保留原始数据及中间实验，模型文件不在仓库中。样本身份以model-manifest.json的SHA-256为准，源码及部署资产以build-manifest.json为准。报告只将完整成功的运行计入验收，不能仅凭存在JSON文件判通过。

| 证据 | 用途 |
| --- | --- |
| gpu-tests、image-tests、integration-tests.json | 真实GPU排序、数学/图像与生命周期边界 |
| sdk-consumer.json | 独立安装包、SSR、文档示例编译、开发StrictMode及生产卸载 |
| model-tests.json | 全部38文件的加载或明确拒绝，以及替换/取消/恢复压力 |
| stability-tests.json | 100次生命周期及30分钟运动；complete:true和shutdown全零才通过 |
| stability-interrupted-*.json | 修复前被主动中断的诊断，不计长稳通过 |
| sort-baseline、sort-hierarchical-scan、sort-local-rank.json | 排序阶段逐步优化，不能当作整体帧收益 |
| decode-baseline、decode-compiled-fields.json | 预加载WASM probe+normalize五轮；不包含完整加载流程 |
| radix-experiment、radix-render-equivalence.json | 4/8-bit微基准与九视角逐字节一致性 |
| benchmark-full | 默认4-bit/Spark完整双引擎、30s+60s×3，约120Hz节流 |
| benchmark-retuned | 8-bit整帧复测，仅本引擎，依据不足以替换默认4-bit |
| benchmark-uncapped | 正式无节流请求，实际持续渲染；报告需确认六组数据、三轮、无错误 |
| benchmark-calibration、benchmark-aligned | 画质/坐标校准历史，不作为最终性能结论 |
| raf-cap-probe、benchmark-cadence-probe | 短期调度探测，不是正式基准；后者与长稳并存 |
| spark-sort-freshness.json | 3秒预热+12秒独立诊断，已完成排序输入相机与当前相机的时间/姿态差；不代表动态图像误差或物理呈现 |
| dependency-audit.json | 当次依赖审计结果，不保证未知漏洞不存在 |

PNG、差异图和CSV可以独立审阅。原始RGBA仅本地生成且被Git忽略。summary.json由performance-summary.py从CSV生成；image-quality.json由image-metrics.py从双方RGBA生成；comparison.json由benchmark-comparison.py生成。基准结果中的GPU计时范围不同：本引擎覆盖投影/全量排序/绘制，Spark query不覆盖异步Worker排序及后续提交。rAF间隔和其1%low不等于已验证的物理显示器呈现；静态九视角SSIM不验证动态排序新鲜度。

ownedResources记录测试插桩中的资源所有权和GPUBuffer分配大小，JS heap来自浏览器performance.memory；没有测量实际VRAM residency、驱动缓存或整个进程RSS。全部证据属于本机Windows/Edge/NVIDIA配置，不能替代跨平台验收。
