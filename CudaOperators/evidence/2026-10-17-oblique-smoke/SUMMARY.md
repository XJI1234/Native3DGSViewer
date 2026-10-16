# Paired GPU diagnostic

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

Timing reference: `d3d12-queued`. Image reference: `d3d12-queued`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| jiulonghu_v1.ply | cuda-sort-queued | 34.7382 | 29.6325 | 14.70 | unavailable (<3 rounds) | True |
