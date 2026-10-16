# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| tumu_v1.ply | cuda-sort | 1.5596 | 1.7459 | -11.74 | [-12.06, -11.22] | True |
| tumu_v1.ply | cuda-compact-full | 1.5596 | 1.7961 | -14.89 | [-15.54, -13.96] | True |
| tumu_v1.ply | cuda-compact-buckets | 1.5596 | 1.8729 | -19.76 | [-20.20, -19.31] | True |
| zhihuizhimen.ply | cuda-sort | 5.5342 | 5.2214 | 5.72 | [5.62, 5.77] | True |
| zhihuizhimen.ply | cuda-compact-full | 5.5342 | 5.3724 | 2.92 | [2.80, 3.04] | True |
| zhihuizhimen.ply | cuda-compact-buckets | 5.5342 | 5.5327 | 0.04 | [-0.14, 0.24] | True |
| jiulonghu_v1.ply | cuda-sort | 25.1182 | 21.1200 | 16.00 | [15.84, 16.24] | True |
| jiulonghu_v1.ply | cuda-compact-full | 25.1182 | 21.8952 | 12.96 | [12.76, 13.31] | True |
| jiulonghu_v1.ply | cuda-compact-buckets | 25.1182 | 21.7723 | 13.34 | [13.29, 13.39] | True |
