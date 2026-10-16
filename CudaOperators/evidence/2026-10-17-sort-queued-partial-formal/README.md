# Formal-duration pan camera, with visibility and image-area limits

Same RTX 3080/driver 617.42/CUDA13.3.0, full 22,480,361 sources/SH3, 1920×1080 and original quality/order. Fixed pan-x=900; compare native D3D12 three slots and ordinary CUB three slots. Six runs alternate AB/BA, **30s warmup + 60s sample each**, three paired rounds. No CUDA event timing, build, sanitizer or other project GPU test overlaps collection. Runtime hashes unchanged.

Every backend/round directly reads **14,057,341 visible / 22,480,361 source** after all timing and screenshot capture. All final images are byte-identical. Native sample counts total7121; CUDA8564; total15685. Across-round median GPU span **25.2360→21.0248ms**; mean paired reduction **16.70%**, whole-round bootstrap 95% interval **16.62–16.81%**. Offscreen slowest-1% inverse-time improves19.16–19.60% per round; no display-FPS conclusion.

This passes the performance target for this precise camera, but **image review limits its interpretation**: nonzero alpha occupies only **1.4168%** of the viewport, bbox(1533,445)–(1919,602). Content is an edge-on strip near the right border. The fit camera also is near edge-on (2.0620% nonzero-alpha area). Do not present this as representative campus browsing or infer that high source/visible count guarantees high screen coverage. The full-campus oblique camera is tested separately.

Formal timings precede the later Context/queue ownership guard patch; exact frozen binary hashes are in results.json. Post-timing visible counts are included in this build. Production context failure handling, debug handle retirement, broader cameras/dataset, GUI/input latency and soak gates remain open. Passing this performance case does not enable a product backend.

Reproduce: `python CudaOperators/tools/collect.py --models jiulonghu_v1.ply --backends d3d12-queued cuda-sort-queued --image-reference d3d12-queued --pan-x 900 --formal --output <new-directory>` then analyze.py. PNGs, CSVs, memory, boundary GPU snapshots, exact commands and hashes are retained.
