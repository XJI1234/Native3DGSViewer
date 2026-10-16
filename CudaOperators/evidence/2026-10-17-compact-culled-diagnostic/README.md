# E05 stable compaction diagnostic

132 runs across three groups; this group: 36 runs, 3 paired rounds, 60 warmup / 120 measured frames. Kernel events ON. Runtime hashes unchanged. All candidate images are byte-identical to the native reference.

Offscreen serialized GPU timing; not display FPS or formal-duration acceptance.

| Model | Visible / source | Buckets vs CUB full-array % |
|---|---:|---:|
| tumu_v1.ply | 0 / 1248730 | 2.43 |
| zhihuizhimen.ply | 0 / 3914609 | 11.80 |
| jiulonghu_v1.ply | 0 / 22480361 | 26.85 |

See SUMMARY.md for native-relative timings and whole-round confidence intervals.

pan-x=1400: all three scenes have ZERO visible points. The large speedups measure empty-frame work avoidance; they do not demonstrate faster rendering of visible content. Small model still regresses against native. CPU-only tile test build overlapped later math diagnostics, not this group.
