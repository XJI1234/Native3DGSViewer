# Spec: Windows 原生 3DGS CUDA 算子加速

状态：**用户已批准规格并授权完善 CUDA 13.3 工具链及执行计划**；2026-10-17。模块 ID：`cuda-operators`。

本次能力是 render-core 的可选 NVIDIA 加速路径。排序、投影、tile 是该路径的实验变体，共享场景、资源生命周期与验收协议；不新建解码、GUI 或服务端产品模块。用户后续指令“完善本地cuda13.3工具链，然后执行计划”批准范围与实施，计划和任务位于 `tasks/`，不重复请求阶段批准。

## 1. 目标与假设

为 Windows 本地 3DGS 查看提供可维护、可测量、可退出的 CUDA 路径，逐项判断其相对现有 D3D12 后端的净收益。模型根目录为 `C:\Users\21544\Desktop\zhishan`，递归包含其 `spz` 子目录。保持模型只读，不上传或修改源文件。

假设：

1. Windows x64、现有 RTX 3080 10GB 为主验证设备；其他设备未经实测不宣称通过。
2. D3D12 后端始终可单独构建运行；CUDA 默认为可选构建、显式实验选择，不自动替换现有默认。
3. 完整点数、源 SH 阶数、1920×1080 物理像素为主比较；禁止自动降阶、抽样、LoD 或改变 Gaussian 支撑范围换速度。
4. 用户要求逐条测试：即使前一路无收益，也保留证据并推进不依赖该优化的下一条路径；环境/资源/正确性阻塞单独登记，不能标成“无收益”。
5. 不承诺所有模型提速；最终允许结论为仅局部适用，或全部 CUDA 方案不值得集成。

依据：`../docs/technical-development-plan.md`、`../docs/system-technical-design.md`、`../docs/SPEC-render-core.md`、`../docs/SPEC-engine-sdk.md`。现有 Viewer 的等画质 20% 外部验收仍独立存在，本实验不代替它。

## 2. 本机现状与工具链门槛

- 起点提交：`75a47714e4002f10666fe2b9265dfdc5e3e8439c`，从当前含测距功能的分支建立新分支。
- GPU：RTX 3080 10GB，WDDM，驱动 617.42；环境原始数据见 evidence。
- PATH 的 nvcc 为 **11.8.89**；`v13.3` 目录缺少 nvcc 与 `include/cuda_runtime.h`，不能视为完整 Toolkit。
- CUDA 11.8 的本机 `crt/host_config.h` 拒绝 `_MSC_VER >= 1940`。当前项目使用 VS 2026；现有 nvcc/宿主组合尚不合格，不能直接假定可编译。
- `ForServer/src/cuda_renderer.cu` 已含 Linux/CUB 参考实现，但其 CMake 在 Windows 关闭 CUDA。先审查算法、许可、错误与布局契约，不直接搬入 WinUI，也不改动服务端模块。

实施第一项是选择并锁定**官方支持的 Toolkit + MSVC + CMake CUDA 编译组合**。优先现代完整 Toolkit；若采用并存旧宿主，必须验证 C++20 宿主边界、CRT/ABI 和部署。禁止 `--allow-unsupported-compiler`、修改 vendor 的版本检查、全局切换 PATH 或降低工程语言标准。缺少可用组合时先记录依赖阻塞，再决定安装范围，不能报告算子测试通过。

锁定 Toolkit 精确版本、nvcc 路径、MSVC 工具集、Windows SDK、CCCL/CUB 版本及许可证、CUDA runtime 链接方式。RTX 3080 以 SM86 为实测目标；PTX/其他架构只是构建范围，不能等同硬件验收。CUB 优先使用该 Toolkit 的固定版本，不另拉浮动最新依赖。

## 3. 边界与资源契约

依赖保持 `splat-types → model-io / render-core → engine → GUI`；`cuda-operators` 作为 render-core 私有实现，由其调用，不依赖 GUI/文件解码器。独立 benchmark 可以同时使用 model-io 和渲染实现。

