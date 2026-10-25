# CUDA 实验质量审查

审查基准：f1c86cb，另含已提交的db2be3a固定绑定CUB Graph切片。采用code-review-and-quality的正确性、可读性、架构、边界校验和性能五轴检查；这是本轮单代理自审，不冒充独立第三方审查。正式计时期间只做静态审查，不构建、不启动其他GPU程序。

最终增量审查覆盖78bd75e之后的七轮证据、扩轮合并工具及Graph零count修复；仍为单代理自审。最终性能14.77% [14.65,14.94]，不通过15%生产门槛。合并核对完整配对、全部输入/参数/runtime/collector hash与来源，拒绝重复组和原始run；未将复制证据计为新增轮次。

## 阻止进入产品的发现

| 严重性 | 发现与影响 | 处理/验收位置 |
|---|---|---|
| Required | analytic tile不能复现薄quad的硬件亚像素光栅化/插值；真实最大误差超规格。普通CPU数学oracle通过不能代替quad图像通过。 | E06–E08全部拒绝产品集成；质量调查和原始失败图像保留。W09–W11不勾选。 |
| Required | conditional while最小复现仍有间歇非法读取/error999与清理卡住，准确归属未证明。 | 验收路径改为普通有界kernel/普通Graph；conditional选项只作诊断，probe不进入CTest。不得默认启用。 |
| Resolved | 算子原先未完整核对输入Context，begin/end也未绑定队列；RED测试实际将另一Context的哨兵0x12345678写成99。 | 已增加实际device LUID、单device/单queue绑定和所有算子输入归属检查；3项负测、完整Cuda CTest8/8、memcheck通过，见ownership/。包含零count拒绝、组合列表构造、不同device及错误调用后正确结束交接。 |
| Resolved | CUB Graph已捕获非零count后，0被早期no-op分支接受，绕过固定count契约。 | RED复现；显式拒绝已捕获Graph的0，未捕获零容量继续no-op。Graph两项、普通边界及memcheck通过，见graph-zero/；不改变普通正规模GPU算法。 |
| Required | shared import完成前释放、算子scratch析构中的cudaFree，在驱动故障下没有完整有界退出保证。当前共享资源超时保留在途导入，不能宣称无泄漏/可恢复。 | W03/W16：明确在途资源退役所有权，故障隔离/设备移除与不可完成fence逐条注入。正常生命周期测试不能替代此项。 |
| Required | Debug Layer下CUDA交接退役fence约每Context遗留1 Event；非调试100次正常生命周期通过。 | 已有最小分类对照与日志，具体持有方未定；保留原句柄阈值。W03未通过完整稳定性。 |
| Required | CUDA/DXGI共享映射计费与物理增量不同，现有原生准入估计不足以证明映射后80%预算合规。 | W14/W15前先锁定唯一分配统计与WDDM预算策略，不以全局nvidia-smi峰值替代独占显存。 |

## 已检查的实现性质

- CUB只处理原32位径向key/value，CPU stable_sort全数组验证到22480361项。Graph只捕获计算，图外交接不隐藏；绑定地址/count/stream变化明确拒绝。变动输入多次重放和无效绑定测试通过memcheck。
- Tile采用受检64位count/prefix与预算准入；超容量拒绝整帧，没有悄悄截断。全局rank保留同键稳定次序，前到后反转完整序列。512项批次限制单kernel工作，不修改TDR。
- 帧槽各自拥有allocator/list/query/readback/events；只有完成最终fence才复用。根常量由命令列表录制时拷贝，每帧相机不依赖可覆写的单个上传常量buffer。各阶段排空未满环，24个Debug Layer边界进程与90个诊断最终图像通过。
- 实验依赖仍封闭在CudaOperators私有DLL/host；根开关默认OFF，无CUDA类型进入公共SDK，model-io不依赖D3D12/CUDA。没有新增网络/运行时服务或修改vendor源码。
- CPU/GPU时间分别报告，排队CPU wall明确包含驻留和延迟解析，不能用它声明输入延迟。普通Graph大场景增益不超过噪声，正式候选采用更简单的普通CUB。

