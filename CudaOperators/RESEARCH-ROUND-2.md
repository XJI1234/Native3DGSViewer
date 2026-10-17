# CUDA 第二轮研究与实测（2026-10-17）

分支：`codex/windows-cuda-operators`。本轮有可复现的算子与加载阶段改善，
**没有新增同时通过画质、性能和资源门槛的生产后端**。CUDA仍默认OFF；SDK、
WinUI和产品renderer没有新增CUDA接口。上一轮七对正式结果14.77%不被本轮短测替代。

## 实验范围

- CUDA13.3.0、CCCL300303、VS2026/MSVC19.50、SM86、RTX3080 10GB、driver617.42。
- 模型来自`C:/Users/21544/Desktop/zhishan`，沿用五个主模型及SHA-256。
  完整点数/SH、1920×1080、32位径向key和同键稳定顺序不变。
- 固定相机yaw30 / pitch−45 / dolly2；主矩阵5模型×9后端×3轮，135次、16,200采样帧。
  另做5模型×5后端×1轮资源筛查，25次、3,000采样帧。
- 每次60帧预热/120帧采样，CUDA events仅作诊断；runtime、shader、模型和采集器hash保留。
  GPU实验顺序执行。这里没有新增30秒/60秒正式胜出结果、显示FPS或输入延迟结论。

## 路径与结论

| 路径 | 实测 | 决定 |
|---|---|---|
| R02 CUB int32 offset | 百万随机kernel约快8.05%；800万和2248万随机约慢1.37%/0.69%。最大真实整帧仅约0.15%变化。 | 保留私有实验选项，默认Int64；不宣布普遍收益。 |
| R03 warp计数 | 最大模型投影kernel约慢0.64%，组合整帧变化约−0.13%，未显示明确收益。 | 保持默认scalar。 |
| R04 SH plane-major | 最大模型投影kernel中位数13.327872→11.211776ms，配对均值降低15.84%；投影+CUB整帧32.459776→30.411264ms，降低6.41%。 | 相对原CUDA投影有收益；图像与原CUDA版本byte一致，但跨D3D12画质及资源门槛失败。 |
| R06 CUDA自有输入 | 最大模型DXGI峰值13,364,547,584→8,056,659,968字节；五模型图像与shared-plane全部byte一致。 | 解决大量输入共享映射计费，仍占预算82.75%，超过80%门槛。 |
| R01 退役 | 原生tail、完成后CPU确认、分离方向fence、诊断层SharedFenceSignal通知均未消除Event增长。 | 失败补丁撤回；不改变句柄阈值、不强关未知句柄。 |
| R05 外部worker故障隔离 | 10次阻塞至少1秒的worker均在64.98–77.85ms内完成外部终止；父进程句柄137→137。 | 注入故障的隔离实验通过；尚未接入产品，不代表任意驱动故障有界。 |
| R07 排序误差根因 | 完整参考排序oracle使shengyi最大误差11→6/255，jiulonghu 21→5/255；两者完整图像门槛通过。 | 证明排序键/顺序差异导致原超标；oracle有CPU读回，不能当加速实现。 |

同帧槽消融详见[ABLATIONS](evidence/2026-10-17-round2-diagnostic/ABLATIONS.md)。
相对D3D12的[总表](evidence/2026-10-17-round2-diagnostic/SUMMARY.md)同时包含1/3槽，
不能把调度差别归因于单个kernel。三轮短诊断的置信区间不替代正式时长验收。

### 资源与加载

R06仅将CUDA读取的attributes/SH改成`OwnedBuffer`，投影输出和排序交接仍用共享资源。
两个输入重载共用同一kernel和校验；Context归属、别名、容量及无效枚举在提交前检查。
上传在Context的同一nonblocking stream上完成后才返回，避免默认stream的pageable
H2D复制尚未完成时就消费数据。转换使用最多64MiB显式暂存，不创建完整GPU副本。

最大模型预算9,736,028,160字节，80%上限7,788,822,528，当前仍超267,837,440字节。
共享排序中间量仍可能重复计费，需另做所有权与准入设计，不能直接扣掉DXGI计费。
单轮SH转换/上传段10.07→1.77秒；这不包含先前原生上传、解码等阶段，不能当完整加载提速。
一轮资源筛查没有置信区间，也没有扩大成正式性能测试。