### 3.1 构建与接口

- 预定构建开关 `GS_ENABLE_CUDA_OPERATORS=OFF`。关闭时不探测 Toolkit、不链接 cudart；原 SDK 消费方不需要 CUDA SDK。
- 启用构建时，CUDA实现封装为按需加载的私有后端DLL，使用明确的应用部署路径；引擎启动不要求CUDA DLL在场。CUDA后端或其runtime缺失时，D3D12仍能启动；显式请求CUDA则返回可解释错误。部署测试覆盖缺少DLL、无NVIDIA及版本不匹配。冷初始化/首帧成本单列，不混入预热后的kernel计时。
- 初期只通过实验 host 选择路径；若要增加正式 `QualityConfig`/SDK 后端选择或统计字段，**先修改提供者规格及契约测试**，再更改公开头文件。
- 私有边界采用批量 buffer view、容量、布局版本、stream/帧槽状态与结构化结果。公共头不暴露 `cudaStream_t` 或 CUDA 头；不逐 splat 调用。
- 主实验模式可独立选择D3D12、interop-noop、CUDA排序/投影/组合/压缩、CUDA/HLSL tile、提前终止及Graph。实际带线程块/tile/调度后缀的模式名以bench/scene.cpp白名单为准，实测命令在各组results.json固定。
- 组合方案必须保留单项消融；不建立多个长期维护的重复 renderer。稳定后保留 D3D12 与少数有收益的 CUDA 策略，实验失败的实现撤回、台账保留。

### 3.2 D3D12/CUDA 互操作

归属检查切片（2026-10-17）：构造Context时核对实际D3D12 device LUID与声明LUID一致。Context首次begin绑定同device的一条队列，之后begin/end必须使用该队列；错误调用在提交任何fence/kernel之前拒绝，正确队列仍可结束尚未完成的交接。Context提供私有owns(buffer/texture)查询；fill、sort、project、compact、tile/list/composite全部拒绝其他Context资源，即使adapter相同或count为0也不借用另一stream的所有权。组合workspace构造前核对列表归属。先建立负测，再实现；正式采集的冻结二进制不受后续源码修改影响。

1. 按适配器 **LUID** 匹配 CUDA 设备与 D3D12 设备；多 GPU、无 NVIDIA、不可共享资源时明确失败或选择已标识的 D3D12 后端，不能选错卡拷贝后冒充零拷贝。
2. 用可共享 D3D12 heap/resource 与 external memory 导入，按实际 allocation size、offset、alignment 和资源类型映射。共享纹理使用合法的 array/surface 映射，不能把纹理当线性内存。
3. 共享 D3D12 fence 导入 external semaphore；每帧为单调值：D3D12 producer signal → CUDA wait/计算/signal → D3D12 consumer wait。明确资源状态转换及访问独占区间。
4. GPU 排序数据和完整图像不逐帧经 CPU 中转；测试 readback 仅作正确性或 fence 完成后的异步统计。常规路径禁止 `cudaDeviceSynchronize()`/同步 CPU 等待来串行每帧。
5. 采用有界帧槽；资源 resize、替换、取消、关闭只能在其最后使用完成后回收。导入对象、Win32 HANDLE、device buffer、event、stream 全部 RAII，单一所有权。
6. 合并统计 CUDA、D3D12、共享资源与临时列表的真实内存；不重复计算别名，也不遗漏 CUDA 分配。遵守现有 DXGI 预算策略，所有字节数/列表数使用受检 64 位运算。
7. 启动失败可显式回到 D3D12。运行时 kernel/fence 故障不能假装完成或复用仍在访问的资源；撤销在途帧，按有界超时/设备重建协议恢复。失败保留旧场景语义、generation/ticket 过滤与测距拾取契约。

## 4. 实验顺序与每条路径的判据

