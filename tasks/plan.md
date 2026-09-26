# render-core 实现计划

## 引擎与 SDK 完善阶段

依据 docs/SPEC-engine-sdk.md，核心缺陷回归、上传拆分、相机及异步引擎、SDK 安装/部署与独立消费验证已完成。OCR 初轮、变更审查和针对超时文件的定向复审均已完成，结果与构建证据归档至 docs/engine-sdk-verification.md。

依据 docs/SPEC-render-core.md 实现 C++20 D3D12 独立模块。保持 model-io 无图形依赖；联合测试在测试目标连接两个模块。

1. 公共契约及 CPU 验证：相机、质量、场景长度、增量预算；GoogleTest 边界验证。
2. 硬件设备与固定 FidelityFX 排序：DXC 构建、key/value GPU readback 对照，包括 800 万键。
3. Gaussian 投影、SH 0-3 与预乘混合：合成图像 GPU/CPU 对照。
4. 非阻塞命令、copy 页上传、ticket 原子激活、surface revision、取消/关闭及有界恢复；真实 composition swapchain 测试。
5. model-io 联合加载真实语料；Release/VSTest、调试层检查、验证文档和提交。

硬件矩阵、Spark SSIM、PresentMon 同画质比较分别记录。缺少基线数据的验收项保持待验收，不推定通过。
