# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| jiulonghu_v1.ply | cuda-tile-list-8 | 34.7249 | 50.3598 | -45.03 | unavailable (<3 rounds) | True |
| jiulonghu_v1.ply | cuda-tile-list-16 | 34.7249 | 50.4018 | -45.15 | unavailable (<3 rounds) | True |
