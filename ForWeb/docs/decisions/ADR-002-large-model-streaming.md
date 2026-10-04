# ADR-002: Bounded decoding and complete large-model rendering

2026-10-04. Authorized by the request to load all 38 reference models and continue stage optimizations.

## Requirement and limits

The local acceptance gate is 38/38 successful complete loads, including 22,480,361-point PLY and SPZ. ResourceLimit is a failure at this gate. All points, SH coefficients, stable ordering and float32 precision are retained. Hardware without WebGPU, enough GPU memory or browser storage cannot have an unconditional success guarantee. Capability/resource errors must remain explicit; no silent downsampling is permitted.

## Decision

Download URL input to a unique OPFS job directory. Decode PLY and legacy SPZ in bounded WASM batches. Incremental gzip validates checksum, exact inflated length and absence of trailing members before publishing. Preserve supported SPZ v4 and its existing bounded path until streaming v4 is verified. Pack world-space float32 centers before any chunk-local rebase; accumulate global double bounds, then rebase disk data exactly once. A File snapshot is taken only after writes, flush and close.

Scene owns an optional reference-counted backing with read, retain, release, residentBytes and totalBytes. Decoder transfers one ownership reference; renderer retains on admission; engine releases decoder ownership in finally. Recovery retains independently before disposing the old renderer. Cancellation, failure and close remove job storage after its last owner. No source model is modified.

Renderer uses one source page per projection dispatch, globally indexed ellipses and pairs, and one stable global sort/draw. Compact scalar packing removes padding (56/92/152/236 bytes); ellipse layout is 40 bytes with unchanged float32 values. Adapter storage/buffer limits are explicitly requested; real allocation errors remain failures. Upload uses bounded read/write chunks and GPU fences for staging backpressure.

Radix scatter uses per-bin lane bitmaps and popcount for deterministic local rank. Count/scatter use 2D workgroup scheduling; block-prefix assigns contiguous segments to lanes, supporting more than 256 prefix blocks. Test beyond 16,777,216 points against an independent closed-form stable reference.

## Ordered verification

1. Stable bitmap sort: existing CPU-reference GPU cases and before/after timestamp benchmarks.
2. Streaming decoder: chunk boundaries, full-buffer equivalence, malformed data, checksum/trailing input, cancellation and storage cleanup.
3. Paged renderer: fixed image tests, complete global sort, GPU budget/failure/recovery.
4. Maximum-model risk gate, then all 38 models sequentially with close/fence between jobs.
5. Stage measurements and controlled full-frame comparisons; SDK, lifecycle and independent code review.

No historical stability or benchmark evidence is relabeled as validating this implementation. New evidence records profile, count, hashes, device and commands.
