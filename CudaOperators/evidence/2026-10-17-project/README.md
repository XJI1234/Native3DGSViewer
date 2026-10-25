# E03/E04 投影实现验证

实现逐点CUDA投影和SH0–3，复用私有FrameConstants/ProjectedEllipse布局。布局定义移动到render-core/passes/layout.h，原生SplatPass行为保持一致；不改变公共SDK。128/256线程块均支持，不启用fast-math。容量、对齐、SH布局、输出别名检查在提交前完成；输出draw counters在同一stream清零和累加。

RED：test-red.log记录未实现时3项失败。实现后解析513点/SH0–3/两种线程块、非法数值/普通剔除分流、容量、随机各向异性/旋转/SH/裁剪对HLSL、空帧、近面交叉/薄椭圆/零距离/溢出共6项通过。HLSL对照允许浮点键最多2ULP，椭圆各分量误差≤0.002px，颜色≤1e-5；真实图像仍必须通过规格的独立全图像门槛。

完整CTest 9/9通过（103.17s，ctest-integration.log）：原生模型IO、render-core、联合、engine、安装SDK消费，以及CUDA工具链/互操作/排序/投影。该轮包含前5项投影测试；新增极端测试单独通过test-edge.log。Compute Sanitizer memcheck运行全部6项，0 errors（memcheck.log）。racecheck/synccheck与长期设备故障尚待验证。

首次真实小模型的两种256线程变体已通过图像门槛，见相邻project-smoke-attempt1。图像非逐字节一致，保持原验收阈值。所有正式质量、主集合性能、显存准入及稳定性工作继续执行，不以本次单元测试代表后端验收。

实验host在非计时阶段释放原输入buffer，并从规范化CPU只读span分块上传共享buffer，避免在10GB GPU上复制整份4GB SH而产生临时双份显存。每帧只传常量，输出、排序与draw通过共享GPU资源交接，没有每帧CPU图像/排序中转。E04的projection_ms目前包含投影+排序+互操作；后续增加CUDA events进行kernel归因。
