# Full tile-list sizing diagnostic

One round; fixed capacity (10M for small/mid models,33M for largest), native draw retained. Images byte-identical, flags=0. Whole timing includes list generation but does not measure CUDA rasterization and is not a speedup claim. Scratch/shared bytes admitted under native80% policy before allocations; post-import shared DXGI accounting remains unresolved.

| Model | Tile size | Actual refs | Max tile length | Workspace bytes |
|---|---:|---:|---:|---:|
| changjin_v1.ply | 8 | 871474 | 4488 | 165783791 |
| changjin_v1.ply | 16 | 421153 | 6949 | 165587183 |
| shengyi_v1.ply | 8 | 2360920 | 19611 | 175927135 |
| shengyi_v1.ply | 16 | 1489177 | 42902 | 175730527 |
| tumu_v1.ply | 8 | 3250586 | 20596 | 183030687 |
| tumu_v1.ply | 16 | 2154154 | 36744 | 182834079 |
| zhihuizhimen.ply | 8 | 6912113 | 52873 | 225684751 |
| zhihuizhimen.ply | 16 | 5285971 | 109271 | 225488143 |
