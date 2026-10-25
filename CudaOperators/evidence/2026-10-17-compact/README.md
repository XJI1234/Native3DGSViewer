# E05 稳定可见集压缩与GPU数量分派

两条实现独立保留：FullCapacity使用稳定DeviceSelect+gather后仍按完整N排序；DeviceBuckets根据device count设置CUDA conditional switch handle，只执行一个预建CUB容量桶，不读回数量、不逐帧CPU等待。最小桶min(32768,N)，逐2倍增长至N，每个桶先查询scratch取最大值。所有有效前缀保留原索引/同键稳定顺序；桶输出统一到buffer0，资源地址或N变化拒绝复用旧graph。

RED见test-red.log（2项未实现失败）。CCCL3.3移除了旧cub::CountingInputIterator，改用随附thrust counting iterator；CUDA13.3 Graph API使用含edgeData的新签名，首次构建错误和修复日志保留。无vendor改动或编译兼容绕过。

正确性：0/1/513/65537 × 0/1/10/50/100%可见 × 2种模式，每个workspace连续运行原比例/互补比例/原比例，验证同一graph在不同device count间切换；百万点10%/90%/10%也全key/value对CPU稳定排序。GPU可见数同时逐项检查。后增别名/容量/固定N/地址失效测试通过。ctest-cuda.log记录7/7 CUDA组通过，新增地址测试见test-validation.log。Compute Sanitizer memcheck覆盖主矩阵与百万点，0 errors；地址校验测试后加，仅普通运行验证。

真实125万点已验证fit=1248730可见、pan-x900=900336可见、pan-x1400=0可见，两种压缩和完整排序图像门槛全部通过，见相邻compact-smoke-fit/partial/pan。首次性能只用于路径检查；全可见和72%可见时压缩增加开销，不能据此启用。完整多模型/多轮消融继续采集。

host沿用D3D12投影/SH/绘制，仅替换排序区域；shader原GPU draw counter继续驱动绘制。压缩数量仅在全部测量结束后读回并核对draw counter，不参与帧循环调度。CUDA event的sort列在本变体中表示select+gather+sort/graph整个区域，不是单独CUB时间。

已知实验性限制：workspace析构及CUDA driver API的任意故障有界性、跨Context资源所有权、异常时graph重建/显存预算和生命周期仍待检查；当前不接入生产renderer。
