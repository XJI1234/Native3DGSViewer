# Paired GPU diagnostic

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Bootstrap resamples whole paired rounds; small round counts may understate uncertainty. Lifecycle/resource gates remain separate.

Timing reference: `d3d12-queued`. Image reference: `d3d12-queued`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-sort-queued | 0.5018 | 0.7885 | -52.72 | [-57.14, -44.48] | True |
| changjin_v1.ply | cuda-sort-i32-queued | 0.5018 | 0.7890 | -52.87 | [-57.35, -44.01] | True |
| changjin_v1.ply | cuda-project-256 | 0.5018 | 0.8090 | -56.68 | [-61.22, -49.44] | True |
| changjin_v1.ply | cuda-project-warp-256 | 0.5018 | 0.8049 | -56.41 | [-61.63, -47.19] | True |
| changjin_v1.ply | cuda-project-plane-256 | 0.5018 | 0.7967 | -54.12 | [-60.61, -42.98] | True |
| changjin_v1.ply | cuda-project-sort-256 | 0.5018 | 0.7496 | -45.87 | [-51.33, -36.89] | True |
| changjin_v1.ply | cuda-project-sort-warp-256 | 0.5018 | 0.7634 | -47.74 | [-54.39, -36.70] | True |
| changjin_v1.ply | cuda-project-sort-plane-256 | 0.5018 | 0.7506 | -46.88 | [-54.90, -36.14] | True |
| shengyi_v1.ply | cuda-sort-queued | 1.2984 | 1.3926 | -7.35 | [-7.69, -7.10] | True |
| shengyi_v1.ply | cuda-sort-i32-queued | 1.2984 | 1.3942 | -7.48 | [-7.73, -7.33] | True |
| shengyi_v1.ply | cuda-project-256 | 1.2984 | 1.6282 | -25.35 | [-25.49, -25.16] | False |
| shengyi_v1.ply | cuda-project-warp-256 | 1.2984 | 1.6297 | -25.73 | [-26.22, -25.35] | False |
| shengyi_v1.ply | cuda-project-plane-256 | 1.2984 | 1.5729 | -23.51 | [-29.18, -20.11] | False |
| shengyi_v1.ply | cuda-project-sort-256 | 1.2984 | 1.4172 | -8.94 | [-9.27, -8.40] | False |
| shengyi_v1.ply | cuda-project-sort-warp-256 | 1.2984 | 1.4131 | -8.97 | [-9.46, -8.60] | False |
| shengyi_v1.ply | cuda-project-sort-plane-256 | 1.2984 | 1.4305 | -8.72 | [-10.73, -5.17] | False |
| tumu_v1.ply | cuda-sort-queued | 1.9333 | 1.9564 | -1.30 | [-1.56, -1.17] | True |
| tumu_v1.ply | cuda-sort-i32-queued | 1.9333 | 1.9702 | -1.72 | [-2.07, -1.51] | True |
| tumu_v1.ply | cuda-project-256 | 1.9333 | 2.3199 | -20.05 | [-20.12, -19.99] | True |
| tumu_v1.ply | cuda-project-warp-256 | 1.9333 | 2.3316 | -20.46 | [-21.46, -19.70] | True |
| tumu_v1.ply | cuda-project-plane-256 | 1.9333 | 2.2200 | -14.73 | [-15.05, -14.31] | True |
| tumu_v1.ply | cuda-project-sort-256 | 1.9333 | 2.0183 | -4.30 | [-4.48, -4.01] | True |
| tumu_v1.ply | cuda-project-sort-warp-256 | 1.9333 | 2.0306 | -4.93 | [-5.44, -4.29] | True |
| tumu_v1.ply | cuda-project-sort-plane-256 | 1.9333 | 2.0173 | -2.66 | [-4.87, 1.27] | True |
| zhihuizhimen.ply | cuda-sort-queued | 6.0570 | 5.5813 | 7.80 | [7.52, 7.94] | True |
| zhihuizhimen.ply | cuda-sort-i32-queued | 6.0570 | 5.5547 | 8.17 | [7.97, 8.36] | True |
| zhihuizhimen.ply | cuda-project-256 | 6.0570 | 6.8198 | -11.32 | [-13.00, -8.89] | True |
| zhihuizhimen.ply | cuda-project-warp-256 | 6.0570 | 6.8178 | -11.39 | [-13.00, -8.81] | True |
| zhihuizhimen.ply | cuda-project-plane-256 | 6.0570 | 6.4896 | -5.96 | [-7.14, -4.04] | True |
| zhihuizhimen.ply | cuda-project-sort-256 | 6.0570 | 5.9366 | 3.02 | [1.64, 5.19] | True |
| zhihuizhimen.ply | cuda-project-sort-warp-256 | 6.0570 | 5.9981 | 1.42 | [0.97, 1.82] | True |
| zhihuizhimen.ply | cuda-project-sort-plane-256 | 6.0570 | 5.6023 | 8.62 | [7.18, 10.19] | True |
| jiulonghu_v1.ply | cuda-sort-queued | 35.5825 | 30.3836 | 14.51 | [14.42, 14.62] | True |
| jiulonghu_v1.ply | cuda-sort-i32-queued | 35.5825 | 30.3391 | 14.64 | [14.55, 14.77] | True |
| jiulonghu_v1.ply | cuda-project-256 | 35.5825 | 38.4671 | -8.16 | [-8.29, -8.07] | False |
| jiulonghu_v1.ply | cuda-project-warp-256 | 35.5825 | 38.5536 | -8.39 | [-9.00, -8.06] | False |
| jiulonghu_v1.ply | cuda-project-plane-256 | 35.5825 | 36.2993 | -2.10 | [-2.24, -1.86] | False |
| jiulonghu_v1.ply | cuda-project-sort-256 | 35.5825 | 32.4598 | 8.64 | [8.53, 8.78] | False |
| jiulonghu_v1.ply | cuda-project-sort-warp-256 | 35.5825 | 32.5499 | 8.52 | [8.39, 8.72] | False |
| jiulonghu_v1.ply | cuda-project-sort-plane-256 | 35.5825 | 30.4113 | 14.50 | [14.16, 14.71] | False |
