# Paired GPU diagnostic

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

Timing reference: `cuda-sort-queued`. Image reference: `d3d12`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | d3d12 | 0.6533 | 0.4065 | 36.69 | [34.40, 37.88] | True |
| changjin_v1.ply | d3d12-queued | 0.6533 | 0.4014 | 39.04 | [38.51, 40.05] | True |
| changjin_v1.ply | cuda-sort | 0.6533 | 0.8008 | -23.59 | [-30.51, -17.70] | True |
| changjin_v1.ply | cuda-sort-graph | 0.6533 | 0.7480 | -13.46 | [-14.59, -11.21] | True |
| changjin_v1.ply | cuda-sort-graph-queued | 0.6533 | 0.6175 | 6.07 | [4.63, 8.09] | True |
| shengyi_v1.ply | d3d12 | 1.3742 | 1.2739 | 5.12 | [1.30, 7.68] | True |
| shengyi_v1.ply | d3d12-queued | 1.3742 | 1.2687 | 7.48 | [6.77, 8.05] | True |
| shengyi_v1.ply | cuda-sort | 1.3742 | 1.5176 | -11.06 | [-11.62, -10.04] | True |
| shengyi_v1.ply | cuda-sort-graph | 1.3742 | 1.4945 | -9.09 | [-9.41, -8.55] | True |
| shengyi_v1.ply | cuda-sort-graph-queued | 1.3742 | 1.3660 | 0.74 | [0.60, 0.98] | True |
| tumu_v1.ply | d3d12 | 1.9425 | 1.9098 | 1.65 | [1.42, 1.85] | True |
| tumu_v1.ply | d3d12-queued | 1.9425 | 1.9046 | 1.83 | [1.68, 1.95] | True |
| tumu_v1.ply | cuda-sort | 1.9425 | 2.1140 | -8.99 | [-9.32, -8.70] | True |
| tumu_v1.ply | cuda-sort-graph | 1.9425 | 2.1130 | -8.54 | [-8.78, -8.21] | True |
| tumu_v1.ply | cuda-sort-graph-queued | 1.9425 | 1.9333 | 0.57 | [-0.18, 1.27] | True |
| zhihuizhimen.ply | d3d12 | 5.5142 | 5.9761 | -8.20 | [-8.38, -8.07] | True |
| zhihuizhimen.ply | d3d12-queued | 5.5142 | 5.9873 | -8.44 | [-8.58, -8.27] | True |
| zhihuizhimen.ply | cuda-sort | 5.5142 | 5.6730 | -2.87 | [-2.91, -2.83] | True |
| zhihuizhimen.ply | cuda-sort-graph | 5.5142 | 5.6684 | -2.63 | [-2.79, -2.47] | True |
| zhihuizhimen.ply | cuda-sort-graph-queued | 5.5142 | 5.5091 | 0.19 | [-0.05, 0.41] | True |
| jiulonghu_v1.ply | d3d12 | 29.6228 | 34.8785 | -17.63 | [-17.88, -17.28] | True |
| jiulonghu_v1.ply | d3d12-queued | 29.6228 | 34.7510 | -17.45 | [-17.80, -17.25] | True |
| jiulonghu_v1.ply | cuda-sort | 29.6228 | 29.8245 | -0.72 | [-0.76, -0.67] | True |
| jiulonghu_v1.ply | cuda-sort-graph | 29.6228 | 29.7810 | -0.58 | [-0.64, -0.53] | True |
| jiulonghu_v1.ply | cuda-sort-graph-queued | 29.6228 | 29.6151 | -0.06 | [-0.17, 0.03] | True |
