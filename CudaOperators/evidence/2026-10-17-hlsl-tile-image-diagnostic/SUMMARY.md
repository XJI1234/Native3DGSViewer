# Paired GPU diagnostic

Offscreen serialized GPU timing; no Present or input-latency conclusion. Three-round bootstrap may understate uncertainty; lifecycle/resource gates remain separate.

| Model | Backend | D3D12 ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| shengyi_v1.ply | hlsl-tile-exact-16 | 1.3568 | 31.4373 | -2217.02 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | hlsl-tile-unorm-16 | 1.3568 | 31.7194 | -2237.81 | unavailable (<3 rounds) | False |
