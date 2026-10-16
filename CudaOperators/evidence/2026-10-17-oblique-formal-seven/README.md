# Full-campus oblique, seven formal paired rounds

RTX3080 10GB/driver617.42/CUDA13.3.0, 22,480,361 source splats, original SH3, 1920×1080, stable radial sorting, unchanged quality. Fixed yaw30°, pitch-45°, dolly2, pan0 through the existing CameraController. All runs report all22,480,361 visible after timing; nonzero-alpha coverage12.7778%. This shows the full campus rather than the near-horizontal strip in the earlier fit/pan groups.

Native and ordinary CUB both use three frame slots; no Graph, CUDA event diagnostics, sanitizer, concurrent build or other project GPU test during timing. Fourteen runs, each30 seconds warmup/60 seconds sample, **26,184 measured frames**, seven complete alternating AB/BA rounds. Every image is byte-identical. All inputs, collector and frozen runtime fingerprints match; runtime remains unchanged. GPU boundary snapshots are outside the benchmark processes and are not continuous telemetry.

Median of round medians: **34.925056→29.779968ms**. Mean paired full-GPU-span reduction **14.771645%**, whole-round20,000-resample bootstrap95% interval **14.653946–14.936190%**. All14 runs passed the exact image/count/runtime checks. The slowest1% inverse GPU-time statistic improves16.69–17.82% across pairs; it is not measured display FPS.

The first three rounds were retained; their reduction14.84% [14.63,15.22] straddled the15% target. Four additional rounds continued the global order with the same binaries, not a recompiled implementation. `merge_rounds.py` verifies exact measurement parameters, source hashes, all runtime files and collector hash, complete pairs and distinct original runs. Original groups are untouched in `../2026-10-17-oblique-formal` and `../2026-10-17-oblique-formal-extra`. `results.json` preserves both group manifests' hashes, original commands and source repetitions. Copied CSV/PNG/log files here are the same evidence, **not additional independent runs**.

The complete seven-round interval lies below15%. **This case fails the optional production-backend performance threshold.** It supports local GPU research value above5%, subject to separate resource/lifecycle gates; it does not justify a default backend. Small-model regressions, Debug Layer retirement handles, driver-fault teardown, broader model/camera coverage, GUI Present/input latency and soak remain unresolved. No production SDK/renderer CUDA backend is integrated.

Observed sort-only local DXGI peak7,157,047,296bytes versus budget9,736,028,160bytes (<80%) in this group. This does not resolve projection shared-SH accounting or prove failure recovery. These data freeze the ownership-guarded build at78bd75e, before the later Graph-zero-count host validation fix; ordinary positive-count CUB GPU math is unchanged by that fix.

Reproduce from repository root:

```powershell
python CudaOperators/tools/collect.py --models jiulonghu_v1.ply --backends d3d12-queued cuda-sort-queued --image-reference d3d12-queued --yaw-degrees 30 --pitch-degrees -45 --dolly-steps 2 --formal --repetitions 7 --output CudaOperators/evidence/<new-directory>
python CudaOperators/tools/analyze.py CudaOperators/evidence/<new-directory>
```

See[SUMMARY.md](SUMMARY.md), `analysis.json`, `results.json` and per-run CSV/PNG/log/memory files. No Present, display FPS or input-latency conclusion.
