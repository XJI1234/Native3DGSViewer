# Spec: splat-types

2026-10-04. Implementation authorized by the user; provider specification for ForWeb/splat-types.

## Objective and boundaries
RUB/float64 origin, normalized floats, Result/EngineError, immutable snapshots. Depends on none. Existing native provider contracts remain unchanged.

## Public contract
System semantic constraints are defined in [system design](system-technical-design.md). TypeScript signatures live in the owning src module and are exported through index.ts; API changes update this provider document first. Expected errors use Result with stable codes; no null/string error substitutes. Scene data is opaque and renderer layout is private. Cloud integration is independently optional.

## Structure and style
ForWeb/src/splat-types/, ForWeb/tests/unit and contracts/gpu. C++ under native uses C++20, four spaces and snake_case; TS uses strict settings, four spaces, PascalCase types and camelCase functions. Example: `function dispose(): Promise<void>` is idempotent and never blocks the UI synchronously.

## Commands
From ForWeb: `pnpm run typecheck`; `pnpm run test:unit`; `pnpm run test:contracts`; `pnpm run test:gpu`; `pnpm run build`; `pnpm run test:sdk`. Native decoder: `pnpm run build:wasm`. Tests requiring hardware must not pass on a missing adapter.

## Verification and success criteria
Required cases: NaN, overflow, layout/version, camera fit and replay. Actual output/state must match the contract, not merely method invocation. Evidence records commands, versions, model/shader hashes and skipped devices. Performance runs are separate from unit tests.

## Boundaries
Always: validate inputs and resource arithmetic, preserve ownership/identity, test failures, pin dependencies. Review new scope/public semantics before changing them; routine authorized fixes proceed. Never: silently drop points/SH, bypass failing tests, copy unlicensed code, publish model files or credentials.

## Large-model extension (ADR-002)
Scene may provide reference-counted disk backing; pages remain the small-scene fixture contract. Backing exposes totalBytes, residentBytes, read(offset,length), retain(), release(). One transferred initial reference is always released by the load operation. Compact scalar packing is explicit. Limits are positive safe integers; disk streaming allows input/scene budgets beyond wasm32 address space while batch allocations remain bounded.
