# Stable tile lists and bounded CUDA compositing

Experimental private interfaces only; production backend selection and public SDK unchanged.

Lists use uint64 counts/prefixes and (tile_id<<32)|global_rank keys, with full 64-bit CUB sorting. All sizing and scratch queries precede allocations and the explicit byte budget gate. Capacity overflow rejects the whole list, leaves ranges empty, and records total count; no truncation. Tests independently enumerate all tiles/ranks and compare all keys/ranges, including zero source, invalid indexes/layouts, repeated changing visibility and a 4,299,161,600-reference case (>UINT32_MAX) without allocating its impossible list.

Five list tests passed memcheck. CUDA compositing adds an independent double-precision CPU oracle for 1301 overlapping layers, reverse source order, multiple batches, partial edge tiles, both 8/16 tiles and FP32/per-splat UNORM8 quantization. Source-count fixed upper-bound dispatch uses at most 1024 references per kernel and reads no per-frame count to CPU. An explicit shared flag snapshot keeps block exits uniform. GPU surface output is blitted RGBA->native BGRA inside the full GPU span. Direct compositing passed two successive memcheck runs, each repeating all six tests 3 times (0 errors); full CUDA CTest 8/8 passed in 46.92s.

## Conditional while failure: NOT accepted

The first graph-while implementation passed ordinary correctness but failed memcheck intermittently (out-of-bounds cursor loads despite reported addresses inside the allocation, and error999). Two isolated runs stalled in CUDA cleanup; only the verified test executable PIDs 43288/44012/37824 were terminated after retaining logs. No machine configuration/TDR/driver changes.

EAGER module loading, lineinfo (identical 672 GPU instructions), cudaFuncGetAttributes/cuFuncLoad preloading, 256-byte cursor padding, 256-reference batches and texture-before-scratch creation did NOT reliably solve it. Preserve all failed attempts. A tiny standalone conditional loop initially passed; successive mode-controlled probes also failed intermittently with and without D3D12, while the ordinary unrolled kernel control passed. Exact driver/tool/library cause remains unproven. The conditional path is diagnostic only and excluded from performance claims/production acceptance. Never relabel it as a passed stability gate because one run succeeds.

Direct bounded submission is the currently verified fallback. It may be much slower; all launch overhead is included. The driver-preload/padding experiments were removed. The private conditional_while option/probe remain for reproducing the unresolved failure; default is false.

## Real sizing and images

See ../2026-10-17-tile-reference-counts/README.md for all five actual reference counts. Full list generation with native draw passed all five models and both tile sizes; images are byte-identical. Largest workspace ~896.9MB at fixed capacity33M, max tile length458459 (8) /963538 (16). These are one-round sizing diagnostics, not acceptance timing. Shared-resource DXGI accounting remains an independent unresolved gate.

See ../2026-10-17-tile-image-smoke/analysis.json: smallest real model, all four CUDA compositing variants pass RGB/alpha gates. FP32 maxRGB5/255, UNORM6/255, with corresponding alpha3/255 and4/255. The 16-tile FP32 path takes6.40ms versus native0.440ms; no performance win. Larger-model image/stability and HLSL/early/Graph ablations remain pending.
