# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| shengyi_v1.ply | cuda-tile-exact-8 | 1.3609 | 69.3166 | -4993.45 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-tile-unorm-8 | 1.3609 | 71.8295 | -5178.10 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-tile-exact-16 | 1.3609 | 27.7443 | -1938.68 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-tile-unorm-16 | 1.3609 | 30.8255 | -2165.09 | unavailable (<3 rounds) | False |
| tumu_v1.ply | cuda-tile-exact-8 | 1.9169 | 105.9036 | -5424.65 | unavailable (<3 rounds) | False |
| tumu_v1.ply | cuda-tile-unorm-8 | 1.9169 | 108.9029 | -5581.12 | unavailable (<3 rounds) | False |
| tumu_v1.ply | cuda-tile-exact-16 | 1.9169 | 37.2152 | -1841.40 | unavailable (<3 rounds) | False |
| tumu_v1.ply | cuda-tile-unorm-16 | 1.9169 | 40.5535 | -2015.54 | unavailable (<3 rounds) | False |
