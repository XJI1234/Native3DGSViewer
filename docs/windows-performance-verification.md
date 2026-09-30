# Windows 性能与显存缓解验证（2026-09-30）

测试机：Windows x64、NVIDIA GeForce RTX 3080（10 GB 独立显存）、32 GB RAM，Release x64。`SceneBench` 使用 1920 x 1080 物理像素、默认 SH3 画质、固定相机及 `force` 模式，每次先预热 60 帧，再记录 10 帧 D3D12 GPU timestamp。加载和上传为墙钟时间。数据是此机器上的诊断结果，不代表其他显卡的性能。

| 样本 | SHA-256 | 点数 | 解码/验证 | 上传（64 MiB 页） | 预热后 GPU 帧 |
| --- | --- | ---: | ---: | ---: | ---: |
| `zhihuizhimen.ply`（923,849,202 B） | `D86FA4BE8A412F3B78169CCB3DD25044397FB1D2DD3F275C27497B6B39D3A669` | 3,914,609 | 1,686 ms | 272 ms | 7.08-7.38 ms |
| `jiulonghu_v1.ply`（5,305,366,675 B） | `BFABDEDB67D3D83DEA046834DFF6B50152DDA3C513EF48037A332671BD155C85` | 22,480,361 | 9,675 ms | 1,512 ms | 41.53-41.91 ms |

大样本完整 SH3 帧中，投影为 15.01-15.41 ms、GPU 排序为 10.06-10.40 ms、绘制为 16.37-16.64 ms。GPU timestamp 不包含模型解码、CPU 提交、Present 和桌面合成。冷首帧可超过 200 ms，不用它推断持续帧率。WinUI 实际启动并打开该 PLY 的 `scene_ready.open_elapsed_ms` 为 12,091 ms；后续稳定 `render_sample` 显示 22,480,361 点已绘制，静止相机复用排序时 GPU 帧约 16.3 ms。打开完成瞬间的空帧统计不计入稳定样本。

## 优化尝试

| 改动 | 同一大样本的结果 | 决定 |
| --- | --- | --- |
| copy 上传页 4 MiB 到 16、32、64 MiB | 完整 SH3 上传约 20.3、5.25、2.73、1.48 秒；本次 64 MiB 复测 1.51 秒 | 保留 64 MiB，显存估算包含 68 MiB 上传预留 |
| 投影椭圆 48 B 缩至 40 B | 22,480,361 点的投影缓冲少约 180 MB；完整画质 GPU 帧仍约 41.8 ms | 保留容量收益，不宣称帧时间改善 |
| shader 四元数矩阵展开 | 预热帧无可靠改善 | 撤回 |
| 上传页 128 MiB | 上传 1.47 秒，与 64 MiB 的波动范围相当；预留需再增加 64 MiB | 撤回，保留较低内存需求 |

### 2026-09-30 排序调度复测

沿用上面的 RTX 3080、Release、1920 x 1080、SH3、60 帧预热后 20 帧 `force` 基准。FidelityFX Parallel Sort 保持 8 次 4-bit radix pass 和稳定排序，只调整每次 pass 的最大工作组数；每个配置均通过 GPU 排序正确性测试。800 万随机键的独立 `SortBench` 每组取 10 次 timestamp；这些是同机单轮诊断，不代表跨设备最优值。

| 最大工作组 | 800 万键 GPU 排序 | 22,480,361 点场景排序 | 大场景整帧 |
| ---: | ---: | ---: | ---: |
| 800（原始） | 约 4.1-4.4 ms | 约 10.1 ms | 约 41.7 ms |
| 1,600 | 约 3.9-4.0 ms | 约 9.8 ms | 约 41.4 ms |
| 3,200 | 约 3.5-3.6 ms | 约 9.4 ms | 约 41.0 ms |
| 6,400 | 约 3.1 ms | 约 8.9 ms | 约 40.5 ms |
| 12,800 | 约 2.9 ms | 约 8.1 ms | 约 39.9 ms |
| 16,000（保留） | 约 2.8 ms | 约 7.8 ms | 约 39.6 ms |

中等 PLY 在 16,000 组时排序约 1.27 ms、整帧约 6.75 ms；原始 800 组分别约 1.66 ms 和 7.14 ms。投影和绘制时间没有明确变化。16,000 组对应 16 个 bin 的归约扫描最多 512 项，符合现有单工作组扫描的范围。该调度未改排序键、稳定性或画质；其他 GPU 的最佳组数尚未测定，Web Viewer 的用户可见帧时间目标仍按前述限制处理。

### SH 投影循环展开

保持 16,000 组排序后，将投影 shader 的动态 SH 累加循环展开为固定 15 次、按实际 SH 阶数条件执行。加载、相机、SH 系数和累加顺序不变；10 项 `RenderImage` GPU 图像测试通过。

