# render-core 实现计划

## 2026-10-16 模型测距实施计划

用户授权开始执行，并明确允许任意比例尺，依据 `docs/SPEC-model-measurement.md` 的修订段。保留下面既有引擎/渲染工作及未完成外部验收。

依赖：metric 值类型/数学 → GPU 像素候选拾取 → engine 异步会话 → WinUI 测距 → SDK/真实样本验证。顺序实施，不启动其他代理。

1. 原生比例尺解析与距离：使用 Windows 内建 JSON、无模型对应校验；CPU 测试异常输入/矩阵/源坐标。
2. GPU 按需收集最多 16,384 候选，复用投影；CPU 求可见贡献加权中位深度。先用透明叠层、偏轴射线及空白验证，溢出必须失败。
3. 引擎独立导入线程、快照状态、取消/超时/代际过滤；scene 记录实际坐标选项，不更改 model-io。检查加载失败回退与关闭恢复。
4. WinUI 导入引导、点击/拖动、A/B/线及米制结果；测距暂用轨道输入，退出恢复。
5. 完整 Release、VSTest/CTest/安装 SDK 消费；真实样本烟雾及可用 UI 验证，记录尚未实测项。

检查点：每个数学/拾取切片先运行定向测试；集成后完整构建与套件。风险：候选过密明确上限错误；GPU 读回仅在完成 fence 后；旧帧/模型结果按操作与视图版本拒绝；任意标定可能不适用，产品注明由用户指定，不宣称独立实测精度。

执行结果：功能与 SDK 接入已实现，Release、完整 VSTest/CTest、安装消费和真实样本原生测距通过。桌面 UI helper 窗口归属恢复失败，可见交互与跨 DPI 验收待补；独立实测长度/长期资源曲线未提供。完整证据见 `docs/model-measurement-verification.md`。

## 引擎与 SDK 完善阶段

依据 docs/SPEC-engine-sdk.md，核心缺陷回归、上传拆分、相机及异步引擎、SDK 安装/部署与独立消费验证已完成。OCR 初轮、变更审查和针对超时文件的定向复审均已完成，结果与构建证据归档至 docs/engine-sdk-verification.md。

依据 docs/SPEC-render-core.md 实现 C++20 D3D12 独立模块。保持 model-io 无图形依赖；联合测试在测试目标连接两个模块。

1. 公共契约及 CPU 验证：相机、质量、场景长度、增量预算；GoogleTest 边界验证。
2. 硬件设备与固定 FidelityFX 排序：DXC 构建、key/value GPU readback 对照，包括 800 万键。
3. Gaussian 投影、SH 0-3 与预乘混合：合成图像 GPU/CPU 对照。
4. 非阻塞命令、copy 页上传、ticket 原子激活、surface revision、取消/关闭及有界恢复；真实 composition swapchain 测试。
5. model-io 联合加载真实语料；Release/VSTest、调试层检查、验证文档和提交。

硬件矩阵、Spark SSIM、PresentMon 同画质比较分别记录。缺少基线数据的验收项保持待验收，不推定通过。
