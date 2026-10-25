# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | interop-noop | 0.4045 | 0.8182 | -100.64 | [-102.28, -98.38] | True |
| changjin_v1.ply | cuda-sort | 0.4045 | 0.7967 | -92.11 | [-100.25, -79.12] | True |
| shengyi_v1.ply | interop-noop | 1.2718 | 1.7464 | -37.50 | [-38.06, -37.13] | True |
| shengyi_v1.ply | cuda-sort | 1.2718 | 1.5667 | -22.40 | [-23.79, -20.33] | True |
| tumu_v1.ply | interop-noop | 1.9057 | 2.4064 | -26.66 | [-28.00, -25.78] | True |
| tumu_v1.ply | cuda-sort | 1.9057 | 2.1074 | -10.67 | [-10.77, -10.59] | True |
| zhihuizhimen.ply | interop-noop | 5.9709 | 6.5142 | -9.35 | [-9.61, -9.06] | True |
| zhihuizhimen.ply | cuda-sort | 5.9709 | 5.6719 | 4.85 | [4.74, 5.01] | True |
| jiulonghu_v1.ply | interop-noop | 34.7428 | 35.6470 | -2.45 | [-2.61, -2.13] | True |
| jiulonghu_v1.ply | cuda-sort | 34.7428 | 29.8327 | 14.21 | [14.02, 14.45] | True |