顺序运行 GPU 实验，不并行争用显卡。每项先过正确性与资源门槛，再测性能；错误方案不进入有收益组合。后续路径可从已验证 D3D12 基础或独立实验 host 出发，不强制依赖前一路的速度胜出。

| ID | 路径与对照 | 单一待验证问题 | 必须记录/验收 |
|---|---|---|---|
| E00 | 当前 D3D12，force/cached/orbit | 当前热点、静态复用与动态代价 | SHA、场景/质量、逐帧阶段时间、三轮离散度；本轮先做诊断 |
| E01 | 共享 buffer/纹理与 noop 交接，对照 D3D12 | 零拷贝互操作是否正确，固定同步代价多大 | 读写校验、LUID/大小/状态、空帧与真实帧交接耗时、10000 次交接 |
| E02 | CUB 32-bit stable key/value sort，对照 FidelityFX | 相同排序任务 CUDA 能否抵消 E01 开销 | 随机/同键/逆序/剔除键，0/1/511/512/513/百万/800万/场景规模；独立与整帧时间 |
| E03 | CUDA 投影/SH，保留 D3D12 sort/draw | CUDA 预处理能否减少投影时间 | 椭圆/SH/键/候选数/图像；128/256线程、布局变体逐个对照，禁止一次混改 |
| E04 | CUDA 投影 + CUB，对照 E02/E03 | 扩大连续 CUDA 区域是否减少交接并改善整帧 | 实际交接次数、阶段与完整 GPU span；复用已验证逻辑 |
| E05 | stable compaction + 仅可见集排序，对照全数组 | 压缩成本能否被少排序抵消 | 0/1/10/50/100%可见率、保守椭圆裁剪、稳定原始索引；全可见时的退化 |
| E06 | 保持原排序的 CUDA tile 合成、关闭提前终止 | 减少几何/数据读取是否优于 quad | tile 8×8、16×16逐项测；列表构造、排序、合成、最终纹理交接及总显存 |
| E07 | 相同列表/顺序/公式的 HLSL tile 对照 | E06 的收益有多少来自算法，多少来自 CUDA | 算法/数据/精度/列表预算相同；报告 CUDA 与 HLSL 整帧，不只比较 kernel |
| E08 | CUDA tile + transmittance 提前终止，对照 E06 | 在限定误差下能否少算被遮挡贡献 | 关闭/1e-5/1e-4 阈值分列、实际处理贡献数与图像误差，未经验证不进默认 |
| E09 | stream/帧槽调度、再测 CUDA Graph | 小 kernel/交接是否受提交开销限制 | 调度与 Graph 分成两次改动；CPU submit、GPU span、排序年龄、输入延迟与尾部 |
| E10 | 各通过项的最小组合及桌面集成 | 用户浏览是否受益、长期是否稳定 | 正式性能协议、生命周期、测距回归、部署与无 CUDA 构建 |

### 4.1 排序与可见集

E02 先保持现有 32 位径向键及稳定同键原始索引顺序；视深度诊断模式单测另列。不能直接缩键宽、改为不稳定排序或静默改变相同深度次序。排序测试与 CPU `stable_sort` 比较 key/value 全数组，不能只检查键单调。

E05 不能以中心不在视锥为由丢弃可见椭圆。完整场景/可见候选/GPU绘制数量分开记录；按 GPU 计数驱动动态规模，不能为获得数量引入每帧同步读回。如果 CUB 当前 API 无法直接消费 device count，必须比较支持的 GPU 间接方案或有界分桶方案与完整规模排序；禁止把 CPU 等待隐藏在“压缩收益”里。2248万点的历史 fit 视角几乎全可见，不能预设此项有效。

