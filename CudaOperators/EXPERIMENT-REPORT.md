# Windows CUDA 算子实测与取舍

分支：`codex/windows-cuda-operators`。测试输入来自 `C:\Users\21544\Desktop\zhishan`；本轮完成工具链和各条主要算子路径的实验筛选，**尚未获得可以进入产品的 CUDA 后端**。原生 D3D12 仍是默认实现，根构建开关 `GS_ENABLE_CUDA_OPERATORS` 默认 OFF。

后续深度调研与R01–R07增量实验见[第二轮报告](RESEARCH-ROUND-2.md)：新增160次真实模型诊断、
SH布局/自有输入、头依赖修复、排序图像误差因果证据和外部worker故障注入。它们不替代本报告
七轮正式性能结果；CUDA投影仍受key一致性、82.75%预算占用及Debug Layer/故障退役门槛阻塞。

## 环境与可复现性

RTX 3080 10GB、驱动617.42、Windows/WDDM、VS2026/MSVC19.50、CMake4.3.2、CUDA13.3.0、C++20、SM86。NVIDIA官方组件逐一核对SHA-256；nvcc13.3.33、cudart13.3.29、Compute Sanitizer13.3.27已真实编译运行。安装在当前用户的 `AppData/Local/Native3DGSViewer/toolchains/cuda/13.3.0`，构建脚本显式选用该路径。详见[工具链收据和探针](evidence/2026-10-17-toolchain/README.md)。

主测试为170799、804758、1248730、3914609、22480361点的五个PLY，源文件SHA在[输入清单](evidence/2026-10-17-baseline/models.json)。目录共40个PLY/SPZ；其中两个SPZ只做过原生烟雾测试，不能说全部40个已通过CUDA验收。

所有比较保持1920×1080、原始完整点数/SH、32位径向键和同键稳定次序、原画质参数。GPU总耗时由同一D3D12时间域覆盖完整跨API依赖链，包含交接与绘制。短诊断使用60帧预热/120帧采样；正式测试每后端30秒预热/60秒采样、AB/BA整轮配对，区间按整轮重采样。每帧不是一个独立实验。正式采集不与构建、sanitizer或其他项目GPU测试重叠。

这些是离屏GPU时间，不能换称显示FPS或输入延迟。固定俯斜视角有可辨认的校园内容，但仍不是动态浏览/Present验收。三轮数据不确定性较大时按规格扩展到七轮，保留全部原始轮次。

## 逐项实验台账

