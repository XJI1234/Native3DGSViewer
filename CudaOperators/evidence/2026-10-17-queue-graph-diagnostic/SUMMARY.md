# Paired GPU diagnostic

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

Timing reference: `d3d12`. Image reference: `d3d12`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | d3d12-queued | 0.4065 | 0.4014 | 3.62 | [1.01, 8.60] | True |
| changjin_v1.ply | cuda-sort | 0.4065 | 0.8008 | -95.50 | [-110.10, -79.42] | True |
| changjin_v1.ply | cuda-sort-graph | 0.4065 | 0.7480 | -79.38 | [-84.47, -69.53] | True |
| changjin_v1.ply | cuda-sort-queued | 0.4065 | 0.6533 | -58.04 | [-60.98, -52.44] | True |
| changjin_v1.ply | cuda-sort-graph-queued | 0.4065 | 0.6175 | -48.51 | [-53.54, -40.12] | True |
| shengyi_v1.ply | d3d12-queued | 1.2739 | 1.2687 | 2.40 | [0.40, 6.41] | True |
| shengyi_v1.ply | cuda-sort | 1.2739 | 1.5176 | -17.18 | [-20.90, -11.49] | True |
| shengyi_v1.ply | cuda-sort-graph | 1.2739 | 1.4945 | -15.09 | [-18.40, -9.98] | True |
| shengyi_v1.ply | cuda-sort-queued | 1.2739 | 1.3742 | -5.49 | [-8.31, -1.32] | True |
| shengyi_v1.ply | cuda-sort-graph-queued | 1.2739 | 1.3660 | -4.71 | [-7.67, -0.68] | True |
| tumu_v1.ply | d3d12-queued | 1.9098 | 1.9046 | 0.18 | [0.00, 0.27] | True |
| tumu_v1.ply | cuda-sort | 1.9098 | 2.1140 | -10.82 | [-10.99, -10.56] | True |
| tumu_v1.ply | cuda-sort-graph | 1.9098 | 2.1130 | -10.36 | [-10.64, -10.20] | True |
| tumu_v1.ply | cuda-sort-queued | 1.9098 | 1.9425 | -1.68 | [-1.88, -1.44] | True |
| tumu_v1.ply | cuda-sort-graph-queued | 1.9098 | 1.9333 | -1.10 | [-2.07, -0.43] | True |
| zhihuizhimen.ply | d3d12-queued | 5.9761 | 5.9873 | -0.22 | [-0.38, -0.10] | True |
| zhihuizhimen.ply | cuda-sort | 5.9761 | 5.6730 | 4.93 | [4.78, 5.09] | True |
| zhihuizhimen.ply | cuda-sort-graph | 5.9761 | 5.6684 | 5.15 | [5.12, 5.18] | True |
| zhihuizhimen.ply | cuda-sort-queued | 5.9761 | 5.5142 | 7.58 | [7.47, 7.73] | True |
| zhihuizhimen.ply | cuda-sort-graph-queued | 5.9761 | 5.5091 | 7.75 | [7.43, 7.92] | True |
| jiulonghu_v1.ply | d3d12-queued | 34.8785 | 34.7510 | 0.15 | [-0.03, 0.41] | True |
| jiulonghu_v1.ply | cuda-sort | 34.8785 | 29.8245 | 14.38 | [14.13, 14.52] | True |
| jiulonghu_v1.ply | cuda-sort-graph | 34.8785 | 29.7810 | 14.49 | [14.28, 14.63] | True |
| jiulonghu_v1.ply | cuda-sort-queued | 34.8785 | 29.6228 | 14.99 | [14.74, 15.17] | True |
| jiulonghu_v1.ply | cuda-sort-graph-queued | 34.8785 | 29.6151 | 14.94 | [14.76, 15.14] | True |
