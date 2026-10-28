# Paired GPU diagnostic

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Bootstrap resamples whole paired rounds; small round counts may understate uncertainty. Lifecycle/resource gates remain separate.

Timing reference: `d3d12-queued`. Image reference: `d3d12-queued`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-sort-queued | 0.4997 | 0.7726 | -47.88 | [-58.40, -41.29] | True |
| citang_v1.ply | cuda-sort-queued | 3.1744 | 3.0060 | 5.22 | [4.97, 5.39] | True |
| danganguan_v1.ply | cuda-sort-queued | 2.8590 | 2.7361 | 4.32 | [4.24, 4.42] | True |
| haiwai_v1.ply | cuda-sort-queued | 2.7853 | 2.6660 | 3.90 | [3.49, 4.38] | True |
| he_v1.ply | cuda-sort-queued | 4.3935 | 4.0842 | 6.99 | [6.91, 7.04] | True |
| J1_v3.ply | cuda-sort-queued | 3.8927 | 3.6429 | 6.46 | [6.42, 6.49] | True |
| jiaoxuelou_v2.ply | cuda-sort-queued | 3.3475 | 3.1642 | 5.52 | [5.48, 5.60] | True |
| jiulonghu_v1.ply | cuda-sort-queued | 34.3404 | 29.4692 | 13.99 | [13.39, 14.40] | True |
| jizhonglou_v2.ply | cuda-sort-queued | 2.4013 | 2.3383 | 2.76 | [2.35, 3.43] | True |
| juyuan_v2.ply | cuda-sort-queued | 3.9603 | 3.7079 | 6.49 | [6.32, 6.62] | True |
| meiyuan_v2.ply | cuda-sort-queued | 2.0132 | 2.0101 | 0.19 | [0.00, 0.31] | True |
| PML_v1.ply | cuda-sort-queued | 1.3609 | 1.4295 | -5.25 | [-5.71, -5.01] | True |
| shengyi_v1.ply | cuda-sort-queued | 1.2974 | 1.3696 | -5.70 | [-6.20, -5.41] | True |
| spz/citang_v1.spz | cuda-sort-queued | 3.1795 | 3.0126 | 5.31 | [5.07, 5.56] | True |
| spz/danganguan_spzv4.spz | cuda-sort-queued | 2.8273 | 2.7110 | 4.05 | [3.86, 4.15] | True |
| spz/danganguan_v1.spz | cuda-sort-queued | 2.8672 | 2.7453 | 4.20 | [3.95, 4.39] | True |
| spz/haiwai_v1.spz | cuda-sort-queued | 2.7924 | 2.6798 | 4.07 | [4.02, 4.14] | True |
| spz/he_v1.spz | cuda-sort-queued | 4.3955 | 4.0924 | 8.46 | [6.65, 11.91] | True |
| spz/J1_v3.spz | cuda-sort-queued | 3.8886 | 3.6582 | 5.94 | [5.77, 6.14] | True |
| spz/jiaoxuelou_v2.spz | cuda-sort-queued | 3.3526 | 3.1734 | 5.41 | [5.15, 5.67] | True |
| spz/jiulonghu_v1.spz | cuda-sort-queued | 34.4489 | 29.4093 | 14.56 | [14.31, 14.69] | True |
| spz/jizhonglou_v2.spz | cuda-sort-queued | 2.4049 | 2.3388 | 2.67 | [2.24, 3.10] | True |
| spz/juyuan_v2.spz | cuda-sort-queued | 3.9619 | 3.7064 | 6.42 | [6.30, 6.64] | True |
| spz/meiyuan_v2.spz | cuda-sort-queued | 2.0132 | 2.0444 | -0.94 | [-1.65, 0.41] | True |
| spz/shengyi_v1.spz | cuda-sort-queued | 1.3169 | 1.4141 | -7.18 | [-7.75, -6.41] | True |
| spz/tiyuguan_v1.spz | cuda-sort-queued | 2.1791 | 2.1565 | 1.34 | [1.03, 1.51] | True |
| spz/tumu_v1.spz | cuda-sort-queued | 1.9354 | 1.9497 | -0.46 | [-0.74, -0.08] | True |
| spz/tushuguan_v1.spz | cuda-sort-queued | 2.2062 | 2.1786 | 1.43 | [1.18, 1.88] | True |
| spz/wenke_v2.spz | cuda-sort-queued | 2.6737 | 2.5748 | 3.99 | [3.70, 4.48] | True |
| spz/xingxi_v1.spz | cuda-sort-queued | 2.0357 | 2.0204 | 2.55 | [0.75, 5.75] | True |
| spz/yulin_v1.spz | cuda-sort-queued | 1.5903 | 1.6456 | -3.47 | [-3.68, -3.05] | True |
| spz/zhihuizhimen_spzv4.spz | cuda-sort-queued | 5.7610 | 5.2695 | 8.52 | [8.32, 8.64] | True |
| tiyuguan_v1.ply | cuda-sort-queued | 2.1719 | 2.1494 | 0.99 | [0.52, 1.32] | True |
| tumu_v1.ply | cuda-sort-queued | 1.9354 | 1.9451 | -0.39 | [-0.66, 0.00] | True |
| tushuguan_v1.ply | cuda-sort-queued | 2.1955 | 2.1688 | 1.34 | [1.21, 1.59] | True |
| wenke_v2.ply | cuda-sort-queued | 2.6655 | 2.5728 | 3.51 | [3.44, 3.59] | True |
| xingxi_v1.ply | cuda-sort-queued | 2.0316 | 2.0188 | 0.59 | [0.25, 0.81] | True |
| yulin_v1.ply | cuda-sort-queued | 1.6814 | 1.7367 | -3.00 | [-3.47, -2.62] | True |
| zhihuizhimen.ply | cuda-sort-queued | 5.7677 | 5.2931 | 8.12 | [7.70, 8.44] | True |
| zhishan.ply | cuda-sort-queued | 0.7721 | 0.9272 | -19.35 | [-20.13, -17.82] | True |
| zhishan_cleaned.ply | cuda-sort-queued | 0.7741 | 0.9262 | -19.47 | [-21.03, -17.75] | True |
| zhishan_v3.ply | cuda-sort-queued | 1.0445 | 1.1679 | -11.56 | [-12.16, -10.70] | True |
