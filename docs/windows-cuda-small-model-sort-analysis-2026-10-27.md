# Windows CUDA 小模型排序回退：原因与改进方案

2026-10-27。基于本次42文件性能对比、当前源码、本机CUDA13.3/CUB头文件以及NVIDIA、Microsoft、AMD资料。此次仅进行源码与资料调研、复用已保存数据，未新增长时间GPU测试，也未修改引擎。

## 1. 核心判断

**最有证据支持的主因是：小模型中CUB节省的计算时间，不足以支付DX12→CUDA→DX12的提交、依赖交接和调度成本。不能把当前结果理解为“CUB基数排序算法天生不如原算法”。**

原DX12排序虽然有40次compute dispatch，但可与投影、绘制一起预先录入一个DX12命令列表。CUDA路径的kernel数量可能更少，却把一帧拆成两份DX12提交和中间CUDA工作，增加了两个API执行域之间的依赖边界。小任务更容易受这些固定成本控制，大任务更容易体现CUB较低的排序数据流量。

此外，CUB当前SM80/86吞吐策略的较大线程块处理粒度、64位count带来的策略选择、kernel启动及初始化操作，可能进一步限制小任务效率；其贡献尚不能与驱动/调度成本精确分离。

建议按两条目标分别处理：

1. **让查看器在各种模型上都快：**保留DX12小模型路径，按短时校准结果选择大模型CUDA排序，保持相机不变时复用结果。这是最直接的产品方案。
2. **让CUDA路径本身在小模型上更快：**先缩短交接与CPU提交链，再尝试已有CUB Graph/int32组合；若仍不足，考虑把高效排序留在DX12执行域。单纯改线程数或换另一个CUDA排序库通常无法消除跨API成本。

## 2. 两条排序路径究竟有何不同

| 项目 | 原DX12/FidelityFX | 当前CUDA/CUB候选 |
|---|---|---|
| 输入/输出 | uint32径向key＋uint32原始索引；稳定排序 | 同样的key/value与稳定次序 |
| 主机入口 | `SortPass::record()` | `SortWorkspace::submit()` |
| 算法 | 8轮4-bit LSD；每轮Count、Reduce、Scan、ScanAdd、Scatter | 本机CUB3.3.3对SM86的32位key选择Onesweep；正常规模主digit轮宽8bit |
| 工作粒度 | 128线程×每线程4项，即512项块；count/scatter最多16000组 | 当前i64 key/value策略主Onesweep块为384线程×17项，即6528项 |
| 指令提交 | 8×5=40次dispatch，伴随40个UAV barrier；录在同一DX12 list中 | histogram、prefix、主digit kernel、清零等多项操作，由CUDA runtime提交；实际次数应以trace为准 |
| 每帧跨API交接 | 无 | D3D queue Signal→CUDA wait→CUB→CUDA signal→D3D queue Wait |
| 外部资源状态 | 排序期间沿用DX12 UAV状态 | 4个共享排序buffer每帧UAV→COMMON，再COMMON→UAV，共8次状态转换 |
| CPU/API边界 | 整帧通常一次`ExecuteCommandLists` | producer、consumer各一次提交，额外CUDA和共享fence调用 |
| 临时内存 | 场景创建时分配，重复使用 | scratch与共享映射也在初始化时建立，重复使用 |

