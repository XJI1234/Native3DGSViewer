# Spec: web-viewer

2026-10-04. Implementation authorized by the user; provider specification for ForWeb/web-viewer.

## Objective and boundaries
Reference host using public SDK, model input, diagnostics and camera controls. Depends on engine, web-adapters. Existing native provider contracts remain unchanged.

## Public contract
System semantic constraints are defined in [system design](system-technical-design.md). TypeScript signatures live in the owning src module and are exported through index.ts; API changes update this provider document first. Expected errors use Result with stable codes; no null/string error substitutes. Scene data is opaque and renderer layout is private. Cloud integration is independently optional.

## Structure and style
ForWeb/src/web-viewer/, ForWeb/tests/unit and contracts/gpu. C++ under native uses C++20, four spaces and snake_case; TS uses strict settings, four spaces, PascalCase types and camelCase functions. Example: `function dispose(): Promise<void>` is idempotent and never blocks the UI synchronously.

## Commands
From ForWeb: `pnpm run typecheck`; `pnpm run test:unit`; `pnpm run test:contracts`; `pnpm run test:gpu`; `pnpm run build`; `pnpm run test:sdk`. Native decoder: `pnpm run build:wasm`. Tests requiring hardware must not pass on a missing adapter.

## Verification and success criteria
Required cases: actual File/URL open, progress, cancel, pointer/keyboard, screenshots. Actual output/state must match the contract, not merely method invocation. Evidence records commands, versions, model/shader hashes and skipped devices. Performance runs are separate from unit tests.

## Boundaries
Always: validate inputs and resource arithmetic, preserve ownership/identity, test failures, pin dependencies. Review new scope/public semantics before changing them; routine authorized fixes proceed. Never: silently drop points/SH, bypass failing tests, copy unlicensed code, publish model files or credentials.


## Standalone template acceptance (preview.2)

`apps/react-viewer` and `apps/vue-viewer` are independent Vite/TypeScript projects consuming the exact installed SDK tarball, with pinned framework versions and lockfiles. Archives include local SDK and assets copy script; installation does not depend on repository source or a public npm package. Match Windows open/drop/replace/cancel/close, orbit/fly, fit/reset, persistent Y flip, progress/stats, device recovery and clear error states. Include URL input and PNG capture. The browser host cannot claim WinUI swapchain access, OS CLI paths, native installer logging or automatic native memory mitigation not present in Web SDK. Do not silently reduce quality; explicit resource errors preserve the previous scene. Inputs remain canonical after reflection. Verify actual rendered reflection and post-flip pan/orbit/fly, cleanup, production/development and responsive layouts. Package source excludes model files, node_modules, dist and credentials; manifests identify Web source commit and hashes even when attached to the older native tag v0.2.2.
