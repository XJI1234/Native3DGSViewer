# Paired GPU timing (formal duration)

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Bootstrap resamples whole paired rounds; small round counts may understate uncertainty. Lifecycle/resource gates remain separate.

Timing reference: `d3d12-queued`. Image reference: `d3d12-queued`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| jiulonghu_v1.ply | cuda-sort-queued | 34.8877 | 29.7748 | 14.84 | [14.63, 15.22] | True |
