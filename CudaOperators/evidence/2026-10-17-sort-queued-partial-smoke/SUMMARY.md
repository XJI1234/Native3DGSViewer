# Paired GPU diagnostic

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

Timing reference: `d3d12-queued`. Image reference: `d3d12-queued`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| jiulonghu_v1.ply | cuda-sort-queued | 25.0854 | 20.9009 | 16.68 | unavailable (<3 rounds) | True |
