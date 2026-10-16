# 基准计时与准入冒烟验证

小模型5后端/单轮仅验证程序路径，不作为性能筛选。所有图像门槛通过，见analysis.json。新增CUDA events可分别测投影/排序，同API计时，不与D3D时间戳直接相减；events默认关闭，正式采集禁止同时开启。

显存准入复用原生fits_scene_budgets的80%可用预算规则；CUB scratch先计入CurrentUsage，场景包括全点/SH、投影、双buffer排序/FFX scratch；额外预留32MiB覆盖目标/共享heap对齐，upload/readback计入非本地预算。替换共享排序buffer前释放旧buffer，避免非必要双份峰值。不支持场景或预算不足明确拒绝，不降质。

每次运行的memory.csv记录初始化、上传、共享替换、逐秒采样及截图后的DXGI local/nonlocal usage/budget、进程private/working set及系统维护的峰值。DXGI最大值是**采样峰值**，未声称覆盖每个临时分配瞬间；辅助解码进程的峰值和外部GPU干扰仍需正式采集补充。

当前E04的projection_ms表示整个CUDA区域及交接；纯kernel列只在--cuda-events on时有效，否则为-1。正式完整GPU总span不受此列命名影响。短时数据中投影128/256的排序变化与时钟/提交噪声不能据此作结论，后续配对实验继续验证。
