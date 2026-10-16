# Frame-slot boundary correctness

24 fresh processes under D3D12 Debug Layer: six backends × four `(sample,warmup)` frame counts `(1,0),(2,1),(4,2),(7,4)`. These cover less than, equal-near and more than the three-slot ring depth, including partial final rings and phase draining. All exited successfully with no D3D12 validation errors. Frame IDs are complete and ordered, camera times monotonic, timing columns nonnegative, CPU wall at least CPU submit, and every final 1920×1080 BGRA image is byte-identical to the matching one-slot D3D12 reference. Commands, hashes and CSVs are retained in `results.json` and per-run files. One lossless reference PNG replaces 24 identical raw BGRA duplicates.

Backends: d3d12, d3d12-queued, cuda-sort, cuda-sort-graph, cuda-sort-queued, cuda-sort-graph-queued. Reproduction: `python CudaOperators/tools/verify_queue.py --output <new-directory>`; new runs keep raw images in ignored `out/cuda-results/` and archive CSVs/hashes/logs here.

This validates offscreen command allocator/query/event reuse and fixed-camera final images. It does not validate GUI resize/cancel/device removal, every intermediate moving-camera image or input-to-Present latency. These are correctness runs, not performance evidence.
