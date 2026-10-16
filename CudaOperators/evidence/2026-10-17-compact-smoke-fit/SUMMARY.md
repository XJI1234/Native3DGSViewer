# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| tumu_v1.ply | cuda-sort | 2.0372 | 2.1120 | -3.67 | [-3.67, -3.67] | True |
| tumu_v1.ply | cuda-compact-full | 2.0372 | 2.1791 | -6.96 | [-6.96, -6.96] | True |
| tumu_v1.ply | cuda-compact-buckets | 2.0372 | 2.2774 | -11.79 | [-11.79, -11.79] | True |