E05实施决定（2026-10-17）：CUB的SortPairs数量参数是host值。对照两种不读取device count的实现：稳定DeviceSelect后仍排序完整容量；以及由GPU计数选择有界容量桶的conditional switch graph。桶从min(32768,N)按2倍增长至N，每个分支用固定host容量的CUB排序；GPU只执行选中分支，不逐帧CPU等待。所有桶先查询scratch取最大值，稳定gather及填充不改变有效前缀顺序，输出统一到固定buffer。Graph只承担device-count分派，E09仍须另测整管线capture的调度收益。Graph不支持时明确拒绝该变体，保留完整容量对照。新增selected-index与count/scratch容量计入预算，N变化重建workspace/graph，资源地址变化不得复用旧图。

E03调优决定（2026-10-17）：初版CUDA投影比HLSL慢，cuobjdump显示56寄存器且LOCAL/STACK为0，不能以未经验证的spill归因。保持参考数学路径，新增三项独立消融：向量除法公共倒数提取、仅归一化/Gram尺度采用显式FP32 rsqrtf、两者组合。公共倒数非有限时回退原逐分量除法；不启用全局fast-math、不改径向键计算。每个变体均须通过同一CPU/HLSL、极端值和原图像门槛，失败则撤回该变体，不调整阈值。

### 4.2 Tile 合成的等价性

- 先采用现有全局径向稳定排序所得 **global rank**，按 `(tile_id, global_rank)` 建立列表，保留同一个像素的完整覆盖顺序。先不改成 tile 局部视深度排序。
- 原管线从后到前绘制；从前到后合成必须反转完整顺序，**包含同键 tie 的顺序**。背景、alpha clamp、最小 alpha、椭圆支撑、SH、颜色空间与当前实现一致。
- E06 关闭提前终止，与 D3D12 比较；E08 才增加误差受控的 transmittance 阈值。浮点累加与硬件 blend 精度差异必须测量，不能用数学等价推定像素等价。
- tile-splat 引用数以受检 64 位前缀和估算，预算包含排序临时空间与输出纹理。超过 CUB/索引的实际计数限制、显存预算或容量时返回明确资源错误，不截断、不丢 splat。
- 大半径/全屏 splat 与密集重叠是必测失败压力场景；长 tile 列表按有界批次处理以避免单次 kernel 超长。不能通过修改 Windows TDR 注册表通过测试。
- 局部深度重排、半精度、截断键宽和 LoD 是另外的画质/算法变更，不计入本规格的等画质加速路径。

### 4.3 调度与缓存

E06–E08实测处置（2026-10-17）：CUDA/HLSL统一512项有界批次，后者受32KiB groupshared限制。conditional while在最小复现中不稳定，禁用于验收路径，保留诊断入口与失败日志。普通固定链Graph单独消融。两个中等真实模型未通过原最大像素误差门槛，相同列表HLSL也重复该误差；薄quad亚像素光栅化/插值精度调查见evidence/2026-10-17-hlsl-tile/quality-investigation.md。analytic tile及提前终止/Graph不进入产品候选，保持原质量阈值。

静态相机可复用投影/排序，动态比较必须按同一墙钟相机路径。E09 先保持当前相机结果语义；如尝试旧排序异步复用，必须独立实验，记录排序年龄上限、快速旋转图像和输入到呈现延迟，不能只用更高提交率证明胜出。Graph 与 resize、场景替换、变动列表容量之间的更新/重建条件显式测试。

## 5. 数据集与性能测量

E09实施约束（2026-10-17）：实验host提供1/3帧槽独立allocator/list、query/readback与CUDA events，仅在对应最终fence完成后复用。共享模型缓冲区/目标通过D3D队列顺序及CUDA fence形成逐帧依赖，每帧重新计算当前相机投影/排序，不复用旧排序。每阶段按帧号排空槽，固定图像比较最终完成帧。CPU submit不含槽回收等待；CPU wall包含队列驻留和稍后解析延迟，不能称为输入延迟。Graph只捕获CUB计算，保留图外交接，绑定stream/四buffer地址/count，变化则拒绝并要求重建。必须有D3D12三槽控制，避免将通用提交调优归因CUDA。

