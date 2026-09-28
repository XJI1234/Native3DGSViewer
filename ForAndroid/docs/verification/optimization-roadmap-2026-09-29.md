# Android Vulkan performance follow-up, 2026-09-29

## Measured starting point

PA2455 / Adreno 735, 1,179,648-point PLY: the subgroup scatter candidate
reduced cold GPU sort from about 33.7 ms to 15.9 ms. A later complete run
measured 22.1 ms sort, 3.7 ms projection and 9.5 ms draw. On the 1,218,232-point
SH3 SPZ, projection / sort / draw averaged 20.0 / 19.0 / 9.0 ms. These are
stage means from timestamp queries, not sustained display frame times. The
three-run mobile frame-rate and fixed-camera SSIM gates remain open. Raw data
and capture limitations are in [the tablet record](device-performance-2026-09-28.md).

The renderer currently projects every point, gives rejected points a sentinel
key and transparent color, sorts all points in eight 4-bit radix passes, and
draws `scene.count` instances. A 75% Surface buffer saved little time on the
SH3 scene because sorting and SH projection did not shrink. This identifies
work reduction before rasterization as the first experiment.

## Ranked experiments

1. **Visible-set compaction and indirect work counts.** After projection,
   prefix-scan visibility flags into compact key/value pairs in original point
   order. Feed the compact count to subsequent compute passes through an
   indirect dispatch buffer and to a one-command indirect draw. Preserve
   depth-key and original-index order for equal keys. Compare with the current
   full-count path at identical SH, viewport and camera. Record visible count,
   compaction cost, GPU sort/draw time, total present interval and CPU-reference
   key order. Keep only if the whole frame improves in three comparable runs;
   keep the existing path when nearly all points are visible or the extra scan
   costs more than it saves. Vulkan 1.1 already includes `vkCmdDispatchIndirect`
   and `vkCmdDrawIndirect`; no CPU count readback is required.
2. **SH3 coefficient bandwidth.** Probe `VkPhysicalDevice16BitStorageFeatures`
   and test half-precision *storage* for SH coefficients while retaining
   float32 evaluation. Vulkan 1.1 includes 16-bit storage support but requires
   `storageBuffer16BitAccess` to be enabled; float16 arithmetic is a separate
   capability. Measure upload/peak memory, SH3 project time, and fixed-camera
   image error against float32. Trial this in Mobile quality first, with a
   float32 fallback. Reject if the reduction in memory traffic fails to improve
   total frame time or creates local color artifacts.
3. **Alternative stable radix organization.** Compare GPUOpen FidelityFX
   Parallel Sort's Vulkan design as an algorithmic reference, including its
   histogram/scan/scatter work and scratch requirements. The prior local
   8-bit radix experiment increased sort time from about 33.7 to 52.8 ms on
   this device, so a larger digit width alone is not promising. Any port must
   pass duplicate-heavy million-key CPU-reference tests and improve the
   full-frame median on Adreno before replacing the subgroup path. Keep the
   baseline fallback for unmeasured drivers.
4. **Screen-error LoD and tile-local methods.** Octree-GS and Mip-Splatting
   motivate hierarchical detail selection and scale-aware filtering for Mobile
   quality. They require a scene preprocessing/cache design and fixed-camera
   comparisons; neither is a drop-in replacement for arbitrary PLY/SPZ.
   Tile-local sorting or blending also changes transparency ordering and needs
   per-pixel visual checks. Evaluate only after the lossless work-reduction
   experiments. Mobile acceptance is strictly SSIM > 0.80 per aligned frame,
   with separate checks for edges, holes and temporal popping.

For each experiment, keep APK and shader hashes, the camera path, 30-second
warmup, three 60-second captures, display intervals, GPU stages, active count,
thermal state and screenshots. The 30-minute warm-device run remains required.
Do not infer a 30 FPS result from a single GPU stage or `vkQueuePresentKHR`
return interval.

## Sources checked

- [Khronos Vulkan subgroup guide](https://github.com/KhronosGroup/Vulkan-Guide/blob/main/chapters/subgroups.adoc): query supported stages and operations before ballot use.
- [Khronos 16-bit storage sample](https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/performance/16bit_storage_input_output): storage and arithmetic are separate; enable the queried storage feature.
- [Khronos barrier sample](https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/performance/pipeline_barriers): tighten stage/access dependencies only after correctness checks.
- [Khronos wait-idle sample](https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/performance/wait_idle): fences permit frame-resource reuse without draining the queue.
- [GPUOpen FidelityFX Parallel Sort](https://github.com/GPUOpen-Effects/FidelityFX-ParallelSort): Vulkan compute radix reference, not an Android performance guarantee.
- [Octree-GS](https://github.com/city-super/Octree-GS) and [Mip-Splatting](https://openaccess.thecvf.com/content/CVPR2024/html/Yu_Mip-Splatting_Alias-free_3D_Gaussian_Splatting_CVPR_2024_paper.html): research directions for hierarchical and scale-aware Mobile quality.
