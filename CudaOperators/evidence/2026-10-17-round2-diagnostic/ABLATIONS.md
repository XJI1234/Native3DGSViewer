# Same-slot ablations (short diagnostics)

All comparisons use matched frame-slot counts and frozen runtime. CUDA events enabled. Not formal acceptance.

| Model | Candidate vs CUDA control | Metric | Before ms | After ms | Reduction % | Same image |
|---|---|---|---:|---:|---:|---|
| changjin_v1.ply | cuda-sort-i32-queued vs cuda-sort-queued | gpu_total_ms | 0.788480 | 0.788992 | -0.09 | True |
| changjin_v1.ply | cuda-project-warp-256 vs cuda-project-256 | cuda_projection_kernel_ms | 0.108544 | 0.108544 | 0.00 | True |
| changjin_v1.ply | cuda-project-plane-256 vs cuda-project-256 | cuda_projection_kernel_ms | 0.108544 | 0.097280 | 10.38 | True |
| changjin_v1.ply | cuda-project-sort-warp-256 vs cuda-project-sort-256 | gpu_total_ms | 0.749568 | 0.763392 | -1.25 | True |
| changjin_v1.ply | cuda-project-sort-plane-256 vs cuda-project-sort-256 | gpu_total_ms | 0.749568 | 0.750592 | -0.67 | True |
| shengyi_v1.ply | cuda-sort-i32-queued vs cuda-sort-queued | gpu_total_ms | 1.392640 | 1.394176 | -0.12 | True |
| shengyi_v1.ply | cuda-project-warp-256 vs cuda-project-256 | cuda_projection_kernel_ms | 0.456192 | 0.458752 | -0.52 | True |
| shengyi_v1.ply | cuda-project-plane-256 vs cuda-project-256 | cuda_projection_kernel_ms | 0.456192 | 0.392192 | 13.62 | True |
| shengyi_v1.ply | cuda-project-sort-warp-256 vs cuda-project-sort-256 | gpu_total_ms | 1.417216 | 1.413120 | -0.03 | True |
| shengyi_v1.ply | cuda-project-sort-plane-256 vs cuda-project-sort-256 | gpu_total_ms | 1.417216 | 1.430528 | 0.20 | True |
| tumu_v1.ply | cuda-sort-i32-queued vs cuda-sort-queued | gpu_total_ms | 1.956352 | 1.970176 | -0.41 | True |
| tumu_v1.ply | cuda-project-warp-256 vs cuda-project-256 | cuda_projection_kernel_ms | 0.698368 | 0.702464 | -0.66 | True |
| tumu_v1.ply | cuda-project-plane-256 vs cuda-project-256 | cuda_projection_kernel_ms | 0.698368 | 0.599040 | 14.18 | True |
| tumu_v1.ply | cuda-project-sort-warp-256 vs cuda-project-sort-256 | gpu_total_ms | 2.018304 | 2.030592 | -0.60 | True |
| tumu_v1.ply | cuda-project-sort-plane-256 vs cuda-project-sort-256 | gpu_total_ms | 2.018304 | 2.017280 | 1.58 | True |
| zhihuizhimen.ply | cuda-sort-i32-queued vs cuda-sort-queued | gpu_total_ms | 5.581312 | 5.554688 | 0.40 | True |
| zhihuizhimen.ply | cuda-project-warp-256 vs cuda-project-256 | cuda_projection_kernel_ms | 2.152448 | 2.166784 | -0.67 | True |
| zhihuizhimen.ply | cuda-project-plane-256 vs cuda-project-256 | cuda_projection_kernel_ms | 2.152448 | 1.849344 | 14.07 | True |
| zhihuizhimen.ply | cuda-project-sort-warp-256 vs cuda-project-sort-256 | gpu_total_ms | 5.936640 | 5.998080 | -1.68 | True |
| zhihuizhimen.ply | cuda-project-sort-plane-256 vs cuda-project-sort-256 | gpu_total_ms | 5.936640 | 5.602304 | 5.77 | True |
| jiulonghu_v1.ply | cuda-sort-i32-queued vs cuda-sort-queued | gpu_total_ms | 30.383616 | 30.339072 | 0.15 | True |
| jiulonghu_v1.ply | cuda-project-warp-256 vs cuda-project-256 | cuda_projection_kernel_ms | 13.327872 | 13.410304 | -0.64 | True |
| jiulonghu_v1.ply | cuda-project-plane-256 vs cuda-project-256 | cuda_projection_kernel_ms | 13.327872 | 11.211776 | 15.84 | True |
| jiulonghu_v1.ply | cuda-project-sort-warp-256 vs cuda-project-sort-256 | gpu_total_ms | 32.459776 | 32.549888 | -0.13 | True |
| jiulonghu_v1.ply | cuda-project-sort-plane-256 vs cuda-project-sort-256 | gpu_total_ms | 32.459776 | 30.411264 | 6.41 | True |
