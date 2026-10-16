# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-tile-list-8 | 0.4393 | 3.2799 | -646.62 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-list-16 | 0.4393 | 3.2333 | -636.01 | unavailable (<3 rounds) | True |
| shengyi_v1.ply | cuda-tile-list-8 | 1.2749 | 4.4022 | -245.30 | unavailable (<3 rounds) | True |
| shengyi_v1.ply | cuda-tile-list-16 | 1.2749 | 4.4058 | -245.58 | unavailable (<3 rounds) | True |
| tumu_v1.ply | cuda-tile-list-8 | 1.9067 | 5.2004 | -172.74 | unavailable (<3 rounds) | True |
| tumu_v1.ply | cuda-tile-list-16 | 1.9067 | 5.1835 | -171.86 | unavailable (<3 rounds) | True |
| zhihuizhimen.ply | cuda-tile-list-8 | 5.9510 | 9.7572 | -63.96 | unavailable (<3 rounds) | True |
| zhihuizhimen.ply | cuda-tile-list-16 | 5.9510 | 9.7096 | -63.16 | unavailable (<3 rounds) | True |
