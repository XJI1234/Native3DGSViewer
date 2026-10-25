# Full tile-list sizing diagnostic

One round; fixed capacity (10M for small/mid models,33M for largest), native draw retained. Images byte-identical, flags=0. Whole timing includes list generation but does not measure CUDA rasterization and is not a speedup claim. Scratch/shared bytes admitted under native80% policy before allocations; post-import shared DXGI accounting remains unresolved.

| Model | Tile size | Actual refs | Max tile length | Workspace bytes |
|---|---:|---:|---:|---:|
| jiulonghu_v1.ply | 8 | 29682009 | 458459 | 896919695 |
| jiulonghu_v1.ply | 16 | 26214950 | 963538 | 896723087 |
