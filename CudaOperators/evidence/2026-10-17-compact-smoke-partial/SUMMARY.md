# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| tumu_v1.ply | cuda-compact-full | 1.6666 | 1.7900 | -7.40 | [-7.40, -7.40] | True |
| tumu_v1.ply | cuda-compact-buckets | 1.6666 | 1.8606 | -11.64 | [-11.64, -11.64] | True |
