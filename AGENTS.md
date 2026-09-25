# Repository Guidelines

## Project Structure & Module Organization

Read `docs/technical-development-plan.md` and `docs/system-technical-design.md` for scope, then `docs/SPEC-model-io.md`, `docs/SPEC-render-core.md`, and `docs/SPEC-desktop-viewer.md` for module contracts. `model-io` and shared scene types are implemented in `src/model-io/` and `include/`; renderer and desktop app remain planned. Tests are in `tests/model-io/`. Pinned source dependencies and licenses are in `third_party/` submodules. Reference PLY/SPZ samples are in the parent workspace, not this project.

## Build, Test, and Development Commands

Initialize dependencies with `git submodule update --init --recursive`. The model-io solution uses CMake 4.3+ and VS 2026. From the project root:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
```

The first command builds Release x64 to `out/Release`; the second runs the full GoogleTest executable through a native VS test bridge DLL. No desktop application exists yet.

## Coding Style & Naming Conventions

Use C++20, four-space indentation, PascalCase for classes/enums, and `snake_case` for functions/variables. Format C++ with a pinned `clang-format` configuration once added. Keep `model-io` independent of D3D12 and `render-core` independent of file decoders and WinUI. Change the provider specification before changing a public contract; keep HLSL layout and host-side buffer definitions synchronized.

## Testing Guidelines

GoogleTest is the framework; name tests by module and behavior, such as `ModelIo.RejectsMalformedPly`. Cover valid and corrupt inputs, cancellation, resource limits, GPU sorting/image correctness, device recovery, and camera controls as those modules are implemented. Record sample hashes and compare screenshots at fixed cameras. Run equal-quality performance benchmarks separately from unit tests; use the acceptance thresholds in the technical plan. No numeric coverage target is defined yet.

## Commit & Pull Request Guidelines

Use concise imperative commit subjects, for example `Specify render-core device recovery`. PRs should link the relevant spec, describe contract or quality changes, list build/test evidence, and include screenshots or benchmark data for rendering changes. Keep third-party licenses and version pins with dependency updates.
