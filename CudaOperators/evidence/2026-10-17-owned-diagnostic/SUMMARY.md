# Paired GPU diagnostic

Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. Bootstrap resamples whole paired rounds; small round counts may understate uncertainty. Lifecycle/resource gates remain separate.

Timing reference: `d3d12-queued`. Image reference: `d3d12-queued`.

| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |
|---|---|---:|---:|---:|---|---|
| changjin_v1.ply | cuda-sort-queued | 0.5478 | 0.7747 | -41.40 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-project-sort-plane-256 | 0.5478 | 0.7245 | -32.24 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-project-sort-owned-plane-256 | 0.5478 | 0.7475 | -36.45 | unavailable (<3 rounds) | True |
| changjin_v1.ply | cuda-project-sort-owned-plane-256-queued | 0.5478 | 0.7660 | -39.81 | unavailable (<3 rounds) | True |
| shengyi_v1.ply | cuda-sort-queued | 1.2974 | 1.3891 | -7.06 | unavailable (<3 rounds) | True |
| shengyi_v1.ply | cuda-project-sort-plane-256 | 1.2974 | 1.3563 | -4.54 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-project-sort-owned-plane-256 | 1.2974 | 1.3517 | -4.18 | unavailable (<3 rounds) | False |
| shengyi_v1.ply | cuda-project-sort-owned-plane-256-queued | 1.2974 | 1.3732 | -5.84 | unavailable (<3 rounds) | False |
| tumu_v1.ply | cuda-sort-queued | 1.9333 | 1.9553 | -1.14 | unavailable (<3 rounds) | True |
| tumu_v1.ply | cuda-project-sort-plane-256 | 1.9333 | 1.9456 | -0.64 | unavailable (<3 rounds) | True |
| tumu_v1.ply | cuda-project-sort-owned-plane-256 | 1.9333 | 1.9180 | 0.79 | unavailable (<3 rounds) | True |
| tumu_v1.ply | cuda-project-sort-owned-plane-256-queued | 1.9333 | 1.9553 | -1.14 | unavailable (<3 rounds) | True |
| zhihuizhimen.ply | cuda-sort-queued | 6.2607 | 5.7452 | 8.24 | unavailable (<3 rounds) | True |
| zhihuizhimen.ply | cuda-project-sort-plane-256 | 6.2607 | 5.7462 | 8.22 | unavailable (<3 rounds) | True |
| zhihuizhimen.ply | cuda-project-sort-owned-plane-256 | 6.2607 | 5.6059 | 10.46 | unavailable (<3 rounds) | True |
| zhihuizhimen.ply | cuda-project-sort-owned-plane-256-queued | 6.2607 | 5.5941 | 10.65 | unavailable (<3 rounds) | True |
| jiulonghu_v1.ply | cuda-sort-queued | 35.5226 | 30.3186 | 14.65 | unavailable (<3 rounds) | True |
| jiulonghu_v1.ply | cuda-project-sort-plane-256 | 35.5226 | 30.5157 | 14.09 | unavailable (<3 rounds) | False |
| jiulonghu_v1.ply | cuda-project-sort-owned-plane-256 | 35.5226 | 30.4502 | 14.28 | unavailable (<3 rounds) | False |
| jiulonghu_v1.ply | cuda-project-sort-owned-plane-256-queued | 35.5226 | 30.4932 | 14.16 | unavailable (<3 rounds) | False |
