# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| tumu_v1.ply | cuda-sort | 0.6912 | 0.8151 | -14.21 | [-17.93, -7.06] | True |
| tumu_v1.ply | cuda-compact-full | 0.6912 | 0.8468 | -18.85 | [-22.81, -11.23] | True |
| tumu_v1.ply | cuda-compact-buckets | 0.6912 | 0.7931 | -11.44 | [-15.85, -4.17] | True |
| zhihuizhimen.ply | cuda-sort | 1.9005 | 1.5473 | 18.87 | [17.95, 20.06] | True |
| zhihuizhimen.ply | cuda-compact-full | 1.9005 | 1.6261 | 13.20 | [11.20, 14.44] | True |
| zhihuizhimen.ply | cuda-compact-buckets | 1.9005 | 1.3865 | 28.47 | [27.05, 30.73] | True |
| jiulonghu_v1.ply | cuda-sort | 10.0623 | 6.6611 | 33.79 | [33.77, 33.81] | True |
| jiulonghu_v1.ply | cuda-compact-full | 10.0623 | 7.0451 | 30.01 | [29.95, 30.11] | True |
| jiulonghu_v1.ply | cuda-compact-buckets | 10.0623 | 4.8753 | 51.57 | [51.55, 51.59] | True |
