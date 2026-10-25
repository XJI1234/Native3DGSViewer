# Formal-duration fit-camera CUB comparison

RTX 3080 10GB, driver 617.42, CUDA 13.3.0, 22,480,361 source splats with original SH3 and quality settings, 1920×1080, radial stable sorting, fit camera. Compare native D3D12 three-slot and ordinary CUDA CUB three-slot paths; no Graph, CUDA events, sanitizer, concurrent build or other project GPU test. Offscreen only: no Present/display FPS/input-latency claim.

Six runs, alternating AB/BA by whole round, **30 seconds warmup + 60 seconds sample per run**, three paired rounds. Native measured frames: 1723/1727/1731; CUDA: 2015/2018/2016. Total 11,230 measured frames. Runtime files were frozen and hashes unchanged. Exact commands, model/runtime/tool hashes, raw frame CSVs, process memory, boundary GPU snapshots and PNGs are archived. GPU snapshots occur outside the benchmark process; they do not provide continuous temperature/clock telemetry.

Across-round median GPU span: **34.8365→29.7892ms**. Mean paired reduction **14.45%**, whole-round bootstrap 95% interval **14.23–14.58%**. Every final image is byte-identical to its D3D12 reference. The inverse of the mean slowest 1% GPU frame times improves by 16.04–16.80% per round; this is an offscreen GPU statistic, not measured display FPS.

Verdict: a repeatable GPU improvement above the 5% single-optimization threshold, but **this fit-camera case does not meet the 15% optional-production-backend threshold**. The interval is wholly below 15%, so more rounds are not needed to resolve this case against that target. The separate visible-content pan case is still evaluated; this does not claim all possible cameras fail.

Observed sort-only DXGI local peak is 7,157,047,296 bytes versus local budget 9,736,028,160 bytes, below 80% in these runs. This does not resolve the rejected projection path's shared-SH double-accounting problem or establish production failure handling. Context/queue ownership, Debug Layer retirement handles, device-loss teardown, GUI interaction/latency, broader dataset/camera coverage and soak remain independent gates.

Reproduce: `python CudaOperators/tools/collect.py --models jiulonghu_v1.ply --backends d3d12-queued cuda-sort-queued --image-reference d3d12-queued --formal --output <new-directory>` then `python CudaOperators/tools/analyze.py <new-directory>`. This frozen binary precedes the later post-timing visibility-counter log addition; neither counters nor snapshots occur inside measured frames.
