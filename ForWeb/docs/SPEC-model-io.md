# Spec: model-io

## SPZ v4 bounded decoding (2026-10-25)

SPZ v4 now uses the disk-backed batch path for every model, including SH0.
The 32-byte header and uint64 TOC are revalidated against admitted count/degree,
supported flags and exact input length. Five/six sequential Zstd frames inflate
into attribute ranges of one job-owned OPFS file. Each range must contain exactly
one ordinary, dictionary-free frame, with exact decoded length and no trailing
compressed bytes. Checksums are verified when present. Zstd window requirements
are inspected before decoding, capped at 128 MiB, and included in CPU admission;
oversized windows report ResourceLimit, never grow WASM without admission.

Unpacking uses the pinned vendor's v4 math (v3 smallest-three quaternions and
unchanged attribute layout), then existing tile packing and one global rebase.
No whole compressed input, full attribute array or normalized scene is retained
in WASM/JS. Point order, all SH coefficients, coordinate convention and GPU ABI
are preserved. Single/pthreads selection and fallback apply to v4 batches too;
Zstd inflate remains serial and is not described as parallel decompression.

For PLY and SPZ1–4, batch capacity shrinks in 64-point tiles to fit available CPU
budget (including retained scenes, runtime, allocator allowance, thread stacks,
input/output copies, bounded compression scratch and Zstd window). A failure to
fit the smallest batch remains ResourceLimit. GPU/storage limits remain enforced;
this change does not claim unlimited-size rendering or silently reduce quality.
Cancellation/timeout/error remove all temporary job files; success retains only
the packed scene backing until the final owner releases it.

Internal `scene.decodeTimings` also exposes numeric allocation diagnostics:
`batchPoints`, `estimatedCpuPeakBytes` (includes retained engine pages),
`wasmHeapBytes` (observed linear heap high-water), `zstdWindowBytes` (maximum
declared frame window). These are not milliseconds or process RSS; all other
existing timing fields retain their original meaning.

preview.4 additionally publishes these four fields as optional, readonly numeric
members of `Snapshot.decoder: DecoderInfo`. They describe the committed scene's
decoder and are replaced/cleared with it, including recovery. Hosts and framework
templates can inspect them without reading private scene/renderer objects.
`estimatedCpuPeakBytes` is admission accounting, `wasmHeapBytes` is linear memory,
and neither is total process RSS. Absent fields mean unavailable (older/small
memory paths), never zero memory use. Existing backend/threads/fallback semantics
and callers remain source-compatible.

This extension supersedes historical statements below that v4 uses whole-scene
in-memory admission. Public EngineOptions and source/error contracts are unchanged.

2026-10-06 增量：可回退的单线程/pthreads 并行路径契约见 [SPEC-parallel-decoder](SPEC-parallel-decoder.md)。解码、归并、资源、失败和验收规则先于实现；已有 Source/错误/模型语义保持。

2026-10-04. Implementation authorized by the user; provider specification for ForWeb/model-io.

## Objective and boundaries
File/Blob/URL preflight, shared C++ PLY/SPZ WASM, bounded decoder Worker. Depends on splat-types. Existing native provider contracts remain unchanged.

## Public contract

Legacy SPZ gzip is streamed through a fixed 64 KiB validation buffer before the vendor decoder. Inflated length must exactly match the supported header/count/SH layout, CRC must pass, and compressed trailing members/data are rejected. This prevents trailing inflated data from bypassing the count-based memory estimate. The complete SPZ header is revalidated against the admitted scene before decoding.

Retained page bytes are supplied from renderer ownership, including admitted uploads and scenes detached by close until actual release. CPU admission must not infer zero retained bytes merely from an empty active scene.

