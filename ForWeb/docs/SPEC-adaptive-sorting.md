# Adaptive sorting contract

2026-10-06. Optional extension of SPEC-render-core and SPEC-engine, authorized by the user.

`EngineOptions.sorting?: SortingOptions` adds `mode?: 'strict' | 'adaptive'` (default adaptive),
`maxSortAgeMs?: number` (default 100, range 16–1000) and `targetFrameMs?: number`
(default 1000/60, range 4–100). Options are validated, copied and frozen before
device creation and retained across recovery. Omission selects adaptive sorting from preview.3 by explicit user direction; strict remains the opt-out.
React and Vue adapters accept the same options without another interface.

Adaptive mode always projects changed frames with the current camera and all SH
coefficients. It retains a complete point permutation between radix updates,
including currently invisible points. Every projection clears visibility before
culling; the vertex stage rejects invisible points before reading ellipse geometry.
Visibility changes must never omit newly visible points or resurrect stale ellipses.
Stable full-distance sorting preserves the relative ordering of visible points.
No point reduction, quantization or frozen projection is permitted.

The existing radix ping-pong buffers also hold retained ordering: projection writes
A; radix finishes in A; same-queue copy A→B publishes ordering; drawing reads B.
On reuse A changes while B remains intact. No extra per-point GPU allocation is
required. Adaptive draws the complete permutation and rejects culled instances;
its extra vertex work and copy cost must be measured against strict mode.
A four-pass stable visibility compaction experiment regressed real browsing
performance and was removed; its evidence remains in the verification report.


First frame, candidate validation, capture, viewport/quality/Y reflection
change, large camera translation (>15% scene diagonal), and abrupt orientation
change (>45 degrees) require fresh sorting. Dynamic near/far planes affect current visibility only and do not invalidate radial ranks. Camera reconstruction noise below 1e-7 scene diagonals is treated as unchanged position. Small translations reuse ordering
until a bounded interval derived from completed sort cost and frame budget:
min(maxSortAgeMs, max(targetFrameMs, 8 × EWMA(sortMs))). This amortizes sort
work to approximately one eighth of elapsed time when cadence permits. Without
timestamp-query, cost remains zero and the fixed targetFrameMs interval is used;
this capability does not promise a speedup on adapters without GPU timing.
Maximum sort age is a scheduling bound while visible, active and able to submit,
not a GPU completion deadline. Pure rotation around an unchanged camera position
does not change radial ranks; it may reuse exactly. Pending stale ordering is
refreshed after interaction stops, without forcing perpetual idle rendering.
Suspension retains the existing no-render contract; resumption evaluates age.

Additive optional FrameStats fields: `sortAgeMs` (submitted ordering age),
`sortPositionErrorRatio` (translation since ordering / scene diagonal),
`sortReason` (fresh/update reason or reuse). `sorted` means this submission
encodes radix; it does not mean a CPU Worker finished. GPU stage timings retain
their completed `gpuFrameId`, with zero for omitted projection/sort work.

Spark 2.3.1 provides the architectural reference: sortDirty/sorting guard,
minSortIntervalMs, retained ordering texture and async Worker publication. This
implementation is original WebGPU code; it does not copy Spark source or claim
cross-queue asynchronous radix. Benchmark actual rAF browsing with identical
full-point scenes, fresh browser processes, three runs, current-pose evidence,
ordering age, low-percentile intervals and completed stage attribution. Preserve
strict image/capture equality; temporal blending order differences during small
translation are an explicit adaptive tradeoff.
