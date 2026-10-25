# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| tumu_v1.ply | cuda-sort | 0.7629 | 0.8177 | -7.18 | [-7.18, -7.18] | True |
| tumu_v1.ply | cuda-compact-full | 0.7629 | 0.8458 | -10.87 | [-10.87, -10.87] | True |
| tumu_v1.ply | cuda-compact-buckets | 0.7629 | 0.8018 | -5.10 | [-5.10, -5.10] | True |
