# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-project-256 | 0.4403 | 0.6835 | -55.23 | [-55.23, -55.23] | True |
| changjin_v1.ply | cuda-project-sort-256 | 0.4403 | 0.6267 | -42.33 | [-42.33, -42.33] | True |
