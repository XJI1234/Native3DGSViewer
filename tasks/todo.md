# render-core 验证任务

## 2026-10-16 模型测距

- [x] T1 比例尺解析/坐标/距离；允许无 model 字段，拒绝坏矩阵；EngineMetric 4 项通过。
- [x] T2 像素候选与贡献中位深度；透明叠层/空白/溢出/抽样；RenderPick 5 项通过。
- [x] T3 renderer 非阻塞拾取；有界资源/过期过滤/fence 安全及单次分配错误隔离；GPU 套件通过。
- [x] T4 engine 异步会话；导入/两点/取消/模型切换、100 次重测和 20 次模式进出通过；实现 5 秒超时，超时注入未验收。
- [x] T5 WinUI 测距实现；引导、A/B/裁剪线、米值、Esc、拖动；Release 编译/链接通过，逻辑集中在 GUI/measurement.cpp。
- [ ] T5 交互验收；UI helper 窗口归属错误，未完成新版可见点击、选择器、Esc、拖动及跨 DPI 截图。
- [x] T6 集成与证据；MSBuild、完整 VSTest、CTest 5/5、独立安装 SDK 双点测距及真实 PLY+JSON 烟雾通过，见 docs/model-measurement-verification.md。
- [ ] 测距外部验收：独立长度精度、AMD/Intel、长期资源曲线、故障注入与实际延迟分布；不替代既有性能门槛。

检查点：T1/T2 定向测试 → T3/T4 集成测试 → T5/T6 完整套件与运行证据。既有 Spark 与硬件待验收项保留。

## 引擎与 SDK

- [x] OCR 初轮、SDK 变更及定向复审；有效发现已修复并归档。
- [x] 数值/队列/资源改进与上传组件拆分。
- [x] 纯 C++ 相机、fit/reset、轨道/飞行和回放测试。
- [x] 异步引擎请求事务、宿主恢复及后台关闭测试。
- [x] CMake SDK 包、部署函数、许可和独立消费测试。
- [x] 全量 Release/VSTest/ASan、基准与发布记录。

- [x] CPU 契约和数学测试。
- [x] 稳定 GPU 排序和 800 万键对照。
- [x] Gaussian/SH/透明混合图像测试。
- [x] 上传/取消/替换/resize/关闭/恢复生命周期测试。
- [x] model-io 联合语料测试。
- [x] 完整构建、VSTest、ASan 和验证记录。
- [ ] Spark 固定相机 SSIM 与 RTX 3080 等画质性能验收。
- [ ] AMD/Intel 硬件兼容性矩阵。
