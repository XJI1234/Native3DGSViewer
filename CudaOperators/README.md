# Windows CUDA 算子实验

状态：规格已批准，按切片实施中。分支：`codex/windows-cuda-operators`。

本目录用于验证 NVIDIA 专用路径能否在相同画质下改善现有 Windows 原生引擎。先验证互操作，再逐项比较排序、投影、可见集压缩、tile 合成和调度；未通过正确性、性能或稳定性门槛的路径不进入默认后端。

- [规格与逐项实验方案](SPEC-cuda-operators.md)：目标、契约、实验顺序、验收门槛和拟实施工作包。
- [本机 D3D12 基线](evidence/2026-10-17-baseline/README.md)：实际执行记录，和后续 CUDA 结果分开。
- [模型清单](evidence/2026-10-17-baseline/models.json)：用户指定目录的清单；主测试样本附 SHA-256。
- [环境与二进制标识](evidence/2026-10-17-baseline/environment.json)。

实施计划和任务在 [tasks/plan.md](tasks/plan.md)、[tasks/todo.md](tasks/todo.md)。根目录既有任务保留。[CUDA13.3工具链与互操作证据](evidence/2026-10-17-toolchain/README.md)已归档；后续算子与整帧收益按各实验验证。

当前文档描述实验要求，不代表 CUDA 已加速、已兼容 VS 2026 或已通过验收。