依据：[原排序实现](../src/render-core/passes/sort.cpp#L162)、[CUDA排序](../CudaOperators/src/operators/sort.cu#L45)、[互操作](../CudaOperators/src/interop/interop.cpp#L97)、[帧提交](../CudaOperators/bench/scene.cpp#L575)。AMD官方算法描述与当前8轮5阶段源码相符[S1]；具体CUB策略以本机固定版本头文件为准，而非推测所有CUB版本都采用相同路径[S6]。

这里的Onesweep是“减少每个digit的全数组扫描”，**不是32位数据只需一个kernel就全部排完**。32位全key仍要处理多个digit，并有准备工作。原始Onesweep论文也明确面向大规模GPU排序，通过减少全局内存流量提高吞吐[S5]。

## 3. 证据如何定位问题

### 3.1 本次分段结果：差异集中在排序及交接区间

全部来自2026-10-27同一组3轮短测，单位ms；分段分别取中位数，不能与整帧中位数强行相加。

| PLY点数 | DX12排序 | CUDA排序＋交接 | 差值CUDA−DX12 | DX12 CPU提交 | CUDA CPU提交 |
|---:|---:|---:|---:|---:|---:|
| 170,799 | 0.124928 | 0.360448 | +0.235520 | 0.08215 | 0.21265 |
| 455,325 | 0.230400 | 0.376832 | +0.146432 | 0.17545 | 0.29715 |
| 804,758 | 0.334848 | 0.406016 | +0.071168 | 0.10420 | 0.20910 |
| 1,248,730 | 0.475136 | 0.473600 | −0.001536 | 0.19025 | 0.29860 |
| 3,914,609 | 1.290240 | 0.751104 | −0.539136 | 0.14690 | 0.28975 |
| 22,480,361 | 7.832064 | 2.681856 | −5.150208 | 0.19270 | 0.33925 |

CUDA提交CPU时间普遍多约0.10–0.15ms，与额外API调用的结构一致。它可能让GPU遇到供给间隙，但**不能把该CPU差值直接加到GPU区间上**：二者有重叠，因果分配必须看CPU/GPU时间线。

17万点排序及交接损失约0.236ms。要让它在整帧中打平，不是随便降低CUB kernel几个百分点就足够；需要显著缩短额外链路。约125万点排序区间才接近持平，说明固定成本正在被计算收益摊薄。该规模只是本机、当前配置的观察，不是通用切换阈值。

### 3.2 历史纯排序微基准：CUB计算已更快，但完整路径更慢

以下为仓库2026-10-17已保存的随机输入排序微基准，不是本轮新增测试。每次恢复相同key和原始索引，拷贝不计时，完整输出对CPU stable_sort校验。

| 项数 | DX12排序ms | CUDA完整排序区间ms | CUDA event区间ms |
|---:|---:|---:|---:|
| 513 | 0.1096 | 0.4581 | 0.0215 |
| 1,000,000 | 0.4349 | 0.6031 | 0.1792 |
| 8,000,000 | 2.6257 | 1.3151 | 0.8970 |
| 22,480,361 | 9.5442 | 2.8535 | 2.4351 |

百万项CUDA event区间约为DX12排序的41%，但完整区间仍更慢；大规模时则足以抵消额外成本。这是“集成路径固定成本主导”的强支持证据。

CUDA event区间包含event之间的准备/清零/kernel及潜在空隙，不严格等于所有kernel busy时间之和。完整区间与event区间的约0.42ms差额也不能全部命名为“fence耗时”：其中可能包括CPU提交供给、驱动排队、调度和时间域端点差异。两API未校准时间戳不宜直接当作可精确相减的单阶段钟表。

仓库还有interop-noop对照，其含义是**保留原DX12排序，再增加空CUDA往返**，不是纯空交接测量。它在小模型明显变慢，同样支持上述方向，但不能把它的整帧耗时全部算作interop成本。

来源：[历史微基准](../CudaOperators/evidence/2026-10-17-sort-distributions/README.md)、[noop原始组](../CudaOperators/evidence/2026-10-17-sort-diagnostic/analysis.json)。历史与本次相机、帧槽和冻结构建不同，不能混算一个新加速比。

## 4. 可能原因，按证据强弱排序

### A. 跨执行域的依赖、提交和GPU供给间隙：高可信主因

当前一帧为：

```text
DX12投影、keys写入、状态转换、producer提交
    → D3D Signal → CUDA外部semaphore Wait
    → CUB排序
    → CUDA外部semaphore Signal → D3D queue Wait
    → 状态转换、DX12绘制、consumer提交
```

零拷贝消除了完整数据经CPU/PCIe往返，但不会消除这些依赖。Microsoft说明queue Wait在CPU上立即返回、让GPU队列等待；因此这里不是每帧显式CPU同步等待CUDA，也不意味着GPU完全没有停顿[S2]。

Microsoft还说明，多次独立ExecuteCommandLists之间的边界及可插入fence，限制驱动跨列表合并执行的机会[S3]。这解释了为什么“40个DX12 dispatch”仍可能比“更少CUDA kernel＋两次跨API交接”快。

WDDM上下文调度、驱动批处理及缓存可见性维护可能放大这个成本，但目前没有GPU context-switch/WDDM trace，**不能断言某次固定上下文切换就是全部回退的根因**。也不能把某个固定微秒值当作Windows或CUDA的普遍规律。

### B. CUB的小任务启动与策略成本：中等可信，未量化

本机`CUB_VERSION=300303`，SM80策略对uint32 key/value、i64 count使用8-bit Onesweep、384×17的主块。17万点主digit只有 `ceil(170799/6528)=27` 个CTA，45万点70个，80万点124个；原DX12对应count/scatter为334、890、1572组。

较少CTA、每线程较多项、histogram/prefix/清零的固定工作量，可能使小任务无法充分发挥大数组吞吐优势。**CTA数不等于实际occupancy，更不能直接比较两个算法的利用率**；不同阶段、寄存器、shared memory、访存和依赖机制均不同，需短trace确认。

CUB已有single-tile小规模分支，本机该策略容量约256×19=4864项。用户所谓“小模型”最小也有17万项，远超过这个范围；因此“已经有小数组快路径”并不代表它覆盖了本次小模型。

64位count也不等于64位key：当前key/value都是32位。改成i32 count会改变地址/策略开销；本机策略主块相应变为384×21，**并不自动增加CTA数或提高小模型occupancy**。

### C. 资源状态转换与宽依赖：结构已确认，成本未拆分

sort-only每帧对4个buffer各做两次状态转换。它们是当前实现跨API访问次序的一部分，并非多余代码已被证明。不能直接删除COMMON转换、external wait/signal或把UAV barrier随意拿掉来提速。

原DX12也有大量UAV barrier，因此“有barrier”本身不足以解释谁慢；真正差异在于barrier覆盖的依赖、跨API资源可见性与提交边界。可考虑将同一资源阶段的多个ResourceBarrier批量发出以降低API记录成本，但需要保持原本正确的资源状态，不承诺因此消除主要GPU等待。

### D. 暖机、时钟和测量粒度：影响数值，不足以解释全部趋势

本次全量是60帧预热/120帧采样，小模型运行很短。17万点DX12的3轮中位帧时间也有波动，所以约48%的回退不应被解读成稳定到小数点后的算法常数。

但80万点已完成的单轮动态长采样仍回退约5%，历史不同测量组也重复出现小模型回退，故不能把问题全部归因于短测噪声。GPU event区间和统一D3D完整span应保留为两个不同指标。

### 已经排除或不应优先猜测的原因

- **每帧cudaMalloc/cudaFree：**sort scratch在workspace构造时分配、析构时释放，不在每帧热路径。引入cudaMallocAsync无法直接修复这里的回退。
- **每帧重新导入共享内存或创建context：**当前也没有这样做；它们主要影响初始化成本。
- **每帧CPU搬运完整keys：**当前共享buffer原地交接，输出通过CUB selector选择；没有常规全数组CPU读回。微基准的输入恢复和正确性readback有明确计时边界。
- **默认stream隐式同步：**context显式创建cudaStreamNonBlocking，所有CUB操作使用该stream，不能默认归因于legacy default stream。
- **CUDA只排序了可见点/格式不同：**主测两后端对同文件、完整count、相同key进行排序；PLY/SPZ解码差异不能解释同文件后端回退。
- **GPU显存带宽不足已被证明：**目前无带宽/缓存counter；小任务更应先看启动与依赖，不应直接建议升级显卡。

## 5. 解决方案及优先级

| 优先级 | 方案 | 预期价值与限制 |
|---|---|---|
| P0 | 小模型保留DX12，大模型按实测选择CUDA | 直接避免负收益；这是后端策略，不是修复CUB kernel。加载后以少量固定视角/轻微运动样本比较含交接整帧，设置收益余量和切换滞回；不要每帧切换或只凭文件扩展名选择。 |
| P0 | 保持相机/viewport/scene不变时复用投影和排序 | 产品DX12已有该语义，CUDA集成也应保留；可以省掉整次排序及交接。相机有影响排序/SH/投影的变化必须失效，不能无限复用旧排序或暗降更新频率。 |
| P1 | 缩短交接处CPU关键路径 | 在producer提交前尽可能预录consumer命令与确定输出buffer；批量记录同阶段资源转换；审查producer提交的完成fence是否与独立interop fence重复、是否确实无人消费。只删已证明冗余的signal，保留资源生命周期需要的最终fence。 |
| P1 | 重用已有CUB Graph，评估i32＋Graph组合 | 当前实现已有，先比较现成变体；主要降低CPU提交/小kernel发射成本，不会自动取消D3D往返。i32需要count范围校验，必须保持完整32位key与稳定顺序。 |
| P2 | Graph内加入external semaphore wait/signal节点 | CUDA runtime提供相应graph节点及exec更新接口[S7]；可能减少热路径调用，但D3D侧Signal/Wait和资源交接仍存在。每帧fence值、graph实例在途更新、帧槽所有权需重新设计和验证，不能复用固定旧fence值。 |
| P2 | 共享资源/真实数据帧槽独立化，评估有限重叠 | 当前三槽主要保护allocator/query/events，模型buffer和目标仍共享，fence链串行访问。增加CPU槽数不等于投影/排序/绘制已并发。真正重叠需独立中间资源、评估显存、排序年龄及输入延迟；同卡带宽竞争也可能使结果更差。 |
| P2 | 在DX12内实现更少扫描/更少阶段的稳定排序 | 保持HLSL投影和绘制，消除CUDA跨域代价，同时争取Onesweep类似的数据流量优势。实现成本较高，跨workgroup前缀/进度保证及稳定tie必须严格验证；不能直接把CUDA block算法翻译成HLSL就认定正确。 |
| P3 | 专门的小任务CUDA策略 | 尝试较小tile、不同radix宽度或小规模分层merge；需要CUB受控策略扩展或新算子，不能假设公开SortPairs有通用block-size参数。先证明kernel本身占可观比例，否则优化后仍会受交接下限控制。 |

### 已有实验对Graph和i32的实际约束

[既有三帧槽Graph对照](../CudaOperators/evidence/2026-10-17-queue-graph-diagnostic/README.md)中，17万点普通CUDA整帧约0.6533ms，Graph约0.6175ms；相对普通CUDA有约6%的改善，但原DX12约0.4014ms，仍差很远。80万点Graph的额外收益约0.74%。因此Graph值得作为小改进复用，不能作为已证实的翻盘方案。

[i32 count实验](../CudaOperators/evidence/2026-10-17-sort-i32/README.md)中，百万随机项event区间约0.1782→0.1638ms，节省约0.0144ms；800万与2248万随机项反而略慢。它改变的是count/offset类型，不是将32位key截断。不能据此承诺17万点或所有真实模型会同比加速。

NVIDIA关于Graph的研究针对特定Ampere、拓扑和CUDA版本，展示重复launch的CPU开销下降；文中的微秒数不是本项目Windows跨API帧的上限保证[S4]。

### 为什么不优先扩大CUDA范围、压缩可见集或异步用旧排序

把投影也移到CUDA理论上可减少中间交接，但本仓库CUDA投影＋排序已有慢于sort-only、排序key一致性及内存预算门槛未解决的记录。CUDA tile还涉及实际图像误差。扩大范围是独立架构研究，不是现成修复。

可见集压缩也已有实测成本与CUB host-count约束；本次最小模型在测试视角并非大量剔除，不能预设压缩收益。缩键宽、按桶近似排序、延迟更新排序会改变画质/时序，应独立评价，不能混入“同画质提速”。

## 6. 最小验证建议：不必再跑复杂的大矩阵

若后续实施，先选17万、80万、约125万、391万点，各取一个固定可见内容视角，短测即可筛选方向：

1. 复用现成的普通CUB、CUB Graph、i32及组合选项，与同三帧槽DX12比较；记录完整span、CPU submit和event区间，保留同key的完整输出及截图对照。
2. 只对17万点抓一次短Nsight Systems/PIX CPU＋GPU时间线：定位producer结束、CUDA wait完成、CUB实际执行、signal及draw恢复的间隙。Windows下按工具版本支持选择D3D12/CUDA/WDDM轨道，不把Linux专用context选项照搬[S8]。
3. 先验证“提前录制consumer/减少冗余提交调用”，再决定是否需要kernel策略实验。若CUB实际计算已很短，继续调block-size的优先级就应降低。

示意模型为 `CUDA完整代价≈H＋CUB计算(N)`，`DX12代价≈FFX排序(N)`；CUDA要胜出，需要 `FFX排序(N)−CUB计算(N)>H`。H是依赖、供给、调度等的综合项，会随环境变化，不是可从不同历史组精确拟合出来的单个固定常数。

本轮结论是**根因优先级与可行方案**，不是已实现或已验证的新加速。无需为给出上述判断重新进行全量长测。

## 7. 官方检索与证据

检索日期：2026-10-27。以官方开发文档、本机固定版本源码和原作者论文为主；未把论坛猜测或网页自动生成摘要当作机制证据。

- **[S1] AMD FidelityFX Parallel Sort**：[官方算法说明](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/parallel-sort/)。32位key、8轮4bit、每轮5阶段；仓库pin及实际循环独立核对。
- **[S2] Microsoft ID3D12CommandQueue::Wait**：[GPU侧等待语义](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-wait)。CPU返回与GPU依赖是两个不同问题。
- **[S3] Microsoft ExecuteCommandLists**：[命令列表提交/合并边界](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists)。
- **[S4] NVIDIA CUDA Graph性能文章**：[Graph重复launch与限制](https://developer.nvidia.com/blog/constant-time-launch-for-straight-line-cuda-graphs-and-other-performance-enhancements/)。采用正文说明，不引用页面自动生成摘要。
- **[S5] Adinets、Merrill，Onesweep**：[原作者论文摘要](https://arxiv.org/abs/2206.01784)。论文2022年与当时CUB的比较不等于今天CUB尚未集成Onesweep；本机实现已经集成。
- **[S6] NVIDIA CCCL/CUB**：[3.3.3源码入口](https://github.com/NVIDIA/cccl/blob/v3.3.3/cub/cub/device/device_radix_sort.cuh)。在线HTML代码内容未完整展开，具体调度以本机Toolkit头文件核对，版本、hash与路径见下方证据。旧CUB生成文档URL返回404，未引用其未读取内容。
- **[S7] NVIDIA CUDA13.3互操作与Graph API**：[13.3互操作文档](https://docs.nvidia.com/cuda/archive/13.3.0/cuda-programming-guide/04-special-topics/graphics-interop.html)、[runtime Graph接口](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__GRAPH.html)。后者检索时为13.4在线版本；已另外核对本机13.3 `cuda_runtime_api.h`，存在external semaphore wait节点及exec wait/signal参数更新声明。接口存在不代表拟议的多帧fence方案已实现或实测通过。
- **[S8] NVIDIA Nsight Systems**：[User Guide](https://docs.nvidia.com/nsight-systems/UserGuide/index.html)。用于区分CUDA API、kernel和WDDM/图形提交，不将profiler计时当正式性能采样。

本次资料清单、检索失败记录、源码指纹与分段重算见[调研证据](../CudaOperators/evidence/2026-10-27-small-model-sort-research/README.md)。原性能数据见[性能报告](windows-cuda-performance-analysis-2026-10-27.md)。