正式相机覆盖补充：fit之后复核pan-x=900的最大模型可见内容场景，先短时检查、再同样30s/60s/3轮配对。所有后端在全部计时和截图后读回最终DrawCounters，采集器保存visible/source；不向正常逐帧路径加入计数读回，不以全剔除帧作为可见内容收益证据。各次冻结运行时单独保存hash。

截图复核补充：默认fit和平移视角对该地形模型接近水平侧视，平移后只剩边缘窄带，不能据此宣称常规浏览加速。host增加固定yaw/pitch角度和dolly步数，通过现有CameraController施加；测试固定yaw=30°、pitch=-45°、dolly=2的俯斜视角，并记录非零alpha像素比例与截图。与动态orbit叠加时仍从同一初始相机按墙钟变化；不改renderer相机算法。先图像/诊断，通过收益筛选后才扩大正式矩阵。

### 5.1 主集合

| 样本（相对用户目录） | splat 数 | 用途 |
|---|---:|---|
| changjin_v1.ply | 170799 | 小任务/固定互操作开销 |
| shengyi_v1.ply | 804758 | 中等规模 |
| tumu_v1.ply | 1248730 | 百万级主场景 |
| zhihuizhimen.ply | 3914609 | 大场景 |
| jiulonghu_v1.ply | 22480361 | 显存/排序/吞吐压力 |
| spz/shengyi_v1.spz、spz/tumu_v1.spz | 以解码结果核对 | 同解码内容与格式入口回归 |

`models.json` 枚举了 40 个 PLY/SPZ 文件；主集合附 SHA-256，其余尚未全量解码/哈希。最终选定策略必须覆盖其余可支持模型，不支持/失败/超预算逐文件登记，不能静默跳过。PLY 与 SPZ 是否具有相同归一化属性须实际比较，不仅按文件名断言。

视角至少包括 fit、近景高重叠、侧视、穿越、快速旋转、平移和缩放；增加高裁剪率视角与合成的全可见/全剔除场景。相机矩阵、时间戳、路径文件和输出 hash 进入证据。

### 5.2 分开报告三个层次

1. **算子诊断**：CUDA events / D3D12 timestamps 测各阶段，说明是否包含准备、列表生成、同步和拷贝。不可直接相减不同 API 的未校准时钟。
2. **完整 GPU 帧**：在同一帧依赖链首尾用统一 D3D12 timestamp span 包住 CUDA 等待与交接，必要时用 Nsight Systems 校验。总帧不能由重叠阶段简单求和。
3. **用户呈现**：正式 WinUI/SDK host 的 CPU 提交、PresentMon 呈现节奏、最慢1%平均帧时间的倒数和输入延迟。受 DWM/刷新率限制则该项“不可判定”，不能由 offscreen GPU 时间推算显示 FPS。

正式测试每个场景/路径：**预热30秒，采样60秒，至少3轮**；CUDA/D3D12 交替 AB/BA 顺序，单 GPU 顺序运行。三轮不足以区分波动时提升至7轮，按配对轮次进行 bootstrap 95% CI；不能将同轮逐帧当成独立实验轮次。保存每轮中位帧时间、1% low、p95/p99、失败次数与原始帧数据。

固定 GPU/驱动/电源设置、物理像素、SH、点数、排序/支撑/alpha/blur/radius、颜色格式；记录温度、时钟、进程干扰、CPU/RAM/VRAM 峰值。不得擅自关闭用户其他程序。Nsight/PIX/调试层/compute-sanitizer 与正式计时分开。

本轮既有 SceneBench 仅能固定60帧预热、逐帧等待GPU，orbit按帧推进；用于 E00 阶段诊断，**不满足正式时间路径和呈现验收**。其SortBench首轮后重复排序已排序buffer；E02须为随机、已排序、逆序、同键分别定义相同输入及重置成本。现有 InteractionBench 也固定5秒+20秒，120Hz节奏；正式使用前需增加时间参数/无人工限帧模式，记录实际呈现是否受限。

