# Paired GPU diagnostic

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

Timing reference: `d3d12-queued`. Image reference: `d3d12-queued`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-sort-queued | 0.4987 | 0.7562 | -49.91 | [-62.63, -35.46] | True |
| shengyi_v1.ply | cuda-sort-queued | 1.2959 | 1.3824 | -6.96 | [-7.96, -6.24] | True |
| tumu_v1.ply | cuda-sort-queued | 1.9282 | 1.9523 | -1.31 | [-1.54, -1.14] | True |
| zhihuizhimen.ply | cuda-sort-queued | 5.9735 | 5.5009 | 7.92 | [7.78, 8.14] | True |
| jiulonghu_v1.ply | cuda-sort-queued | 34.7382 | 29.6069 | 14.77 | [14.73, 14.80] | True |
