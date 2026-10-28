# 自适应 CUDA 排序证据（2026-10-28）

结论与实现说明见[中文报告](../../../docs/windows-cuda-adaptive-sort-2026-10-28.md)。RTX 3080 10GB / 驱动 617.42 / CUDA 13.3 / CUB 3.3.3 / Release SM86；实际渲染点数低于 2,000,000 使用 DX12，达到阈值且实际适配器 CUDA 探测成功时使用完整 32 位稳定 CUB + Graph。

## 性能与图像

模型位于 `C:\Users\21544\Desktop\zhishan`；1920×1080、完整点数与 SH3、三命令帧槽、yaw 30° / pitch −45° / dolly 2。每次 60 帧预热、120 帧采样，两轮交替；GPU 指标采用同一 D3D12 时间域内从投影至绘制完成的 span，包含 CUDA 交接，另记 CPU submit。固定视角基准仍强制每帧计算排序，不能将倒数当作桌面呈现 FPS；实际桌面静止视角保留排序复用。

| 组 | 次数 | 内容 |
|---|---:|---|
| [baseline](baseline/) | 16 | 四模型 DX12 / 原 CUB 交接 |
| [experiments](experiments/) | 72 | 四模型、九后端、两轮，逐项交换链路及归并实验 |
| [confirmation](confirmation/) | 56 | 七个模型文件条目、四后端、两轮，含 PLY/SPZ 与阈值附近模型 |
| [roundtrip-graph](roundtrip-graph/) | 18 | 三模型、三后端、两轮，整段 wait/sort/signal Graph |

共 162 次运行，19,440 个采样帧，全部图像与各自 DX12 参考字节一致。confirmation 首轮 changjin 的四后端运行与一次产品测试有短暂重叠，整块计时排除；158 次有效计时，原始数据保留。其他性能组串行执行。每次运行的命令、模型与图像 hash、JSON 和原始 CSV 位于各组。

汇总：[逐次台账 attempts.csv](attempts.csv)、[summary.json](summary.json)、[summary.csv](summary.csv)。两轮组内中位数用于工程判断，没有把不同组漂移混为优化收益。约 125 万点存在接近噪声的微小 GPU 优势，未据此下调阈值；200 万阈值仅在本机校准。

保留普通 CUB Graph 的主机提交收益；提前录制、批量屏障、去 producer 冗余完成 fence、组合交接、整段互操作 Graph，以及本轮稳定并行归并均撤回。[废弃实验快照与清单](discarded-experiments/)用于复核，不参与产品构建；[归并对照测试](merge-tests.log)包含六种规模、四类分布共 24 个输入案例（单个 GoogleTest 内迭代）。这里只否定本轮实现，不推断所有归并算法均无效。

## 构建与产品验证

- [最终 Release 解决方案构建](validation/solution-build-final.log)：MSBuild `/restore /m /p:Configuration=Release /p:Platform=x64`，完成原生库、测试和桌面程序构建。
- [完整 CUDA 配置 CTest](validation/ctest-all.log)：`ctest --test-dir out/cmake-cuda -C Release --output-on-failure -j 1`，14/14 组通过，144.39 秒，含渲染、引擎、独立 SDK 安装及 CUDA 各算子。
- [最终产品 CUDA 测试](validation/production-final.log) / [XML](validation/production-final.xml)：`out/Release/Native3DGSViewer.Tests.exe --gtest_filter=RenderLifecycle.Adaptive*:RenderLifecycle.CudaPreparation*:RenderRecovery.FailedCuda*:RenderRecovery.ActualRemovalAfterCuda*`，5/5 通过、零跳过，记录 `cuda_available=true`；覆盖换模、静态复用、取消、清空、测距、准备失败回退、提交失败及真实移除后的场景恢复。
- [缺 DLL 回退](validation/no-cuda.log) / [XML](validation/no-cuda.xml)：在只含测试程序与 shaders 的独立目录运行 `RenderLifecycle.AdaptiveBackendRendersFullSceneWithOptionalProvider`，完整 2,000,000 点成功渲染，`cuda_available=false`，没有质量降级。
- [桌面小模型](validation/gui-small.log)：170,799 点，CUDA 可用但 `cuda_sort_active=false`；[桌面大模型](validation/gui-large.log)：3,914,609 点，`cuda_sort_active=true`。两次均完成加载/渲染并正常 `app_exit code=0`。[脚本](validation/gui-smoke.ps1)只启动和关闭本轮自有进程。此项核对状态日志，没有对角落提示做截图或像素检查。
- [正常配置详细记录](validation/adaptive-production-verbose.log)保留单独运行上下文。全量 RenderCore 进程此前已启用全局 Debug Layer，CUDA 专属覆盖由隔离的 AdaptiveSortProduction 组承担。

恢复开发中曾失败：[早期诊断](validation/recovery-diagnostic.log)、[普通配置](validation/recovery-production.log)、[早期 CUDA 配置](validation/recovery-cuda-production.log)。最终修正了测试宿主先释放旧 swapchain 的顺序，并修复引擎在 retained scene 重传完成前提前发出 DeviceRestored 的问题。早期失败记录不是最终结果，不能将其一律归因于 Debug Layer。另有既存调试层外部 fence 句柄滞留问题，尚未解决，因此产品诊断配置保守使用 DX12。

## 可复核范围

[源码与最终运行文件 SHA-256](validation/fingerprints.json)记录 HEAD、未提交 diff 摘要、源码、程序、DLL 和 shader 指纹。最终源码随后仅修正文档及一处调试层原因的注释措辞，未改变被测程序行为。冻结实验运行文件仍位于忽略目录 `out/cuda-results/{baseline,experiments,confirmation,roundtrip-graph}/runtime`；废弃实现另有源码快照，当前产品源码不含这些实验。

本轮为用户要求的工程短测，未覆盖两小时长稳、跨显卡/驱动矩阵和任意驱动 API 挂起的强隔离。PLY/SPZ 格式分别对各自 DX12 图像验证，不假设解码后逐点相同；本轮不修改加载/解码，加载耗时沿用前一轮报告。旧正式性能与稳定性门槛保持原状态。