`cpuBytes` bounds a conservative estimated decode peak, not measured process RSS: input retention + normalized scene + SPZ unpack temporaries + WASM high-water packing + transferred pages + retained scene + 32 MiB runtime margin. The wasm32 linear-memory estimate is independently bounded to 960 MiB. Point count/SH degree/page capacity/GPU peak are checked after probe and before scene allocation. False-positive resource rejection is preferable to a browser OOM; no silent LOD or SH reduction.
System semantic constraints are defined in [system design](system-technical-design.md). TypeScript signatures live in the owning src module and are exported through index.ts; API changes update this provider document first. Expected errors use Result with stable codes; no null/string error substitutes. Scene data is opaque and renderer layout is private. Cloud integration is independently optional.

## Structure and style
ForWeb/src/model-io/, ForWeb/tests/unit and contracts/gpu. C++ under native uses C++20, four spaces and snake_case; TS uses strict settings, four spaces, PascalCase types and camelCase functions. Example: `function dispose(): Promise<void>` is idempotent and never blocks the UI synchronously.

## Commands
From ForWeb: `pnpm run typecheck`; `pnpm run test:unit`; `pnpm run test:contracts`; `pnpm run test:gpu`; `pnpm run build`; `pnpm run test:sdk`. Native decoder: `pnpm run build:wasm`. Tests requiring hardware must not pass on a missing adapter.

## Verification and success criteria
Required cases: malformed/unsupported inputs, truncation, resources, timeout, terminate/cancel. Actual output/state must match the contract, not merely method invocation. Evidence records commands, versions, model/shader hashes and skipped devices. Performance runs are separate from unit tests.

## Boundaries
Always: validate inputs and resource arithmetic, preserve ownership/identity, test failures, pin dependencies. Review new scope/public semantics before changing them; routine authorized fixes proceed. Never: silently drop points/SH, bypass failing tests, copy unlicensed code, publish model files or credentials.

## Large-model extension (ADR-002)
This extension supersedes the whole-scene memory/page admission rules for large PLY and legacy gzip SPZ. Small PLY and SPZ v4 retain the original bounded in-memory path. CPU admission describes engine allocation estimates, excluding browser/OS file caches and externally owned source Blob allocations; disk backing has no retained full ArrayBuffer. File snapshots must match the exact written length, including files beyond 2GiB; a browser snapshot/storage limit is ResourceLimit. OPFS requires a secure origin and capability support.
Batch policy admits at most 65,536 points and 4MiB encoded attributes/PLY input, whichever is smaller. Corresponding SPZ slices may be read concurrently within that byte bound. CPU admission includes normalized/cloud/packed scratch, JS copies, allocator growth allowance, gzip output and runtime margin. Rebase has a separate 4MiB bound. Counts must fit uint32 GPU indexing including a padded final workgroup; oversized declarations are ResourceLimit before allocation.
URL streams and normalized world-coordinate batches are stored in job-owned OPFS files. Streaming batches are at most 4 MiB input with bounded normalization scratch. Legacy SPZ is incrementally inflated with exact length, checksum and trailing-input validation; attributes are unpacked by the pinned vendor implementation in batches. Global rebase occurs once. Cancellation terminates the worker and removes the uniquely named job; success keeps only the backing file until its last owner releases. Small SPZ v4 retains its validated existing path. Storage exhaustion is an explicit ResourceLimit.

Streaming checks the current WASM packing view length before writing and checks backing length before rebase and after each rewrite. A short view/backing fails with DecoderFailure and closes the synchronous handle; a failed browser snapshot remains ResourceLimit. Diagnostics include expected/actual length and bounded offsets so partial storage cannot be mistaken for an accepted complete model. These checks improve diagnosis; they do not claim to repair the intermittent private-browser OPFS truncation recorded in the 2026-10-05 tuning evidence.

Streaming batch finalization validates normalized world-space centers with origin zero. Batch-local float32 rebasing is disposable and must not introduce false bound failures near zero; original world min/max and maxScale are retained for one final global rebase. Full-scene ABI validation is unchanged. Regression covers a near-zero maximum where a temporary batch-local float32 round trip exceeds the native absolute bound tolerance.