## 6. 测试与稳定性

GoogleTest 是正确性框架；性能基准独立，不在普通单测中断言 FPS。拟新增 `CudaInterop`、`CudaSort`、`CudaProject`、`CudaTile`、`CudaLifecycle` 分组。

- **排序**：0/1、同键、原索引、NaN/无穷生成边界、UINT_MAX剔除、线程块边界、800万及真实2248万；完整 key/value 对照。
- **投影/图像**：SH0–3、薄各向异性、极端尺度/坐标、近面交叉、clip边缘、透明层叠、大/小alpha、全屏splat和空帧；独立 CPU oracle + 当前 D3D12 参考。
- **图像门槛（草案）**：合成夹具逐通道误差≤2/255；真实固定图像相对原生 D3D12 的 SSIM≥0.995、RMSE≤1/255、p99绝对误差≤2/255、最大误差≤8/255，归一化RGB和alpha分别检查。保存热图/局部图，明显缺块、闪烁或顺序错误直接失败。门槛不能在看见失败后静默放宽。
- **显存/错误**：分配失败、导入失败、LUID不匹配、前缀和/列表溢出、kernel launch/异步错误、不可完成fence超时、设备移除；注入测试与真实故障区分。
- **生命周期**：100次加载/替换/关闭、100次resize/最小化恢复、100次场景/相机变化时拾取取消；重复运行与测距状态不串场景。
- **长稳**：主大场景连续移动2小时，记录每分钟资源曲线和错误。预热结束后最后窗口相对首窗口的私有常驻分配漂移≤max(16MiB, 1%)；同时检查live allocations/HANDLE回到同场景稳态，WDDM外部用量变化另列。
- **工具**：compute-sanitizer 的 memcheck/racecheck/synccheck（当前工具/平台支持范围内）与D3D12 Debug Layer分开跑；不支持项明确未验证，不能以工具不可用推定无竞态。
- **回归**：CUDA OFF构建、现有完整CTest/VSTest、SDK安装消费、WinUI正常打开/取消/resize/测距/恢复。设备移除必须有注入证据，单凭正常退出不算通过。

## 7. 保留、撤回与发布门槛

实验完成与成功加速分开判断：E01–E10 每项必须有正确性、数值/资源、性能与结论；无法运行的路径写明阻塞及下一步，不填虚构数字。

1. **保留单项优化**：目标工作负载的完整GPU帧时间至少降低5%，且配对95% CI排除零收益；正确性/资源/生命周期均通过。只有算子快、整帧无收益的变体不作为默认实现保留。
2. **进入可选生产后端**：至少一个≥百万点主场景完整GPU帧时间降低15%；主集合其他场景中位帧时间回退不超过3%，1% low FPS回退不超过3%，且无新增错误。若不满足普适门槛，只能采用有明确、可测试选择条件的局部策略，不能自动全局开启。
3. **声明用户浏览更快**：必须通过正式未受呈现节流的可见host对照；offscreen胜出只能声明GPU帧加速。
4. **稳定性门槛**：第6节长稳、生命周期/故障恢复、无CUDA构建及SDK回归通过，资源峰值在同一准入预算内；不能靠提高超时或降质通过。
5. **中性/负收益**：保留台账及原始数据，撤回产品代码；有依赖价值的基础设施独立注明用途，不宣称其为性能优化。

实验台账字段：ID、输入/相机/质量hash、基线和候选提交/二进制hash、Toolkit/驱动、各轮数值与CI、GPU阶段/总帧/Present、显存峰值、图像指标、失败与原因、keep/revert/blocked。重试保留所有attempt；修改代码后重新配对，不挑最好一轮。

## 8. 目录与代码风格

