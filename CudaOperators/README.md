# Windows CUDA 算子实验

状态：规格已批准，按切片实施中。分支：`codex/windows-cuda-operators`。

本目录用于验证 NVIDIA 专用路径能否在相同画质下改善现有 Windows 原生引擎。先验证互操作，再逐项比较排序、投影、可见集压缩、tile 合成和调度；未通过正确性、性能或稳定性门槛的路径不进入默认后端。

- [规格与逐项实验方案](SPEC-cuda-operators.md)：目标、契约、实验顺序、验收门槛和拟实施工作包。
- [本机 D3D12 基线](evidence/2026-10-17-baseline/README.md)：实际执行记录，和后续 CUDA 结果分开。
- [模型清单](evidence/2026-10-17-baseline/models.json)：用户指定目录的清单；主测试样本附 SHA-256。
- [环境与二进制标识](evidence/2026-10-17-baseline/environment.json)。

实施计划和任务在 [tasks/plan.md](tasks/plan.md)、[tasks/todo.md](tasks/todo.md)。根目录既有任务保留。[CUDA13.3工具链与互操作证据](evidence/2026-10-17-toolchain/README.md)已归档；后续算子与整帧收益按各实验验证。

CUDA13.3/MSVC19.50/SM86工具链、共享buffer/纹理、10000次fence交接及全规模稳定排序已实际通过测试。[5模型/3轮排序实测](evidence/2026-10-17-sort-diagnostic/README.md)表明单独CUB排序在小模型回退，在2248万点场景降低GPU帧耗时约14.2%；尚未通过正式生产后端门槛。

构建：`pwsh -NoProfile -File CudaOperators/tools/build.ps1 -Configure -Targets Native3DGSViewer.CudaProbe,Native3DGSViewer.CudaTests,Native3DGSViewer.CudaBench`。
硬件测试：`ctest --test-dir out/cmake-cuda -C Release --output-on-failure -R '^Cuda'`。
采集：`python CudaOperators/tools/collect.py --output <新证据目录>`；分析：`python CudaOperators/tools/analyze.py <该目录>`。

实验host另支持cuda-project-128/256、cuda-project-sort-128/256。它们只改变投影/排序，保留原生draw、原始点数/SH和质量参数。E03的projection_ms含CUDA投影及交接，E04的该列包含整个CUDA投影+排序区域；不能把它当作纯投影kernel时间。完整gpu_total_ms使用同一D3D12时间域，覆盖跨API依赖链。offscreen时间不表示显示帧率或已通过生产稳定性验收。

[E09三帧槽/Graph三轮消融](evidence/2026-10-17-queue-graph-diagnostic/README.md)完成90次运行、全图byte一致。CUDA三帧槽相对原生三帧槽在391万/2248万点约降低GPU帧耗时7.78%/14.86%，小模型仍回退，Graph在大场景没有独立收益。正式候选采用普通CUB三帧槽。采集支持 `--formal --image-reference d3d12-queued --backends d3d12-queued cuda-sort-queued`；每后端30秒预热/60秒采样、默认3轮。

[E06–E08画质失败调查](evidence/2026-10-17-hlsl-tile/README.md)：analytic CUDA/HLSL tile对薄quad的亚像素光栅化/插值不能完全复现，两个中等模型超出原最大误差门槛，并且明显慢于原生。提前终止和Graph无法解除画质失败，不接入生产。conditional while另有未解决稳定性问题，正常路径禁用。