## 可维护性决定

实验入口集中在独立host；tile、投影、压缩、排序各自有头/实现和行为测试，失败方法保留为复现资料。当前host后端名称分派适合实验矩阵，不是SDK后端选择接口；若未来达生产门槛，应以提供者规格中的显式策略替换host字符串组合，不能把整个实验分派复制进renderer。

本轮允许保留实验基础设施与证据，**不批准产品合并/启用CUDA后端**。正式性能、相机覆盖、资源/故障、GUI呈现与2小时长稳均独立验收；未验证项留在todo，不以绿色单测替代。

后续审查：普通有界tile的racecheck零hazards、synccheck零错误（ownership目录）；前者仅检查CUDA共享内存，不证明全部全局内存/跨API竞态不存在。正式pan视角收益16.70%，但非零alpha面积仅1.42%；不得把该窄带视角等同常规浏览。已增加完整校园俯斜视角与画面面积统计。归属修复后GPU数学与排序方式未改，正式测量仍按各组冻结二进制识别，不能混写为同一版本。

最终生命周期复核：随机种子1将Debug Layer测试排在100次句柄测试之前，同进程592→692再次失败；独立普通配置仍通过。两份结果同时保留于lifecycle-recheck/。没有放宽阈值、冒称修复或将该失败混写为绿色CTest。

## 第二轮增量自审（2026-10-17）

范围：R01–R07，见[第二轮报告](../RESEARCH-ROUND-2.md)。仍是单代理自审，不是独立审查。

- 修复：Ninja对本机MSVC include前缀识别失败，导致头文件变动遗漏重编。
  raw-byte校准在CMake生成之后执行；首次清理历史无依赖对象。实际ownership依赖30项、
  根measurement测试依赖24项；不以编译成功代替增量依赖验证。
- 正确性：Int32只改变CUB公开count类型，仍完整32位keys/values、稳定tie；超过INT32_MAX及
  无效enum在分配前拒绝。Graph重放变动输入新增Int32覆盖。
- 正确性：warp/plane选项默认保持原值。Owned/Shared输入重载共用校验与kernel；输入/输出
  Context、别名、容量均校验。Owned上传明确同stream复制并同步，不依赖pageable cudaMemcpy
  的返回时机或默认stream为nonblocking stream建立隐式依赖。
- 架构：CUDA-only输入只存在于私有DLL/实验host；draw的未使用原属性SRV绑定有效4字节占位，
  HLSL vertex只读取projected/indexes。未把该占位资源用于HLSL投影。无SDK/WinUI/vendor更改。
- 资源：Owned显著降低重复映射计费，但最大模型8,056,659,968字节占预算82.75%，不通过80%。
  不以物理别名解释直接扣减DXGI统计，不对单轮时间编造CI。SH暂存64MiB只约束显式应用分配。
- 图像：完整俯斜视角暴露CUDA key/排序差异。两个模型的全排序oracle在保留CUDA椭圆时通过
  原门槛，证明排序影响；oracle读取CPU排列，只在计时结束后执行、输出单独图像，不能成为
  胜出后端。CSV候选集合校验、完整u32长度/唯一排列校验、文件上限限制均保留。
- 故障：SharingContract通知失败，不进入后端。外部监督器只验证自己创建的独立进程，
  不操作其他用户进程；确认阻塞后terminate并等待process handle。十次65–78ms与父进程
  句柄不增长只证明本次注入，不宣称任意驱动故障、正常cudaFree有界或产品可恢复。
- 保持阻塞：Debug Layer 100次RED仍失败；正常8组CUDA CTest不能覆盖或掩盖它。
  原15%性能、≤3%回退、80%资源、画质、设备移除与长稳门槛均未放宽。

最终全项目CTest13/13通过（158.34秒，含installed SDK），不包含上述单列Debug Layer RED。
只保留可复现的实验选项、诊断工具和失败证据；继续拒绝生产启用。主矩阵135次和资源筛查
25次分别冻结runtime，不能混写成同一源码版本的正式性能扩轮。
