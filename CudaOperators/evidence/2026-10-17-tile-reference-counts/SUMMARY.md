# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-tile-count-8 | 0.4403 | 0.8812 | -100.12 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-tile-count-16 | 0.4403 | 0.8786 | -99.53 | unavailable (<3 rounds) | True |
| shengyi_v1.ply | cuda-tile-count-8 | 1.2708 | 1.7454 | -37.35 | unavailable (<3 rounds) | True |
| shengyi_v1.ply | cuda-tile-count-16 | 1.2708 | 1.8053 | -42.06 | unavailable (<3 rounds) | True |
| tumu_v1.ply | cuda-tile-count-8 | 1.9077 | 2.4120 | -26.44 | unavailable (<3 rounds) | True |
| tumu_v1.ply | cuda-tile-count-16 | 1.9077 | 2.4172 | -26.70 | unavailable (<3 rounds) | True |
| zhihuizhimen.ply | cuda-tile-count-8 | 5.9612 | 6.5347 | -9.62 | unavailable (<3 rounds) | True |
| zhihuizhimen.ply | cuda-tile-count-16 | 5.9612 | 6.5495 | -9.87 | unavailable (<3 rounds) | True |
| jiulonghu_v1.ply | cuda-tile-count-8 | 34.7484 | 36.4795 | -4.98 | unavailable (<3 rounds) | True |
| jiulonghu_v1.ply | cuda-tile-count-16 | 34.7484 | 36.4646 | -4.94 | unavailable (<3 rounds) | True |
