# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-tile-exact-16 | 0.4403 | 7.7537 | -1660.93 | unavailable (<3 rounds) | True |
| changjin_v1.ply | hlsl-tile-exact-16 | 0.4403 | 7.3078 | -1559.65 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-front-16 | 0.4403 | 7.8080 | -1673.26 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-early5-16 | 0.4403 | 7.5592 | -1616.74 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-early4-16 | 0.4403 | 7.2084 | -1537.09 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-graph-16 | 0.4403 | 6.8265 | -1450.35 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-early4-graph-16 | 0.4403 | 6.3258 | -1336.63 | unavailable (<3 rounds) | True |
| shengyi_v1.ply | cuda-tile-exact-16 | 1.2718 | 33.2006 | -2510.51 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | hlsl-tile-exact-16 | 1.2718 | 31.1726 | -2351.05 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-tile-front-16 | 1.2718 | 32.8566 | -2483.45 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-tile-early5-16 | 1.2718 | 34.3967 | -2604.55 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-tile-early4-16 | 1.2718 | 34.2497 | -2593.00 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-tile-graph-16 | 1.2718 | 28.1242 | -2111.35 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-tile-early4-graph-16 | 1.2718 | 29.3663 | -2209.02 | unavailable (<3 rounds) | False |
