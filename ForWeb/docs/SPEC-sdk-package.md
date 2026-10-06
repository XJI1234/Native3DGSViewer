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


## Release artifact provenance

The current Web preview attached to native v0.2.2 is named
`Native3DGS-SDK-0.2.2-Web-WebGPU-preview.3.zip`, with package version
`0.2.2-preview.3` and internal tarball `native3dgs-web-0.2.2-preview.3.tgz`.
The matching standalone source templates are
`Native3DGS-Template-0.2.2-Web-React-preview.3.zip` and
`Native3DGS-Template-0.2.2-Web-Vue-preview.3.zip`.
Packaging requires committed source and a matching build inventory. Internal
MANIFEST.json identifies actual SDK/template commits, dependencies and per-file
SHA-256. Template vendor tarballs must exactly match the SDK archive tarball.
No independent hash assets are published. Replacement removes only superseded
Web SDK/template assets and their old sidecars; native platform assets and the
v0.2.2 tag remain. Archives include licenses/guides, exclude models/Spark and
node_modules, and do not merge the PR or publish to npm.

preview.3 defaults are decoder auto/4 (with single-thread fallback) and adaptive
sorting. Existing method signatures remain; omitted sorting changes behavior.
The migration guide must document strict/single opt-outs, threaded assets,
COOP/COEP/MIME, initialization-only options and measurement attribution.
Installed SDK and independent template consumers must test real WASM, both
frameworks, resource cleanup and the default behavior before publication.
