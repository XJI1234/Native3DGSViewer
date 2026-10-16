# CUDA 实验质量审查

审查基准：f1c86cb，另含已提交的db2be3a固定绑定CUB Graph切片。采用code-review-and-quality的正确性、可读性、架构、边界校验和性能五轴检查；这是本轮单代理自审，不冒充独立第三方审查。正式计时期间只做静态审查，不构建、不启动其他GPU程序。

## 阻止进入产品的发现

| 严重性 | 发现与影响 | 处理/验收位置 |
|---|---|---|
| Required | analytic tile不能复现薄quad的硬件亚像素光栅化/插值；真实最大误差超规格。普通CPU数学oracle通过不能代替quad图像通过。 | E06–E08全部拒绝产品集成；质量调查和原始失败图像保留。W09–W11不勾选。 |
| Required | conditional while最小复现仍有间歇非法读取/error999与清理卡住，准确归属未证明。 | 验收路径改为普通有界kernel/普通Graph；conditional选项只作诊断，probe不进入CTest。不得默认启用。 |
| Required | 算子仅部分核对stream，未完整核对每个输入buffer/texture的Context归属；互操作begin/end未绑定同一队列。错误调用可能绕过真正的数据依赖。 | W03/检查点A：增加统一归属与队列一致性检查，并覆盖跨Context/跨queue负测后才能接产品。当前host单Context/单队列调用，已验证路径没有依赖这些无效调用。 |
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