| ID | 已执行的路径与证据 | 实测结论及处置 |
|---|---|---|
| E00 | [原生force/cached/orbit基线](evidence/2026-10-17-baseline/README.md)：45次、5400帧；五模型及两个SPZ烟雾 | 最大模型force约34.72ms，投影10.50、排序8.10、draw15.91ms。cached不等于动态重算，分开列报。 |
| E01 | [共享buffer/纹理、10000次双向fence](evidence/2026-10-17-toolchain/README.md)；[noop整帧对照](evidence/2026-10-17-sort-diagnostic/README.md)；[生命周期分类](evidence/2026-10-17-toolchain/lifecycle-notes.md) | 正常读写和交接正确；noop全场景回退，WDDM交接固定成本不能省略。正常100生命周期通过，Debug Layer句柄与故障析构仍阻塞生产。 |
| E02 | [CUB全数组整帧](evidence/2026-10-17-sort-diagnostic/README.md)；[四分布逐次重置](evidence/2026-10-17-sort-distributions/README.md)；全key/value对CPU stable_sort至22480361项 | 最大规模随机排序含交接2.85ms，对照9.54ms；不能将该排序加速比当整帧加速比。小场景整帧回退，只有大场景保留研究价值。 |
| E03 | [CUDA投影/SH128和256线程](evidence/2026-10-17-project-diagnostic/README.md)；[rcp/rsqrt独立及组合消融](evidence/2026-10-17-project-math-diagnostic/README.md) | 图像通过（初版最大3/255、数学变体1/255），但CUDA投影慢于HLSL，数学变体无稳定胜出。拒绝产品选择；保留参考实现支持后续实验。 |
| E04 | 同一[75次投影/排序组合对照](evidence/2026-10-17-project-diagnostic/README.md) | 最大模型组合GPU降低6.49%/8.90%，弱于排序单项；共享SH映射的DXGI计费也未解决。不因扩大CUDA区域就默认保留。 |
| E05 | [全可见](evidence/2026-10-17-compact-fit-diagnostic/README.md)、[部分可见](evidence/2026-10-17-compact-partial-diagnostic/README.md)、[全剔除](evidence/2026-10-17-compact-culled-diagnostic/README.md)：共132次，完整容量及GPU选择容量桶 | 图像byte一致。所有有内容主测试均慢于全数组CUB；大模型部分可见仍慢3.17%。空帧收益不能证明可见内容加速。短诊断中的CPU构建重叠已单独披露，不作正式验收。 |
| E06 | [64位预算列表及CUDA合成](evidence/2026-10-17-tile-list/README.md)；五模型8/16列表；1301层CPU oracle、FP32/逐splat UNORM | 列表稳定次序/溢出整帧拒绝通过；analytic合成在shengyi/tumu超出原最大像素误差，且明显更慢。拒绝产品路径。 |
| E07 | [相同列表/公式/512项批次HLSL对照](evidence/2026-10-17-hlsl-tile/README.md)和[画质调查](evidence/2026-10-17-hlsl-tile/quality-investigation.md) | CUDA/HLSL差≤1/255，但都不能复现原生薄quad全部亚像素覆盖/插值；不是单凭换语言可解决。中模型约33.20/31.17ms，对照1.27ms。 |
| E08 | 同一证据目录中的前到后0/1e-5/1e-4、普通Graph，16次短诊断 | 夹具与memcheck通过，真实图像仍失败且慢。实际贡献数统计尚未实现；停止扩大该失败路径的正式矩阵，不放宽阈值。 |
| E09 | [1/3帧槽、普通CUB Graph90次消融](evidence/2026-10-17-queue-graph-diagnostic/README.md)；[24组调试层槽边界](evidence/2026-10-17-queue-validation/README.md) | 同三槽原生对照，大模型GPU约降低14.86%；Graph额外大场景收益落在噪声内。正式候选选较简单的普通CUB三槽，每帧用当前相机重新排序。 |
| E10 | [fit正式](evidence/2026-10-17-sort-queued-formal/README.md)、[pan正式](evidence/2026-10-17-sort-queued-partial-formal/README.md)、俯斜正式；[CUDA OFF/SDK回归](evidence/2026-10-17-regression/README.md) | 正式候选仍受下面的性能与稳定性门槛约束；未做SDK/WinUI CUDA集成。回归通过不代表新增后端生产验收通过。 |

conditional-WHILE Graph单独失败：即使最小CUDA-only复现仍有间歇非法访问/error999及清理停顿，准确归属未定。[失败attempt](evidence/2026-10-17-tile-list/README.md)全部保留，诊断probe不注册为稳定CTest。正常验收路径采用普通有界kernel/普通Graph，不将该失败说成已修复。

## 正式性能与画面

最大模型22480361点，原生/CUB均三帧槽，所有固定图像逐字节一致。fit与pan为较早冻结构建；俯斜为增加Context/队列归属校验后的冻结构建。组间不混用二进制hash或样本。

| 视角 | 配对轮数 | 原生→CUB GPU ms | 平均配对耗时降低及95%区间 | 非零alpha面积 |
|---|---:|---|---|---:|
| fit |3|34.8365→29.7892|14.45% [14.23,14.58]|2.06%|
| pan-x900 |3|25.2360→21.0248|16.70% [16.62,16.81]|1.42%|
| [yaw30/pitch-45/dolly2俯斜](evidence/2026-10-17-oblique-formal-seven/README.md) |7|34.9251→29.7800|14.77% [14.65,14.94]|12.78%|

fit和pan呈近水平窄带；pan达到15%不代表常规浏览也达到。完整校园俯斜五模型[三轮诊断](evidence/2026-10-17-oblique-diagnostic/README.md)中，CUB相对同三槽原生在17万/80万/125万点分别回退49.91%/6.96%/1.31%，391万点降低7.92%，2248万点降低14.77%。不能全局启用，也不能仅凭点数自动选择后端。

