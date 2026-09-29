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

大场景打开时间目前主要受解码/验证约 9.7 秒制约。GPU 动态相机帧仍受投影、排序与绘制共同限制。本轮没有相同相机轨迹、相同画质且未受显示节奏限制的 Web Viewer/原生对照，因此技术计划中的用户可见帧时间改善 20% 目标尚未验收。此机器未发现 PIX 或 PresentMon 可执行工具；当前阶段数据来自引擎内 D3D12 timestamp、墙钟和桌面日志。

## 低显存路径

预算不足时，渲染器先降低 GPU 上传的 SH 阶数，SH0 仍不足时按源索引间隔 2/4/8/16 抽样；CPU 场景和源文件保持完整。大 PLY 的 SH0 全点估算约 2.59 GB；SH0、间隔 2 为 11,240,181 点，估算 1,331,608,496 B（含上传预留），上传 398 ms，预热后 GPU 帧 14.79-15.15 ms。降质帧时间不能与完整 SH3 比作等画质加速。

测试覆盖预算模拟下的自动降阶、降阶后抽样、`allow_memory_mitigation=false` 的严格拒绝，以及 GPU 读回核对抽样点和 SH 系数。完整 Release 构建、五组 CTest、VS 测试桥均通过；`git diff --check` 无空白错误。实际 WinUI 在 RTX 3080 上验证了完整质量大模型打开、持续绘制与正常关闭；没有 2 GB/4 GB 真实显卡，因此这些设备的实际预算、分配成功率、画质和交互帧率仍未验证。CPU 解码仍需足够的可用物理/提交内存，间隔 16 仍不符合预算时照常拒绝。

复现命令：

```powershell
.\out\Release\Native3DGSViewer.SceneBench.exe 'C:\Users\21544\Desktop\zhishan\jiulonghu_v1.ply' force 10 3
.\out\Release\Native3DGSViewer.SceneBench.exe 'C:\Users\21544\Desktop\zhishan\jiulonghu_v1.ply' force 10 0 2
ctest --test-dir out/cmake -C Release --output-on-failure
```
