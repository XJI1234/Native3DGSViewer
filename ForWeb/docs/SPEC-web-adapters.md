# Spec: web-adapters

2026-10-04. Implementation authorized by the user; provider specification for ForWeb/web-adapters.

## Objective and boundaries
React/Vue lifecycle bindings, DOM input, resize and SSR-safe imports. Depends on engine. Existing native provider contracts remain unchanged.

## Public contract
Each mounted view owns a separate Canvas and engine. Delayed creation after cleanup disposes immediately. React uses useSyncExternalStore; Vue uses markRaw/shallowRef, and both unsubscribe/unbind/remove Canvas on unmount. Options are fixed at mount. Default DOM input is mouse/single-pointer orbit, right-button pan and wheel dolly; pinch and keyboard fly bindings are left to the host. Engine imports are SSR-safe; creation and browser globals belong in a client boundary. The installed consumer verifies production and development StrictMode cleanup, and compiles the documented TSX/SFC (Vue tooling uses compatible TypeScript6.0.3).
System semantic constraints are defined in [system design](system-technical-design.md). TypeScript signatures live in the owning src module and are exported through index.ts; API changes update this provider document first. Expected errors use Result with stable codes; no null/string error substitutes. Scene data is opaque and renderer layout is private. Cloud integration is independently optional.

## Structure and style
ForWeb/src/web-adapters/, ForWeb/tests/unit and contracts/gpu. C++ under native uses C++20, four spaces and snake_case; TS uses strict settings, four spaces, PascalCase types and camelCase functions. Example: `function dispose(): Promise<void>` is idempotent and never blocks the UI synchronously.

## Commands
From ForWeb: `pnpm run typecheck`; `pnpm run test:unit`; `pnpm run test:contracts`; `pnpm run test:gpu`; `pnpm run build`; `pnpm run test:sdk`. Native decoder: `pnpm run build:wasm`. Tests requiring hardware must not pass on a missing adapter.

## Verification and success criteria
Required cases: StrictMode, mount/unmount, source watches, two instances, no globals at import. Actual output/state must match the contract, not merely method invocation. Evidence records commands, versions, model/shader hashes and skipped devices. Performance runs are separate from unit tests.

## Boundaries
Always: validate inputs and resource arithmetic, preserve ownership/identity, test failures, pin dependencies. Review new scope/public semantics before changing them; routine authorized fixes proceed. Never: silently drop points/SH, bypass failing tests, copy unlicensed code, publish model files or credentials.

### Host binding

Vue watches the host ref after DOM updates and releases each old binding, including late initialization. React accepts HTMLElement|null for reactive callback-ref hosts; the existing RefObject form is reserved for a fixed host mounted with the hook. Returned Vue engine refs are shallow readonly so WebEngine retains its public nominal type. Resize clamps physical dimensions uniformly to adapter limits; pointer completion only releases the tracked pointer.