| SH3 场景 | 原投影 | 展开后投影 | 展开后整帧 |
| --- | ---: | ---: | ---: |
| `jiulonghu_v1.ply` | 约 15.1 ms | 约 10.3 ms | 约 34.7 ms |
| `zhihuizhimen.ply` | 约 2.62 ms | 约 1.77 ms | 约 5.9 ms |

大 PLY 的 SH0、间隔 2 路径投影仍约 1.67 ms；此路径的排序随工作组调度降至约 3.6-3.9 ms，完整 GPU 帧约 13.5-13.8 ms。相同画质的 SH3 大模型相对本轮开始时约 41.7 ms 的 GPU 阶段总时间降至约 34.7 ms；这不是 Present 或 Web Viewer 的用户可见帧时间验收结果。

### 绘制几何复测

静止相机的同一大 PLY 在 1920 x 1080 与临时 960 x 540 视口下，绘制均约 16.5 ms；像素数降为四分之一仍未改变主要成本。基准视口已恢复。原来每点绘制两组三角形、执行 6 次顶点调用；改为覆盖相同两个三角形的 4 顶点三角带后，GPU 图像测试 10/10 通过。大模型 `cached` 绘制约 16.4 → 15.34 ms，中等模型约 2.85 → 2.69 ms。三角带不扩大光栅化区域，也不改变 Gaussian 的像素计算。

另试过一个 3 顶点覆盖三角形，图像测试通过且大模型绘制约 15.15 ms，但相对 4 顶点的收益约 0.2 ms，中等模型的测量受频率档位切换影响。该方案会额外光栅化支撑区外的像素，对大半径 splat 或低端 GPU 的代价未验证，因此撤回。最终保留 4 顶点方案；与排序调度和 SH 循环展开共同作用时，大 PLY 的 `force` GPU 帧约 33.4-33.8 ms。所有这些数字是 GPU 阶段诊断，不代替技术计划规定的同画质 Web Viewer 用户可见呈现验收。

大场景打开时间目前主要受解码/验证约 9.7 秒制约。GPU 动态相机帧仍受投影、排序与绘制共同限制。本轮没有相同相机轨迹、相同画质且未受显示节奏限制的 Web Viewer/原生对照，因此技术计划中的用户可见帧时间改善 20% 目标尚未验收。阶段数据来自引擎内 D3D12 timestamp、墙钟和桌面日志；PIX 复核见下文。

### 投影行列式改写复测

在上述三项优化后，尝试把 Gram cross 从 `cross(tx/sqrt(scale), ty/sqrt(scale))` 改为 `cross(tx, ty)/scale`。10 项 GPU 图像测试通过，包括薄各向异性高斯；相同大 PLY 的 20 帧 `force` 基准中，原写法投影约 10.2-10.5 ms、整帧约 33.5-33.9 ms，改写后投影约 10.2-10.5 ms、整帧约 33.4-33.7 ms。差异落在时钟/调度波动内，已撤回。当前大样本 22,480,361 个点均进入绘制，可见点压缩不能直接减少该样本的排序规模；若另有高裁剪率样本，应先量化实际可见数与压缩开销再设计动态排序。

### PIX 复核（2026-09-30）

安装 PIX 2603.25 后，在同一台 RTX 3080 上用 `pixtool attach ... take-new-timing-capture` 对运行中的 Release `SceneBench` 采集了中等与大模型 Timing Capture。原始文件分别为 `out/pix-medium.wpix`（43,864,064 B，SHA-256 `566023EC8E3FAA4C5CE81813003731742D705512315FB5BCB2AB6A43F0A7372D`）和 `out/pix-large.wpix`（51,572,736 B，SHA-256 `4539B8A97873745FDB95A6492F31989AE3DAC0923000B2623CC3F85ED3426734`）。它们是本机诊断产物，不随 SDK 发布。同期运行的 `SceneBench` 输出了 400 帧 D3D12 timestamp，其中位数为投影 10.62 ms、排序 8.03 ms、绘制 16.94 ms、合计 35.54 ms；捕获开销和显卡频率波动使这些数值不能与非捕获基准直接比较。同一模型另一次未捕获的 20 帧总时间为 34.45-37.39 ms，其中多数排序帧为 7.74-8.02 ms。收益方向与先前记录一致，但整帧的单轮区间比先前的 33.4-33.8 ms 略慢，不能把这部分波动解释成新算法收益或回退。

`SceneBench` 没有 Present，故采用 Timing Capture；桌面程序的启动期单帧 GPU Capture 只得到空白帧。开发者模式开启前，PIX 拒绝导出 GPU 事件列表与计数器，返回 `E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE`。此前三项已保留的优化仍通过引擎时间戳与图像/排序测试支持；本次未新增未经量化的优化。

