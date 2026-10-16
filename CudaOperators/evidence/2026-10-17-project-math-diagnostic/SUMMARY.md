# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-sort | 0.4393 | 0.8156 | -91.84 | [-102.16, -84.62] | True |
| changjin_v1.ply | cuda-project-sort-256 | 0.4393 | 0.6446 | -51.06 | [-60.79, -45.64] | True |
| changjin_v1.ply | cuda-project-sort-256-rcp | 0.4393 | 0.7096 | -63.25 | [-65.48, -60.98] | True |
| changjin_v1.ply | cuda-project-sort-256-rsqrt | 0.4393 | 0.6702 | -59.41 | [-76.78, -48.90] | True |
| changjin_v1.ply | cuda-project-sort-256-rcp-rsqrt | 0.4393 | 0.6892 | -59.80 | [-64.34, -56.33] | True |
| jiulonghu_v1.ply | cuda-sort | 34.7402 | 29.8194 | 14.20 | [14.16, 14.25] | True |
| jiulonghu_v1.ply | cuda-project-sort-256 | 34.7402 | 31.6749 | 8.91 | [8.81, 8.99] | True |
| jiulonghu_v1.ply | cuda-project-sort-256-rcp | 34.7402 | 33.3865 | 3.94 | [3.90, 4.01] | True |
| jiulonghu_v1.ply | cuda-project-sort-256-rsqrt | 34.7402 | 31.6631 | 8.89 | [8.85, 8.95] | True |
| jiulonghu_v1.ply | cuda-project-sort-256-rcp-rsqrt | 34.7402 | 31.8162 | 8.65 | [8.41, 8.79] | True |
