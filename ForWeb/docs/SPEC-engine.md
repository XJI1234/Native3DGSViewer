# Spec: engine

Optional `EngineOptions.sorting` and recovery/capture semantics follow [SPEC-adaptive-sorting.md](SPEC-adaptive-sorting.md). Preview.3 defaults to adaptive sorting and decoder auto/4; explicit strict/single opt-outs remain available.

2026-10-06 增量：EngineOptions 可选 decoder 配置与场景解码诊断见 [SPEC-parallel-decoder](SPEC-parallel-decoder.md)；默认可回退 auto，既有公共方法不变。

2026-10-04. Implementation authorized by the user; provider specification for ForWeb/engine.

## Objective and boundaries
Latest-wins load transactions, camera/surface/device revisions, snapshots and async shutdown. Depends on model-io, render-core. Existing native provider contracts remain unchanged.

## Public contract
Large-model backing has independent ownership across decode, GPU scene and recovery. closeScene/dispose wait for in-flight load completion and cancellation cleanup. Scene activation and snapshot publication remain synchronous before asynchronous old-scene cleanup; cleanup cannot publish a stale request. Cleanup failures produce a structured diagnostic while remaining GPU resources are destroyed.

2026-10-05 scheduling experiment: EngineOptions adds `maxFramesInFlight?: 1 | 2 | 3`. The candidate default is 2, subject to complete-frame, foreground presentation and latency measurements; 1 retains the previous gate. Invalid values fail initialization before creating a renderer. Automatic frames remain rAF paced, submit at most the configured number before completion, and consume only the latest camera when capacity is available. No FIFO of intermediate camera poses is retained. GPUQueue orders each frame's writeBuffer/compute/draw operations; shared scratch does not imply concurrent unordered access. Completion accounting belongs to a renderer generation: an old renderer's completion cannot release a new renderer's capacity. Pause, hidden/zero viewport, transaction presentation, loss and disposal still suppress automatic submissions. Capture and transaction validation retain their explicit GPU completion waits. No point/SH reduction or stale-sort rendering is enabled by this experiment.

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

### Transaction deadline and presentation

Load and recovery share their configured deadline across GPU fences, backing reads and validation. Disposal aborts recovery. An aborted upload cannot perform a late write. Candidate first frames are validated on a temporary offscreen texture; only a still-current request can present to the host canvas. Recovery validates GPU error scopes before publishing success and preserves the host camera.

Presentation uses validation/OOM scopes and abort-aware GPU completion. Active ownership and the host camera remain unchanged until presentation succeeds. Failure/cancellation restores the previous visible scene (also when paused), or clears a closed scene. Recovery publishes a terminal error at the deadline; late device/cleanup work cannot publish success. OPFS deletion has a 5-second cleanup bound and failures remain observable as StorageCleanup.


## Viewer navigation contract (preview.2)

`Camera.setFlipY(boolean)` / readonly `flipY` controls a display-only reflection about the scene bounds origin Y. It never mutates decoded splats, allocates a second scene, or changes `getPose()` / `setPose()` canonical coordinates. `frame()` reflects its three view rows and the eye about that origin, including the SH viewing direction. Reflection is performed inside the SDK; hosts MUST NOT add a CSS mirror. Flip persists across orbit/look/pan/fly, fit/reset, model replacement and recovery. Pointer deltas are always taken from untransformed client coordinates.

`Camera.setMode('orbit' | 'fly')` / readonly `mode` selects adapter navigation. Default orbit: left drag follows the pointer, right drag pans, wheel dollies. Fly: primary click requests pointer lock, raw relative mouse looks, WASD/QE translates, Shift accelerates, Escape/blur/hidden/mode change releases capture and clears keys. Pointer-lock failure is nonfatal and a primary drag remains available. Focused arrow keys orbit/look, +/- dolly, allowing keyboard access. Public methods reject invalid inputs without changing state. Mode and flip survive reset/fit.
