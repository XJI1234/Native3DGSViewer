# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-project-128 | 0.4403 | 0.7588 | -72.33 | [-72.33, -72.33] | True |
| changjin_v1.ply | cuda-project-256 | 0.4403 | 0.6641 | -50.81 | [-50.81, -50.81] | True |
| changjin_v1.ply | cuda-project-sort-128 | 0.4403 | 0.6339 | -43.95 | [-43.95, -43.95] | True |
| changjin_v1.ply | cuda-project-sort-256 | 0.4403 | 0.6938 | -57.56 | [-57.56, -57.56] | True |
