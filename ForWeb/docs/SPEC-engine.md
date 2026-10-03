# Spec: engine

2026-10-04. Implementation authorized by the user; provider specification for ForWeb/engine.

## Objective and boundaries
Latest-wins load transactions, camera/surface/device revisions, snapshots and async shutdown. Depends on model-io, render-core. Existing native provider contracts remain unchanged.

## Public contract

Snapshots freeze their nested progress/error/stat values as well as the outer object. Recovery/disposal promise guards are installed before invoking observers, including observers that reenter these operations. Nonzero viewport must be verified after asynchronous upload and first-frame completion; a collapsed viewport keeps activation pending until a new validated frame is submitted at a nonzero size.

Recovery begins its invalidation synchronously after its promise guard is installed. Successful recovery republishes scene metadata from the actual active scene, including zero/null values after a concurrent close, so request identity changes cannot leave a stale sceneCount/source snapshot.

Close publishes zero/null metadata synchronously before awaiting GPU cleanup, and preserves Faulted until device recovery. Recovery failure republishes metadata from the actual active/retained scene too; a rejected open during recovery cannot make a closed scene appear in a stale snapshot. Cleanup always uses the renderer that owned the closed resources.

Camera controls include `fly(right, up, forward, seconds)`: displacement along local axes equals each speed factor times camera distance times clamp(seconds,0,0.1); forward is positive toward the view direction. `fit/resize/frame` are engine-facing Camera helpers; frame exposes the internal 128-byte uniform ABI and is not an independent renderer integration boundary.

`open` is latest-wins and preserves the previous scene until the first validated GPU frame. A zero viewport delays activation (cancellable, bounded by timeout). Resize/pause/resume preserve Loading/Uploading/Recovering/Faulted phases. `closeScene` invalidates pending and retained scenes; concurrent recovery may rebuild the device but must never resurrect a closed scene. `dispose` awaits in-flight upload/recovery cleanup before Stopped. New loads during recovery return DeviceLost. `capture` returns Result<{width,height,rgba}> of the current nonzero surface, with a bounded GPU readback; bytes are an owned copy. Capture is unavailable during loading/recovery. Camera methods validate and throw synchronously; asynchronous engine operations return Result.

Faulted requires recovery before a new open, and rejected open must preserve Faulted. Closing a scene must also release diagnostic CPU references. Failed initialization disposes a created renderer. URL and download policy failures distinguish InvalidInput/NetworkFailure/ResourceLimit.
System semantic constraints are defined in [system design](system-technical-design.md). TypeScript signatures live in the owning src module and are exported through index.ts; API changes update this provider document first. Expected errors use Result with stable codes; no null/string error substitutes. Scene data is opaque and renderer layout is private. Cloud integration is independently optional.

## Structure and style
ForWeb/src/engine/, ForWeb/tests/unit and contracts/gpu. C++ under native uses C++20, four spaces and snake_case; TS uses strict settings, four spaces, PascalCase types and camelCase functions. Example: `function dispose(): Promise<void>` is idempotent and never blocks the UI synchronously.

## Commands
From ForWeb: `pnpm run typecheck`; `pnpm run test:unit`; `pnpm run test:contracts`; `pnpm run test:gpu`; `pnpm run build`; `pnpm run test:sdk`. Native decoder: `pnpm run build:wasm`. Tests requiring hardware must not pass on a missing adapter.

## Verification and success criteria
Required cases: old scene retained, stale messages, zero viewport, loss, disposal races. Actual output/state must match the contract, not merely method invocation. Evidence records commands, versions, model/shader hashes and skipped devices. Performance runs are separate from unit tests.

## Boundaries
Always: validate inputs and resource arithmetic, preserve ownership/identity, test failures, pin dependencies. Review new scope/public semantics before changing them; routine authorized fixes proceed. Never: silently drop points/SH, bypass failing tests, copy unlicensed code, publish model files or credentials.
