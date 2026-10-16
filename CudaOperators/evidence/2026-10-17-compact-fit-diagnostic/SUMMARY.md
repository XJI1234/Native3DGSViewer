# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-sort | 0.4045 | 0.8417 | -101.17 | [-112.78, -90.72] | True |
| changjin_v1.ply | cuda-compact-full | 0.4045 | 0.8248 | -97.74 | [-105.19, -84.11] | True |
| changjin_v1.ply | cuda-compact-buckets | 0.4045 | 0.8535 | -105.27 | [-111.01, -94.66] | True |
| shengyi_v1.ply | cuda-sort | 1.2718 | 1.5104 | -20.44 | [-24.50, -18.05] | True |
| shengyi_v1.ply | cuda-compact-full | 1.2718 | 1.5580 | -22.45 | [-22.75, -22.00] | True |
| shengyi_v1.ply | cuda-compact-buckets | 1.2718 | 1.6451 | -30.03 | [-32.65, -28.00] | True |
| tumu_v1.ply | cuda-sort | 1.9067 | 2.1187 | -10.99 | [-11.12, -10.74] | True |
| tumu_v1.ply | cuda-compact-full | 1.9067 | 2.1750 | -13.90 | [-14.29, -13.46] | True |
| tumu_v1.ply | cuda-compact-buckets | 1.9067 | 2.2876 | -19.56 | [-20.19, -19.15] | True |
| zhihuizhimen.ply | cuda-sort | 5.9715 | 5.6724 | 4.90 | [4.82, 5.01] | True |
| zhihuizhimen.ply | cuda-compact-full | 5.9715 | 5.8409 | 2.07 | [1.93, 2.19] | True |
| zhihuizhimen.ply | cuda-compact-buckets | 5.9715 | 6.0027 | -0.64 | [-0.70, -0.52] | True |
| jiulonghu_v1.ply | cuda-sort | 34.8621 | 29.8332 | 14.17 | [13.91, 14.52] | True |
| jiulonghu_v1.ply | cuda-compact-full | 34.8621 | 30.8383 | 11.34 | [11.15, 11.68] | True |
| jiulonghu_v1.ply | cuda-compact-buckets | 34.8621 | 31.4189 | 9.77 | [9.65, 9.92] | True |