```text
CudaOperators/
  README.md
  SPEC-cuda-operators.md         本规格，当前阶段产物
  evidence/<date>-<experiment>/  原始命令/日志/环境/摘要
  CMakeLists.txt                可选CUDA构建，默认OFF
  include/gs_cuda/              私有批量算子契约，不安装到公共SDK
  src/interop/                  共享内存/fence/帧槽RAII
  src/operators/                sort.cu、project.cu、compact.cu、tile.cu
  tests/                        独立正确性与生命周期测试
  bench/                        实验host与采集/分析工具
  tools/                       工具链/采集/分析脚本
  tasks/plan.md                 已授权实施计划
  tasks/todo.md                 实际验收状态
```

C++20、四空格、类型PascalCase、函数/变量snake_case；`.cu`采用受支持工具链的相同语言标准。每个kernel职责单一；显式stream、受检容量/乘法、结构化错误。布局用static_assert与CPU/HLSL/CUDA对照测试锁定，不复制三份容易漂移的数学常数。不启用全局fast-math；任何近似优化独立计量。

示意接口（非实现）：

```cpp
namespace gs::cuda_ops
{
struct SortRequest
{
    uint32_t count = 0;
    uint32_t layout_version = 1;
};

// Result 和 FrameContext 由私有资源契约定义；调用只提交工作，不等待整卡。
Result submit_sort(FrameContext &frame, const SortRequest &request);
}
```

## 9. 命令

从仓库根目录运行。当前已有、可运行的命令：

```powershell
cmake --build out/cmake --config Release --target Native3DGSViewer.SceneBench Native3DGSViewer.SortBench Native3DGSViewer.Tests --parallel
& .\out\Release\Native3DGSViewer.SceneBench.exe 'C:\Users\21544\Desktop\zhishan\zhihuizhimen.ply' force 120 3 1
& .\out\Release\Native3DGSViewer.SceneBench.exe 'C:\Users\21544\Desktop\zhishan\jiulonghu_v1.ply' cached 120 3 1
& .\out\Release\Native3DGSViewer.SortBench.exe 8000000 30
ctest --test-dir out/cmake -C Release --output-on-failure
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
cmake --install out/cmake --config Release --prefix out/sdk --component SDK
git diff --check
```

已实现的CUDA构建/采集命令（build.ps1显式设置已验证的本地13.3.0工具链，不依赖全局PATH）：

```powershell
pwsh -NoProfile -File CudaOperators/tools/build.ps1 -Configure -Targets Native3DGSViewer.CudaTests,Native3DGSViewer.CudaBench
ctest --test-dir out/cmake-cuda -C Release --output-on-failure -R '^Cuda'
python CudaOperators/tools/collect.py --models jiulonghu_v1.ply --backends d3d12-queued cuda-sort-queued --image-reference d3d12-queued --formal --output CudaOperators/evidence/new-formal-run
python CudaOperators/tools/analyze.py CudaOperators/evidence/new-formal-run
```

新增CUDA目标显式使用自己的runtime输出目录，不覆盖基线exe；集成阶段如需重建render-core，先保存基线host/依赖/shader及hash。每次正式采集验证运行二进制与当前实验提交对应。

## 10. 审阅后的拟实施工作包

本节记录工作包及依赖；已授权的Plan/Tasks在本目录tasks/，实际验收状态以todo.md为准。每项约5文件，过大继续拆分，执行顺序与E编号保持一致。

