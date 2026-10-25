# E01/E02 完整GPU帧诊断

5个主PLY × 3后端 × 3配对轮次，共45次、5400个测量帧；每次60帧预热/120帧采样，1920×1080、全点数/原SH、固定fit相机。偶数轮逆转场景和后端顺序。运行时从编译输出复制为独立快照，开始/结束逐文件SHA相同。命令、输入/二进制hash和每帧数据见results.json、CSV及原始log。

复现：`python CudaOperators/tools/collect.py --output <新目录>`，随后 `python CudaOperators/tools/analyze.py <该目录>`。分析输出见[SUMMARY.md](SUMMARY.md)、analysis.json。bootstrap按整轮配对重采样；3轮和短时采集不足以通过正式验收，禁止解读为显示FPS。

所有CUDA/noop图像与对应D3D12参考逐字节相同，RGB/alpha图像门槛全部通过。PNG保存每轮最终帧，完整BGRA位于忽略的out/cuda-results/2026-10-17-sort-diagnostic，hash已归档。

结果：单独CUB排序在约17万/80万/125万点分别增加完整GPU帧耗时约92.1%/22.4%/10.7%；391万点降低4.85%，2248万点降低14.21%。noop在全部场景回退。跨API固定交接开销抵消了小场景的排序节省；391万点未到5%保留门槛，最大场景尚未到15%可选后端门槛。当前结论为**单独排序不作为全局默认优化**；继续E03/E04扩大连续CUDA区域，再正式验证胜出组合。

GPU时间由同一D3D12时间域首尾包住CUDA依赖链，逐帧CPU等待，offscreen，无Present。CPU/GPU流水线、内存峰值、故障和长稳仍待验证。初始小场景第一轮有较高基线耗时，全部保留，不挑轮次。原生D3D12仍是产品默认实现。
