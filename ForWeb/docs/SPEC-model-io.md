# Spec: model-io

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
URL streams and normalized world-coordinate batches are stored in job-owned OPFS files. Streaming batches are at most 4 MiB input with bounded normalization scratch. Legacy SPZ is incrementally inflated with exact length, checksum and trailing-input validation; attributes are unpacked by the pinned vendor implementation in batches. Global rebase occurs once. Cancellation terminates the worker and removes the uniquely named job; success keeps only the backing file until its last owner releases. Small SPZ v4 retains its validated existing path. Storage exhaustion is an explicit ResourceLimit.