### 图像误差的因果证据

新俯斜视角中，shengyi和jiulonghu各有2个像素超过原8/255上限。warp/plane和排序宽度
没有造成新的投影图像变化；原CUDA投影与HLSL之间已经存在key位差异。
shengyi误差像素附近的933个候选完全相同，但112个key不同、共同候选的相对次序不同；
椭圆中心差约1.2e−4像素，颜色分量差约1.8e−7。只恢复该局部次序，目标像素11→0/255。

随后保留所有CUDA椭圆/颜色，只使用完整D3D12排序排列重新绘制：

| 模型 | 排序位置不同数 | 原RGB最大误差 | 完整排序oracle最大误差 | oracle RGB SSIM |
|---|---:|---:|---:|---:|
| shengyi（804,758点） | 21,510 | 11/255 | 6/255 | 0.999999629 |
| jiulonghu（22,480,361点） | 7,168,514 | 21/255 | 5/255 | 0.999999137 |

两者alpha/RMSE/p99及最大误差均通过原门槛。排序oracle是测量后的CPU诊断，输出另存，
没有改写原性能图像或纳入后端。后续GPU实现应验证key生成的一致性；本轮没有证明具体
是哪条编译指令或浮点规约产生位差，也没有通过缩窄key或增加容差掩盖问题。
详见[像素与完整排序证据](evidence/2026-10-17-projection-pixel/README.md)。

### 退役与故障

独立进程分级探针中，native fence、Context构造、原始semaphore导入/销毁、D3D signal/
CUDA wait均未增长；CUDA signal/D3D queue Wait的roundtrip重现Event增长。可选SharingContract
在真实GPU完成后通知仍失败，未进入后端。具体持有模块尚未证明。显式Debug Layer
100次生命周期RED继续保留，未混写为绿色CTest结果。

外部监督器不调用CUDA，通过终止并等待独立worker避开其CUDA/COM析构。本次只注入了
不可完成fence；尚未证明任意驱动故障、自动场景恢复或GPU资源回收曲线。
详见[退役尝试、官方资料和故障注入](evidence/2026-10-17-retirement/README.md)。

## 工具链和验证

修复了本地Ninja对MSVC `/showIncludes`前缀识别失败导致的头文件漏编译。
VSLANG=1033单独无效；工具现在先生成CMake规则，再用实际编译输出的原始字节校准当前
构建目录。首次校准清理旧无依赖对象；不改系统locale或安装语言包。
ownership对象实际依赖从0恢复到30，包含sort.h/project.h/interop.h，修改头文件会重编消费者；
最终根测试对象也验证有24项依赖。证据在[排序/构建目录](evidence/2026-10-17-sort-i32/README.md)。

- CUDA CTest最新8/8通过，包含Owned输入100次生命周期及新排序/投影测试。
- 最终全项目CTest13/13通过（158.34秒），含ModelIo、RenderCore、集成、Engine与installed SDK；
  见`evidence/2026-10-17-owned-inputs/ctest-full.log`。不包含下述显式Debug Layer RED。
- int32排序两项memcheck零错误；warp/plane/Owned/Int32Graph四项memcheck零错误。
- warp synccheck零错误；完整参考排列的重复index负测被明确拒绝。
- SH0–3、0/边界/tail、混合剔除/拒绝、128/256线程、完整keys/indexes/ellipse/counter对照通过。
- 显式Debug Layer句柄测试仍失败；产品GPU key一致性、80%预算、故障清理、GUI呈现/设备移除、
  两小时长稳尚未通过。原生SDK和产品开关保持不变。

最终Debug Layer单独复核仍605→701，见`retirement/final-debug-red.log`（路径在evidence下）。
最终代码切片提交为`c29145d`，源码hash在`owned-inputs/final-source.json`；这不重标早期冻结runtime。

官方来源与实施约束见[研究计划](tasks/research-round-2.md)，原始URL/下载hash含失败404保留于
[sources.json](evidence/2026-10-17-retirement/sources.json)。本轮不批准产品启用CUDA。
