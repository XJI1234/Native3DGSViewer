# E05 stable compaction diagnostic

132 runs across three groups; this group: 36 runs, 3 paired rounds, 60 warmup / 120 measured frames. Kernel events ON. Runtime hashes unchanged. All candidate images are byte-identical to the native reference.

Offscreen serialized GPU timing; not display FPS or formal-duration acceptance.

| Model | Visible / source | Buckets vs CUB full-array % |
|---|---:|---:|
| tumu_v1.ply | 900336 / 1248730 | -7.17 |
| zhihuizhimen.ply | 3458593 / 3914609 | -6.02 |
| jiulonghu_v1.ply | 14057341 / 22480361 | -3.17 |

See SUMMARY.md for native-relative timings and whole-round confidence intervals.

pan-x=900: visible content remains. Buckets still lose to full-array CUB. A low-priority CPU-only build overlapped collection (see project-math/build-interval.log); use this only as diagnostic evidence.
