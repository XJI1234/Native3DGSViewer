# Tile reference-count diagnostic

Five unmodified source PLY, full SH, fit camera, 1920x1080. Counts read only after all samples. This is a one-round sizing experiment, not performance acceptance. Capacity=1 deliberately prevents reference emission/sorting of the full list; flags=2 denotes expected whole-list capacity rejection. Native draw remains the reference; no partial list is rendered. All images byte-identical.

| Model | Source count | References 8x8 | References 16x16 |
|---|---:|---:|---:|
| changjin_v1.ply | 170799 | 871474 | 421153 |
| shengyi_v1.ply | 804758 | 2360920 | 1489177 |
| tumu_v1.ply | 1248730 | 3250586 | 2154154 |
| zhihuizhimen.ply | 3914609 | 6912113 | 5285971 |
| jiulonghu_v1.ply | 22480361 | 29682009 | 26214950 |
