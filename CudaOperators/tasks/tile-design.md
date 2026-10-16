# W08–W11 实现细化（待验证）

这是已批准E06–E08的内部实现设计，不是测试完成记录。先完成当前投影/压缩切片，再按以下顺序执行；不启动额外代理。

## W08a：稳定tile列表

- 输入沿用原生40字节ProjectedEllipse、全局稳定排序的uint32原索引与GPU DrawCounters。先保留D3D12投影/SH，在一个CUDA区域执行CUB全局排序、列表和合成，以隔离投影参考实现的性能问题，也避免为tile单独导入整份SH。
- 每个global rank计算椭圆四边形的保守屏幕bbox，clamp到viewport后再转换tile坐标；不按中心裁剪。8×8/16×16都测试。不可见/alpha0的项引用数为0，索引/非有限布局错误置全帧错误标志。
- 每点引用数和前缀均用uint64，CUB ExclusiveSum；总数为最后prefix+count。用checked uint64估算N×最大tile数及容量/字节数，任何超预算/超实际索引范围均拒绝，不截断。
- 每个引用使用uint64 `(tile_id << 32) | global_rank`，完整64位CUB SortKeys；排序后低32位直接定位全局排序索引，再取Ellipse。相同深度的tie由global rank完整保留。
- 首版分配固定、受预算约束的引用容量，填充尾部UINT64_MAX，无逐帧CPU count读回；实际数量大于容量则标记错误，后续合成清空且host报告失败。后续再单独测GPU容量桶，避免首版混合优化。
- 排序后建立每tile的uint32 begin/end，并保存uint64总引用数、最大tile长度及错误flags。引用容量不超过UINT32_MAX，必要时更严格遵守CUB实际限制。
- 预算包含两份64位key、两份64位count/prefix、CUB scratch、ranges/status、共享分配对齐；渲染阶段另计像素状态/输出纹理。构造前查询scratch并检查预算，不能先分配再发现超预算。
- CPU oracle独立枚举tile/rank，核对全部有效keys和ranges；覆盖空帧、边界、全屏、同键顺序、超容量、非法数据及预算拒绝。先失败测试再实现。

## W08b：列表整帧诊断

增加cuda-tile-list-8/16：保留原生draw，额外执行列表生成，仅验证交接/列表统计与预算，不宣称加速。按已有固定输入采集真实引用数和最大长度，读回只在全部计时结束后；之后选择可行容量。默认容量比例只是实验参数，失败如实记录，不偷偷扩大原生80%准入预算。

## W09：CUDA合成

1. 先写小夹具的后到前合成，不启用提前终止。每像素中心(x+.5,y+.5)，按ellipse正交轴求局部uv，gaussian坐标=uv×stddev，与原pixel shader相同支撑、exp、alpha clamp/cutoff和预乘颜色。输入为全局径向顺序。
2. 比较FP32累加和逐splat显式UNORM8舍入两种精度变体。原生R8目标每次blend会舍入，不能假定最终一次量化必然等价。真实图像仍沿用原阈值，失败不放宽。
3. 每线程一个像素；tile线程协作装载有限批次Ellipse；屏幕边缘inactive线程仍参加block同步。保留完整global rank，前到后时反转完整序列（含相同key tie）。
4. 长列表按固定上限（CUDA/HLSL统一512项/批）分kernel，禁止一个不可控超长循环。最初计划的conditional while目前不稳定，不用于验收路径。像素累加状态跨批次保存在FP32 buffer，结束后写共享RGBA8 surface，空帧/错误清空。
5. 原生目标为BGRA8，不能直接CopyResource自RGBA8。用D3D12 fullscreen blit采样共享RGBA8并写BGRA8，包含在完整GPU依赖span；CPU截图转换不计入渲染路径。

## W10：HLSL同算法对照

2026-10-17实现修订：conditional while在最小复现中也出现Compute Sanitizer error999/非法读取，EAGER/显式预加载/内存padding均不能稳定解决。保留原始attempt与probe，暂不接受该调度。E06先使用每tile最多N条引用的静态上界，host展开ceil(N/512)个有界kernel、GPU空块直接退出；无CPU计数读回、不截断、完整计入提交成本。普通Graph捕获单独消融。该保守路线可能很慢，不能因此省略成本或放宽门槛。

复用同一CUDA生成的列表/全局顺序/数据，HLSL仅替换tile合成，使用同样批大小/精度/支撑/公式；不把算法改善全部算为CUDA收益。每chunk由GPU填充间接Dispatch及0/1 count，host按有界容量录制ExecuteIndirect和chunk间UAV barrier，不每帧读回长度。D3D12间接命令内部不能插UAV barrier，因此不能直接一次多Dispatch间接执行后省掉必要的跨chunk同步。报告这项提交成本及纯kernel时间，必要时按相同列表长度子集比较。

## W11：提前终止

先独立验证前到后、阈值关闭；再测T<1e-5/1e-4。每tile同步判断所有像素完成后跳过后续批次，global device条件只在仍有tile待处理时继续；阈值、贡献数、误差热图和完整GPU时间逐项保存。背到前结果、前到后无阈值结果和提前终止结果分别记账。

## 仍需实际数据决定

真实引用放大倍数和最大tile长度、预算内的容量、UNORM误差、前到后无阈值精度、HLSL间接命令开销，都必须先测。大场景或近景超预算时明确失败并保留D3D12；不降点数/SH/分辨率、不改TDR、不丢列表。GPU shared映射的DXGI重复计费与物理增量分开记录，生产准入政策待E10前解决。

## 2026-10-17 实测决定

- HLSL 32KiB groupshared上限使1024×40字节批次不可编译；两侧统一512项、20KiB，原编译失败日志保留。
- 同列表HLSL与CUDA逐splat UNORM图像最大相差1/255、alpha相同，但shengyi/tumu相对quad最大误差超过原门槛。薄quad亚像素光栅化/插值精度调查见hlsl-tile/quality-investigation.md，尚未完全模拟硬件。
- analytic tile所有变体均未通过等画质验收；提前终止与普通Graph只是诊断，不能成为产品候选。不为失败方案扩大正式矩阵。贡献数统计未完成，W11不能勾选完整。
- 普通Graph捕获固定有界kernel链；输入内容可变、地址/尺寸/调度不可变，变化则重建workspace。参数在图外同stream更新，纹理store在图后同stream。
- 1301层独立double oracle、33×19边缘、8/16、full/empty/full、前后顺序、量化与Graph通过；3次重复memcheck零错误。它们不能替代真实quad图像门槛。
