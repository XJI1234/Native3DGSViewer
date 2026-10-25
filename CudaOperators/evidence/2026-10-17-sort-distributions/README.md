# Reset-per-iteration sorting diagnostic

Four distributions, 3 alternating rounds, 4 warmup + 20 measured iterations per backend. Both keys and original indexes restored before EVERY iteration; copy excluded from timestamps. CUDA event time is diagnostic; full GPU sort span includes D3D12/CUDA handoffs, no CPU wait within that span. Full key/value array checked against CPU stable_sort after each distribution/round/backend. No display FPS claim.

| Count | Distribution | D3D12 median ms | CUDA full span ms | CUDA kernel ms |
|---:|---|---:|---:|---:|
| 513 | random | 0.1096 | 0.4581 | 0.0215 |
| 513 | sorted | 0.1096 | 0.4588 | 0.0205 |
| 513 | reverse | 0.1096 | 0.4550 | 0.0215 |
| 513 | equal | 0.0983 | 0.4259 | 0.0195 |
| 1000000 | random | 0.4349 | 0.6031 | 0.1792 |
| 1000000 | sorted | 0.3924 | 0.5809 | 0.1669 |
| 1000000 | reverse | 0.3927 | 0.5653 | 0.1679 |
| 1000000 | equal | 0.3840 | 0.5476 | 0.1413 |
| 8000000 | random | 2.6257 | 1.3151 | 0.8970 |
| 8000000 | sorted | 2.6269 | 1.3100 | 0.8991 |
| 8000000 | reverse | 2.6263 | 1.3090 | 0.8970 |
| 8000000 | equal | 2.5647 | 1.2620 | 0.8366 |
| 22480361 | random | 9.5442 | 2.8535 | 2.4351 |
| 22480361 | sorted | 9.5485 | 2.8536 | 2.4341 |
| 22480361 | reverse | 9.5498 | 2.8575 | 2.4341 |
| 22480361 | equal | 6.3479 | 2.7143 | 2.2702 |
