# Paired GPU diagnostic

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

Timing reference: `d3d12-queued`. Image reference: `d3d12`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | d3d12 | 0.4014 | 0.4065 | -3.90 | [-9.41, -1.02] | True |
| changjin_v1.ply | cuda-sort | 0.4014 | 0.8008 | -102.68 | [-112.24, -96.31] | True |
| changjin_v1.ply | cuda-sort-graph | 0.4014 | 0.7480 | -86.11 | [-86.48, -85.50] | True |
| changjin_v1.ply | cuda-sort-queued | 0.4014 | 0.6533 | -64.06 | [-66.79, -62.63] | True |
| changjin_v1.ply | cuda-sort-graph-queued | 0.4014 | 0.6175 | -54.08 | [-55.10, -53.31] | True |
| shengyi_v1.ply | d3d12 | 1.2687 | 1.2739 | -2.55 | [-6.84, -0.40] | True |
| shengyi_v1.ply | cuda-sort | 1.2687 | 1.5176 | -20.04 | [-21.39, -19.12] | True |
| shengyi_v1.ply | cuda-sort-graph | 1.2687 | 1.4945 | -17.92 | [-18.88, -17.35] | True |
| shengyi_v1.ply | cuda-sort-queued | 1.2687 | 1.3742 | -8.09 | [-8.75, -7.26] | True |
| shengyi_v1.ply | cuda-sort-graph-queued | 1.2687 | 1.3660 | -7.30 | [-8.10, -6.21] | True |
| tumu_v1.ply | d3d12 | 1.9046 | 1.9098 | -0.18 | [-0.27, 0.00] | True |
| tumu_v1.ply | cuda-sort | 1.9046 | 2.1140 | -11.01 | [-11.19, -10.86] | True |
| tumu_v1.ply | cuda-sort-graph | 1.9046 | 2.1130 | -10.56 | [-10.94, -10.24] | True |
| tumu_v1.ply | cuda-sort-queued | 1.9046 | 1.9425 | -1.86 | [-1.99, -1.71] | True |
| tumu_v1.ply | cuda-sort-graph-queued | 1.9046 | 1.9333 | -1.28 | [-2.07, -0.70] | True |
| zhihuizhimen.ply | d3d12 | 5.9873 | 5.9761 | 0.22 | [0.10, 0.38] | True |
| zhihuizhimen.ply | cuda-sort | 5.9873 | 5.6730 | 5.14 | [5.02, 5.27] | True |
| zhihuizhimen.ply | cuda-sort-graph | 5.9873 | 5.6684 | 5.36 | [5.22, 5.54] | True |
| zhihuizhimen.ply | cuda-sort-queued | 5.9873 | 5.5142 | 7.78 | [7.63, 7.90] | True |
| zhihuizhimen.ply | cuda-sort-graph-queued | 5.9873 | 5.5091 | 7.96 | [7.77, 8.08] | True |
| jiulonghu_v1.ply | d3d12 | 34.7510 | 34.8785 | -0.15 | [-0.41, 0.03] | True |
| jiulonghu_v1.ply | cuda-sort | 34.7510 | 29.8245 | 14.25 | [14.14, 14.46] | True |
| jiulonghu_v1.ply | cuda-sort-graph | 34.7510 | 29.7810 | 14.36 | [14.22, 14.57] | True |
| jiulonghu_v1.ply | cuda-sort-queued | 34.7510 | 29.6228 | 14.86 | [14.71, 15.11] | True |
| jiulonghu_v1.ply | cuda-sort-graph-queued | 34.7510 | 29.6151 | 14.81 | [14.56, 15.07] | True |
