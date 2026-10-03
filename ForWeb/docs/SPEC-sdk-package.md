# Spec: sdk-package

2026-10-04. Implementation authorized by the user; provider specification for ForWeb/sdk-package.

## Objective and boundaries
ESM/types, external optional peers, self-hosted WASM/Worker, notices and installed consumers. Depends on published libraries. Existing native provider contracts remain unchanged.

## Public contract

Rebundled consumers explicitly host assets and set both `assets.baseUrl` (decoder.mjs/wasm) and `assets.workerUrl` (decoder.worker.js). Library-relative Vite worker URLs are not a portable rebundling contract. Worker override supports same-origin CSP deployment without blob workers. Build ships a stable worker filename and license/asset hashes; independently installed production consumer must decode successfully.
System semantic constraints are defined in [system design](system-technical-design.md). TypeScript signatures live in the owning src module and are exported through index.ts; API changes update this provider document first. Expected errors use Result with stable codes; no null/string error substitutes. Scene data is opaque and renderer layout is private. Cloud integration is independently optional.

## Structure and style
ForWeb/src/index.ts and react.ts/vue.ts, package.json/vite.config.ts, tools/package-notices.mjs and sdk-consumer.mjs own this capability; no placeholder src/sdk-package module exists. C++ under native uses C++20, four spaces and snake_case; TS uses strict settings, four spaces, PascalCase types and camelCase functions. Example: `function dispose(): Promise<void>` is idempotent and never blocks the UI synchronously.

## Commands
From ForWeb: `pnpm run typecheck`; `pnpm run test:unit`; `pnpm run test:contracts`; `pnpm run test:gpu`; `pnpm run build`; `pnpm run test:sdk`. Native decoder: `pnpm run build:wasm`. Tests requiring hardware must not pass on a missing adapter.

## Verification and success criteria
Required cases: tarball without source links, SSR import, asset subpath, license completeness. Actual output/state must match the contract, not merely method invocation. Evidence records commands, versions, model/shader hashes and skipped devices. Performance runs are separate from unit tests.

## Boundaries
Always: validate inputs and resource arithmetic, preserve ownership/identity, test failures, pin dependencies. Review new scope/public semantics before changing them; routine authorized fixes proceed. Never: silently drop points/SH, bypass failing tests, copy unlicensed code, publish model files or credentials.
