# Spec: render-core

2026-10-04. Implementation authorized by the user; provider specification for ForWeb/render-core.

## Objective and boundaries
GPU layouts, resource admission, global stable radix, projection, SH and premultiplied alpha. Depends on splat-types. Existing native provider contracts remain unchanged.

## Public contract

Capture reserves each in-flight readback allocation against the shared GPU budget before allocation. Upload admission also includes outstanding readback reservations; completion/failure releases reservations. Concurrent captures cannot bypass the configured budget.

The renderer tracks admitted scene bytes until actual release, including scenes detached by close while their GPU fence is pending. A new load during that interval cannot bypass the new/old GPU budget merely because engine.active is empty. Dispose releases all owned scenes and clears ownership references.

Stable LSD radix defaults to eight 4-bit passes. Although isolated 8-bit sorting improved, complete-frame retesting showed no benefit (small/medium slightly regressed), so 8-bit remains an internal comparison fixture. Admission includes 16-bin histogram/prefix, block offsets, parameters and minimum allocation sizes. Both preserve all points and exact stable uint32 order.
System semantic constraints are defined in [system design](system-technical-design.md). TypeScript signatures live in the owning src module and are exported through index.ts; API changes update this provider document first. Expected errors use Result with stable codes; no null/string error substitutes. Scene data is opaque and renderer layout is private. Cloud integration is independently optional.

## Structure and style
ForWeb/src/render-core/, ForWeb/tests/unit and contracts/gpu. C++ under native uses C++20, four spaces and snake_case; TS uses strict settings, four spaces, PascalCase types and camelCase functions. Example: `function dispose(): Promise<void>` is idempotent and never blocks the UI synchronously.

## Commands
From ForWeb: `pnpm run typecheck`; `pnpm run test:unit`; `pnpm run test:contracts`; `pnpm run test:gpu`; `pnpm run build`; `pnpm run test:sdk`. Native decoder: `pnpm run build:wasm`. Tests requiring hardware must not pass on a missing adapter.

## Verification and success criteria
Required cases: stable CPU reference, multi-page order, pixel fixtures, real GPU self-test. Actual output/state must match the contract, not merely method invocation. Evidence records commands, versions, model/shader hashes and skipped devices. Performance runs are separate from unit tests.

## Boundaries
Always: validate inputs and resource arithmetic, preserve ownership/identity, test failures, pin dependencies. Review new scope/public semantics before changing them; routine authorized fixes proceed. Never: silently drop points/SH, bypass failing tests, copy unlicensed code, publish model files or credentials.
