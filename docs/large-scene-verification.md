# Large scene verification (0.1.2)

Hardware: Windows x64, NVIDIA GeForce RTX 3080 (10 GB dedicated video memory), 32 GB physical memory. Release x64 build. These are local measurements, not a cross-device performance guarantee.

| PLY | Splats | Input bytes | Normalized bytes | Load-only time | GPU smoke frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| `zhihuizhimen.ply` | 3,914,609 | 923,849,202 | 923,847,724 | 1,808 ms | 7.73 ms (sort 4.30 ms) |
| `jiulonghu_v1.ply` | 22,480,361 | 5,305,366,675 | 5,305,365,196 | 9,769 ms | 316.87-380.47 ms (sort 298.55-360.90 ms) |

The first file previously failed because PLY opacity logits contained 862,184 positive and 10,395 negative infinities. These now map to opacity 1 and 0. NaN and non-opacity infinities remain invalid. The second file previously failed at the 1 GiB input limit; its point count also exceeded the old 8 million renderer contract. It now loads by mapping only the header prefix and streaming about 4 MiB of PLY vertices at a time. The GPU stores basic attributes and SH in separate buffers and uses a two-dimensional projection dispatch.

`Native3DGSViewer.SceneBench.exe <file> load 1` measures decoding and validation. `smoke 1` uploads through the production copy-page transaction and renders one GPU-timed frame. The large-scene smoke frame verifies capacity, not sustained interactive performance. The desktop host records `open_elapsed_ms` and samples CPU/GPU frame time, sort time, drawn splats and DXGI budget/usage every five seconds while a scene is active. Compare sustained measurements at identical quality and camera paths before claiming a frame-rate improvement.

At the second large-scene smoke run, the adapter reported `local_budget_bytes=9,736,028,160`, `local_usage_bytes=14,450,688`, and `gpu_required_bytes=6,755,306,956`; the same 80% incremental budget rule used by the viewer admitted the scene. This budget is dynamic and can differ when other applications use the GPU.

Verification: Release x64 solution build passed; 69 GoogleTests passed; all five CTest groups passed. The 0.1.2 SDK ZIP was extracted into a new location and its independent consumer rendered the 1,179,648-point reference PLY (`SDK OK frames=31 active=1`). The Inno Setup installer compiled successfully. The installer was not installed over the existing desktop application during this run.