| 工作包 | 前置 | 可验证完成条件 | 预期文件范围 |
|---|---|---|---|
| W01 工具链探针 | 规格审阅 | 支持组合编译/运行、版本锁定、CUDA OFF可构建 | CMake、探针、环境说明（≤3） |
| W02 共享buffer切片 | W01 | 数据往返及LUID/容量/句柄失败测试通过 | interop头/源、测试、CMake（≤4） |
| W03 fence/帧槽切片 | W02 | 10000次交接、超时/关闭与noop计时 | 帧槽头/源、测试、bench（≤4） |
| W04 CUB排序切片 | W03 | 全key/value稳定性与E02原始性能 | sort.cu、私有头、测试、bench、台账（≤5） |
| W05 投影等价切片 | W03 | CPU/D3D椭圆、SH/键与图像通过 | project.cu、布局头、测试、host（≤4） |
| W06 投影调优与组合 | W04/W05 | E03/E04分别归因，交接和整帧报告 | 投影、bench、采集、台账（≤4） |
| W07 可见集切片 | W05 | device count方案明确，E05各可见率通过 | compact.cu、计数接口、测试、bench、台账（≤5） |
| W08 tile列表切片 | W04/W05 | 覆盖/稳定rank/溢出/预算通过 | 列表kernel、头、测试、bench（≤4） |
| W09 CUDA精确合成切片 | W08 | E06图像与完整纹理交接通过 | tile.cu、纹理interop、图像测试、bench（≤4） |
| W10 HLSL算法对照切片 | W09 | E07等算法与整帧对照通过 | HLSL、对照host、CMake、图像测试、台账（≤5） |
| W11 提前终止切片 | W09 | E08阈值误差/收益分别量化 | tile.cu、测试、bench、台账（≤4） |
| W12 调度消融 | W06/W09 | E09调度单独测，图像和尾部不过退 | 帧槽、host、生命周期测试、台账（≤4） |
| W13 CUDA Graph消融 | W12 | Graph单独测，resize/换场景重建正确 | graph源、测试、host、台账（≤4） |
| W14 正式基准采集 | 各候选 | 30s/60s/3轮、原始数据/图像/内存、全模型结果 | 采集、分析、相机、报告（≤4） |
| W15 renderer/SDK集成 | W14胜出 | 先改提供者规格，再做选择/统计/回归 | 若超过5文件，拆成契约与接入两个任务 |
| W16 长稳与发布检查 | W15 | 2小时/故障注入/测距/安装消费/CUDA OFF证据 | 测试、验证脚本、报告（≤4） |

检查点：W01–W03环境/互操作 → W04–W07预处理/排序 → W08–W11合成 → W12–W14正式收益 → W15–W16集成稳定性。每个检查点保留失败路径，不把未验收项勾成完成。

## 11. 操作边界与决定

- **Always**：先规格/后契约/再实现；逐路径消融；所有错误检查；共享资源按fence释放；保留D3D12；原始数据与失败台账；正确性通过后才声明性能。
- **Ask first**：正式规格批准；新的公共SDK行为；系统级Toolkit/宿主安装或卸载；改变排序/画质约定；本规格未授权的外部依赖/CI/发布方式；未通过指标时调整验收阈值。
- **Never**：降低SH/点数冒充等画质；静默丢tile列表；CPU逐帧排序/图像中转；改TDR；关闭失败测试；复制未知许可代码；把未测硬件、未测CUDA或受限呈现说成通过。

2026-10-17：用户已授权E00–E10范围与本地CUDA13.3工具链完善。采用 `CudaOperators/tasks/plan.md` 与 `CudaOperators/tasks/todo.md`，保留根目录既有任务。标准Program Files目录不可写，使用当前用户目录安装官方13.3.0开发组件，显式指定工具路径，不变更驱动或系统PATH。精确组件版本/hash/支持探针结果回填环境记录；不能以驱动支持版本代替工具链验证。

## 12. 参考资料

- [CUDA Windows 安装/宿主兼容性](https://docs.nvidia.com/cuda/cuda-installation-guide-microsoft-windows/)
- [CUDA Programming Guide](https://docs.nvidia.com/cuda/cuda-programming-guide/)
- [CUDA Runtime external memory](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__EXTRES__INTEROP.html)
- [CCCL/CUB](https://nvidia.github.io/cccl/cub/)
- [Compute Sanitizer](https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/)

以上是实施核对入口；本轮未用在线文档证明某一特定Toolkit/VS组合已兼容。E01须记录实际采用版本及对应文档依据。
