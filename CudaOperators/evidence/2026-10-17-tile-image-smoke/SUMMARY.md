# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-tile-exact-8 | 0.4403 | 15.8930 | -3509.42 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-unorm-8 | 0.4403 | 16.7352 | -3700.70 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-exact-16 | 0.4403 | 6.3995 | -1353.37 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-unorm-16 | 0.4403 | 7.2284 | -1541.63 | unavailable (<3 rounds) | True |
