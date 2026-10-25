# E05 stable compaction diagnostic

132 runs across three groups; this group: 60 runs, 3 paired rounds, 60 warmup / 120 measured frames. Kernel events ON. Runtime hashes unchanged. All candidate images are byte-identical to the native reference.

Offscreen serialized GPU timing; not display FPS or formal-duration acceptance.

| Model | Visible / source | Buckets vs CUB full-array % |
|---|---:|---:|
| changjin_v1.ply | 170799 / 170799 | -2.10 |
| shengyi_v1.ply | 804758 / 804758 | -8.05 |
| tumu_v1.ply | 1248730 / 1248730 | -7.72 |
| zhihuizhimen.ply | 3904214 / 3914609 | -5.83 |
| jiulonghu_v1.ply | 22480361 / 22480361 | -5.13 |

See SUMMARY.md for native-relative timings and whole-round confidence intervals.

Full visibility: compaction loses to sorting the full array on every model. Retain the full-array control; do not choose buckets by point count alone.
