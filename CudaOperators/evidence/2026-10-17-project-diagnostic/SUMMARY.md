# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-project-128 | 0.4065 | 0.7127 | -74.09 | [-86.11, -61.86] | True |
| changjin_v1.ply | cuda-project-256 | 0.4065 | 0.7030 | -68.27 | [-72.92, -60.81] | True |
| changjin_v1.ply | cuda-project-sort-128 | 0.4065 | 0.6600 | -56.93 | [-62.75, -45.70] | True |
| changjin_v1.ply | cuda-project-sort-256 | 0.4065 | 0.6595 | -59.92 | [-70.58, -46.98] | True |
| shengyi_v1.ply | cuda-project-128 | 1.2728 | 1.6307 | -28.01 | [-28.56, -27.25] | True |
| shengyi_v1.ply | cuda-project-256 | 1.2728 | 1.6118 | -26.41 | [-26.73, -25.80] | True |
| shengyi_v1.ply | cuda-project-sort-128 | 1.2728 | 1.4131 | -11.05 | [-11.31, -10.90] | True |
| shengyi_v1.ply | cuda-project-sort-256 | 1.2728 | 1.3967 | -9.75 | [-9.78, -9.73] | True |
| tumu_v1.ply | cuda-project-128 | 1.9098 | 2.3352 | -22.28 | [-22.47, -22.15] | True |
| tumu_v1.ply | cuda-project-256 | 1.9098 | 2.3071 | -20.98 | [-21.67, -20.46] | True |
| tumu_v1.ply | cuda-project-sort-128 | 1.9098 | 2.0321 | -6.36 | [-6.58, -6.22] | True |
| tumu_v1.ply | cuda-project-sort-256 | 1.9098 | 1.9953 | -4.47 | [-4.65, -4.37] | True |
| zhihuizhimen.ply | cuda-project-128 | 5.9628 | 6.7712 | -13.56 | [-13.62, -13.50] | True |
| zhihuizhimen.ply | cuda-project-256 | 5.9628 | 6.6678 | -11.70 | [-12.04, -11.40] | True |
| zhihuizhimen.ply | cuda-project-sort-128 | 5.9628 | 5.9110 | 0.78 | [0.68, 0.87] | True |
| zhihuizhimen.ply | cuda-project-sort-256 | 5.9628 | 5.8158 | 2.65 | [2.34, 3.01] | True |
| jiulonghu_v1.ply | cuda-project-128 | 34.7571 | 38.5096 | -10.88 | [-11.42, -10.40] | True |
| jiulonghu_v1.ply | cuda-project-256 | 34.7571 | 37.6320 | -8.24 | [-8.67, -7.91] | True |
| jiulonghu_v1.ply | cuda-project-sort-128 | 34.7571 | 32.5417 | 6.49 | [6.22, 6.89] | True |
| jiulonghu_v1.ply | cuda-project-sort-256 | 34.7571 | 31.6805 | 8.90 | [8.70, 9.17] | True |
