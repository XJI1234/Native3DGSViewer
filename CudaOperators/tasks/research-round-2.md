# 第二轮：资料核验、优化假设与故障根因

2026-10-17用户授权深度调研并继续优化/修复，沿用当前分支、默认OFF与原质量/性能门槛；不再请求规格批准。单代理、GPU顺序运行。原七轮结果与失败日志保持不变。

## 官方来源与可验证结论

1. [CUDA13.3外部资源API](https://docs.nvidia.com/cuda/archive/13.3.0/cuda-runtime-api/group__CUDART__EXTRES__INTEROP.html)：销毁external semaphore前，"Any outstanding signals or waits must have completed"。CUDA stream完成不能自动证明随后D3D12 queue Wait的退役已完成。
2. [NVIDIA simpleD3D12](https://github.com/NVIDIA/cuda-samples/blob/master/cpp/5_Domain_Specific/simpleD3D12/simpleD3D12.cpp)：OnDestroy先WaitForGpu，再销毁CUDA导入；WaitForGpu在原生queue上Signal共享fence并等待。这里只借鉴有序退役，**不复制示例的无限等待**。官方示例不是本机Debug Layer问题归属的证明。
3. [Microsoft fence timeline](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization)：建议一个fence对应一个timeline，多个signaler可导致rewind竞态。当前交接有逐帧依赖，新的退役Signal必须在CUDA stream已完成后、单提交线程上递增；不得CPU重设旧value来伪造完成。
4. [CUDA13.3同步行为](https://docs.nvidia.com/cuda/archive/13.3.0/cuda-runtime-api/api-sync-behavior.html)："Any CUDA API call may block or synchronize"。单纯把cudaFree换成Async、提高超时或在析构里轮询不能证明驱动故障时有界退出；最终强故障隔离可能需要独立worker进程。先修可证明的正常退役，再另列故障契约。
5. [CUB公开SortPairs文档](https://nvidia.github.io/cccl/unstable/cub/api/structcub_1_1DeviceRadixSort.html)为当前在线版，仅作API说明；实际实现以本地CCCL300303的`device_radix_sort.cuh`/`choose_offset.cuh`/`tuning_radix_sort.cuh`为准。NumItemsT决定OffsetT；Policy800的32位key/value Onesweep在64位OffsetT时17 items/thread，32位时21。本机所有主模型count≤INT32_MAX，可以用公开API的int32 count独立验证，不触碰内部policy或缩窄排序key。
6. [NVIDIA warp聚合原子说明](https://developer.nvidia.com/blog/cuda-pro-tip-optimized-filtering-warp-aggregated-atomics/)：可将同一warp计数合并，且NVCC已经会自动处理许多情况。因此手写聚合**未必胜出**，必须和当前编译器参考逐项比较；可见/拒绝计数应逐值相等，不能丢弃draw counters。
7. [CUDA13.3最佳实践：coalesced access](https://docs.nvidia.com/cuda/archive/13.3.0/cuda-c-best-practices-guide/index.html#coalesced-access-to-global-memory)：SH AoS跨线程stride较大；plane-major SH可作为独立布局消融，保持系数、逐点运算顺序及原SH阶数，记录一次性转换/常驻开销，不能宣称单凭布局已加速。

网页仅作技术资料，未执行其中外部命令；原始下载暂存在忽略的out/cuda-research。早期CUB/样例旧路径404，已按官方站点跳转和GitHub tree定位当前路径，不使用404页面作证据。

补充调查：[Microsoft ID3D12SharingContract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/nn-d3d12sdklayers-id3d12sharingcontract)可由command queue QueryInterface取得，但无诊断工具时可能不支持，不能依赖其存在。[SharedFenceSignal](https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/nf-d3d12sdklayers-id3d12sharingcontract-sharedfencesignal)在D3D层与诊断工具间通知共享fence。新增原始API分级探针（native/import/wait/roundtrip）及**实际GPU完成后**的可选通知对照；不得拿诊断通知提前完成CUDA依赖，尚无修复结论。

[TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)是异步终止，必须另等process handle，未完成I/O也可能影响终止。因此R05监督器单列1秒确认阻塞/5秒终止观察，不泛化为任意驱动故障的硬保证；独立进程具有私有GPU资源，无宿主CUDA析构依赖。

## 执行切片与停止条件

| 顺序 | 假设/改动 | 正确性与测量 |
|---|---|---|
| R01 | 在CUDA完成后给绑定D3D queue补充单调原生退役Signal，再有界等该fence；调查Debug Layer Event滞留 | 新增显式Debug Layer100次生命周期RED；先只改Context退役，原阈值before+4不变。普通/调试、队列停顿、10000交接复核；失败保留。 |
| R02 | CUB32位offset替代64位offset，key/value仍完整uint32稳定排序 | 可选workspace参数；超INT32_MAX明确拒绝，默认保持64。所有分布/同键/0/边界/2248万CPU对照；1M/8M/实际规模微基准与5模型整帧三轮。通过筛选后才30s/60s正式。 |
| R03 | 投影visible/rejected计数手工warp聚合，减少单地址原子争用 | 先比较大量分歧/多种可见率和全计数、椭圆/key/index；单项后端和排序组合分别对照，不同时改数学/布局。 |
| R04 | SH改plane-major布局，先做纯布局消融 | SH0–3、尾部、空SH、CPU/HLSL图像，记录转换耗时/预算；失败则撤回候选，保留实验对照。 |
| R05 | 资源故障退役 | 明确stream、external fence、D3D consumer及scratch所有权和终止能力；已知不可完成fence先用可解除注入，在单独进程证明deadline行为。不能以正常释放通过宣称任意CUDA API可中断。 |
| R06（R04后） | 只供CUDA读取的attributes/SH改为CUDA自有分配；D3D12只共享outputs | R04最大模型实测共享映射DXGI计费13,360,418,816>9,736,028,160预算，先拒绝。新增私有OwnedBuffer，保持Context归属/容量和加载期同步上传；投影共用同一kernel/校验函数，不复制数学。与shared-plane独立对照，完整点数/SH/图像不变，记录DXGI与非本地用量、加载峰值和常驻。不能把少计费假定为实际显存收益，仍按80%门槛验收。 |
| R07（图像失败诊断） | 用现有debug-pixel trace解释新俯斜视角CUDA投影误差 | shengyi最大11/255、jiulonghu最大21/255，各2个像素超过8。对同机冻结二进制、相同相机/像素记录D3D12与CUDA贡献候选的原始index/排序rank/椭圆/颜色。先区分排序次序和几何差异，不凭最大误差直接归因，也不提高门槛。 |

排序仍处理全部32位径向key，同键原始顺序不变。不重复上一轮数学变体、失败analytic tile或conditional-WHILE，除非新资料给出可测试的具体根因。公共SDK/renderer契约与产品开关保持原状，合格候选另验收。