开发者模式开启后，先导出启动空帧的基础事件列表，确认它只有清屏与 Present。随后在 `pixtool` 同一命令中先对桌面程序做 5 秒 Timing Capture，再抓取 `zhihuizhimen.ply` 的 GPU 帧，得到 `out/pix-viewer-medium-loaded.wpix`（718,380,319 B，SHA-256 `85C6536A6444F01507A70E5584FF276EAD3F1B131CC1EA4BA1CAA6501F950DE3`）。导出的 [基础事件列表](evidence/windows/pix-viewer-medium-loaded.csv) 共 39 个事件，包含 `ExecuteIndirect`/`DrawInstanced` 和 Present；`out/pix-viewer-medium-loaded.png` 的渲染截图非空。此帧相机静止，排序复用，因此没有投影或 radix dispatch。带计数器的 `save-event-list` 在该有效帧上返回 PIX 内部错误 `0x8000ffff`，`collect-occupancy` 返回 `E_PIX_GPU_PLUGIN_INITIALIZATION_FAILED`。这限制了事件级耗时与占用率分析，不能从此帧推断动态相机的投影/排序热点。

16,000 是 FidelityFX 排序每次 radix pass 的工作组上限，不是显卡执行单元数量要求；Arc 核显理论上可分批执行这些工作组。D3D12 `Dispatch` 的 X 维上限为 65,535，本实现的 16,000 也满足归约扫描的 512 项约束。实际最优工作组数与显卡的并行度、带宽和驱动有关；Arc 系列尚未实测，不能把 RTX 3080 的速度结论外推给 Arc。设备启动时的 GPU 稳定排序自检验证结果正确性，但不测该设备的最佳调度参数。

## 低显存路径

预算不足时，渲染器先降低 GPU 上传的 SH 阶数，SH0 仍不足时按源索引间隔 2/4/8/16 抽样；CPU 场景和源文件保持完整。大 PLY 的 SH0 全点估算约 2.59 GB；SH0、间隔 2 为 11,240,181 点，估算 1,331,608,496 B（含上传预留），上传 398 ms，预热后 GPU 帧 14.79-15.15 ms。降质帧时间不能与完整 SH3 比作等画质加速。

测试覆盖预算模拟下的自动降阶、降阶后抽样、`allow_memory_mitigation=false` 的严格拒绝，以及 GPU 读回核对抽样点和 SH 系数。完整 Release 构建、五组 CTest、VS 测试桥均通过；`git diff --check` 无空白错误。实际 WinUI 在 RTX 3080 上验证了完整质量大模型打开、持续绘制与正常关闭；没有 2 GB/4 GB 真实显卡，因此这些设备的实际预算、分配成功率、画质和交互帧率仍未验证。CPU 解码仍需足够的可用物理/提交内存，间隔 16 仍不符合预算时照常拒绝。

### 缓解接口与小场景预算审查

`QualityConfig` 增加手动 `point_stride`（默认 1）和自动 `max_point_stride`（默认 16），两者只接受 1/2/4/8/16 且前者不得大于后者。`RenderStats` 增加活动 ticket、源点数和源 SH 阶数；场景切换和设备恢复期间清空旧帧统计，已完成的旧场景 GPU 读回不得覆盖新场景。小场景 upload heap 页按实际打包字节数分配，预算预留为 `min(打包字节数, 64 MiB) + 4 MiB`；独显仅把默认堆场景计入 local，上传页计入 non-local，UMA 把两者合计到 local。1000 点 SH3 样本的上传预留从固定 71,303,168 B 降至 4,430,304 B；模拟 10 MiB local 余量时完整质量可加载。测试还覆盖手动抽样在严格模式生效、自动抽样上限、非法配置拒绝、SDK 快照中的源/实际点数，以及场景替换和设备恢复时的统计身份。真实低显存 AMD/Intel/NVIDIA 设备仍待验证。

真模型复测：`changjin_v1.ply`（40,310,041 B，170,799 点，SH3）按实际数据分配上传页，预算预留 44,502,868 B，解码约 92 ms、上传约 30 ms；1920 x 1080 的 `force` GPU 帧约 0.43-0.44 ms。大 PLY 的 SH0、间隔 2 路径计入 local 的默认堆需求为 1,260,305,328 B，non-local 上传预留为 71,303,168 B；解码约 9.78 秒、上传约 385 ms，10 帧 GPU 总时间约 13.0-13.35 ms。该路径降低了画质，只证明缓解选择能完整打包并绘制，不参与同画质性能目标。

复现命令：

```powershell
.\out\Release\Native3DGSViewer.SceneBench.exe 'C:\Users\21544\Desktop\zhishan\jiulonghu_v1.ply' force 10 3
.\out\Release\Native3DGSViewer.SceneBench.exe 'C:\Users\21544\Desktop\zhishan\jiulonghu_v1.ply' force 10 0 2
ctest --test-dir out/cmake -C Release --output-on-failure
```
