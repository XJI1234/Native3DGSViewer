# 生命周期与故障注入补充

生产配置在独立进程中重复100次Context/共享buffer/共享纹理创建、写入、双API交接、完成和释放，通过句柄数≤初始值+4的原门槛；注入D3D12队列未完成fence后，10ms CUDA stream轮询返回超时，解除人工阻塞后写入值123和后续交接恢复正确。见lifecycle-isolated.log与handle-release-control.log。它们不代替100次GUI场景/resize/取消或设备移除测试。

保留的失败与诊断：

- 首次注入在首次kernel模块加载前阻塞了stream，主机端提交未返回；手动结束该测试进程，原输出在lifecycle-stalled-attempt1.log。测试现在先在同一上下文预热kernel，再注入可控fence停顿。该测试只能证明已初始化stream的有界轮询；不能承诺所有CUDA API在任意驱动故障下有界返回。
- 启用D3D12 Debug Layer的进程中，交接后退役fence会留下约1个Event/Context，100次句柄596→696，失败见compact目录test-compact-lifecycle-attempt1.log。临时分类诊断已归档handle-diagnostic.log、handle-debug.log、handle-native-control.log、handle-types.log。只创建/释放Context、buffer或texture无持续增长；未启用调试层时，交接也不增长；启用调试层后CUDA fence交接增长Event，普通原生队列fence对照不增长。PssCaptureSnapshot官方API定位到Event类型，未擅自关闭未拥有的句柄。
- 这是已定位到调试层/CUDA交互条件的**未解决问题**；尚未证明具体由Windows还是NVIDIA实现持有。不能把生产配置通过写成调试配置通过，也不能据此宣称完整稳定性验收。

Debug Layer开关是进程全局状态，先前测试启用后会影响后来创建的device。因此生产资源计数测试放到独立进程，调试故障注入另一个进程；未放宽句柄门槛。CTest的CudaLifecycleHandles/CudaLifecycle分别执行，避免配置串扰。现有Debug Layer图像和共享资源正确性测试继续运行。