七轮合并工具核对相机、输入、质量协议、采集器及全部运行时文件hash，拒绝重复原始组/非独立run，保留原组清单hash与轮次来源。统计不会把补测四轮当成丢弃前三轮的替代数据。

七轮共26184个测量帧，14次运行全部退出0、图像byte一致，22480361源点/可见点与12.7778%画面面积一致，全局AB/BA顺序正确。最终区间整体低于15%；本例未通过可选产品后端性能门槛，停止继续堆叠或挑选轮次来追求越线。

## 正确性、维护与生产门槛

[归属回归](evidence/2026-10-17-ownership/README.md)先用RED实际复现跨Context写坏哨兵，再加实际D3D12 LUID、首次begin队列绑定和所有算子输入归属检查；错误调用在提交前拒绝。修复后CUDA CTest8/8、memcheck五测试零错误，普通tile racecheck零hazards、synccheck零错误。它们分别覆盖工具支持范围，不能推断所有跨API竞态都不存在。

公开SDK无CUDA类型，model-io不依赖GPU，算子私有DLL/实验host与产品隔离；CUB/投影/压缩/tile分别有头、实现和行为测试。固定Graph绑定地址、stream、count，变化须重建；最终边界修复另有RED/GREEN记录。失败实现只为可复现的实验依赖保留，不称为已采纳优化。审查见[quality-review](tasks/quality-review.md)，具体任务状态见[todo](tasks/todo.md)。

[Graph零count回归](evidence/2026-10-17-graph-zero/README.md)补齐已捕获513后传0的拒绝，RED失败后修复；Graph两项、普通排序边界通过，memcheck零错误，未捕获零容量仍允许no-op。该修复不改变普通正规模CUB计算，正式测量保持原冻结版本，不冒称重新测过。最终[Debug Layer复核](evidence/2026-10-17-lifecycle-recheck/README.md)仍为592→692句柄，普通隔离进程通过；问题尚未修复。

尚未通过的项目及下一步：

1. Debug Layer进程下CUDA fence退役约1 Event/Context持续增长；普通配置100次通过。先定位实际持有方及释放时机，保持原句柄阈值，不能未经所有权证明强关句柄。
2. 不可完成fence/设备移除时，导入资源保留及scratch的cudaFree没有全API有界退出证明。需要资源退役所有权和故障隔离设计，再逐项注入，不能只延长超时。
3. sort-only正式组观测DXGI峰值约7.16GB、预算约9.74GB，符合80%；投影共享SH路径出现映射计费问题。应分别建立准入策略，不能拿全局nvidia-smi替代进程预算。
4. 全40模型/SPZ、更多静态与动态相机、GUI呈现/输入延迟、100次resize/取消/测距/设备恢复、安装器和两小时长稳均未完成。须先取得画质、资源及性能合格候选，才能按W15先更新提供者契约并集成、再执行W16。

本轮不降低5%研究收益、15%生产后端及其他场景≤3%回退标准，也不降低图像阈值。**有局部GPU收益证据；产品稳定性与用户浏览提速没有完成验收。**

## 复现入口

从仓库根执行；GPU验证依次运行，正式计时独占测试环境：

```powershell
pwsh -NoProfile -File CudaOperators/tools/build.ps1 -Configure -Targets Native3DGSViewer.CudaTests,Native3DGSViewer.CudaBench
ctest --test-dir out/cmake-cuda -C Release --output-on-failure -R '^Cuda'
python CudaOperators/tools/collect.py --models jiulonghu_v1.ply --backends d3d12-queued cuda-sort-queued --image-reference d3d12-queued --yaw-degrees 30 --pitch-degrees -45 --dolly-steps 2 --formal --repetitions 7 --output CudaOperators/evidence/<new-directory>
python CudaOperators/tools/analyze.py CudaOperators/evidence/<new-directory>
```

不要复用现有输出目录，也不要在计时期间编译或运行sanitizer。保存精确命令、输入/运行时hash、逐帧CSV、最终PNG、内存采样、进程外GPU起止快照和全部错误日志；快照不是连续GPU遥测。
